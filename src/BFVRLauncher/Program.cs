using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

namespace BFVRLauncher;

internal static class Program
{
    private const string DefaultProfileName = "bf1942-win32-decbb52f.json";

    private static int Main(string[] args)
    {
        try
        {
            var options = LauncherOptions.Parse(args);
            if (options.ShowHelp)
            {
                PrintUsage();
                return 0;
            }

            if (options.EnableVr)
            {
                Console.Error.WriteLine("[BLOCKED] BFVR VR attachment is not implemented yet.");
                Console.Error.WriteLine("[INFO] Use --verify or --launch-flat; neither modifies game files.");
                return 2;
            }

            var profile = LoadProfile(options.ProfilePath);
            var verification = Verify(options.GameRoot, profile);
            verification.WriteToConsole();

            if (!options.LaunchFlat)
            {
                return verification.ErrorCount == 0 ? 0 : 2;
            }

            if (verification.ErrorCount != 0)
            {
                Console.Error.WriteLine("[BLOCKED] Flat launch was not attempted because the executable does not match this BFVR profile.");
                return 2;
            }

            return LaunchFlat(options.GameRoot, profile, options.GameArguments);
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"[FAIL] {exception.Message}");
            return 2;
        }
    }

    private static BuildProfile LoadProfile(string profilePath)
    {
        if (!File.Exists(profilePath))
        {
            throw new FileNotFoundException("BFVR build profile was not found.", profilePath);
        }

        var json = File.ReadAllText(profilePath);
        var profile = JsonSerializer.Deserialize<BuildProfile>(json, new JsonSerializerOptions
        {
            PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
            PropertyNameCaseInsensitive = true
        });

        if (profile is null || profile.SchemaVersion != 1 || profile.Executable is null || profile.ObservedEnvironment is null)
        {
            throw new InvalidDataException("Unsupported or incomplete BFVR build profile.");
        }

        return profile;
    }

    private static VerificationResult Verify(string gameRoot, BuildProfile profile)
    {
        var result = new VerificationResult();
        var executablePath = Path.Combine(gameRoot, profile.Executable.Filename);

        if (!File.Exists(executablePath))
        {
            result.Error($"Missing expected executable: {executablePath}");
            return result;
        }

        var executableHash = ComputeSha256(executablePath);
        if (HashesMatch(executableHash, profile.Executable.Sha256))
        {
            result.Pass($"Executable matches profile {profile.ProfileId}: {profile.Executable.Filename}");
        }
        else
        {
            result.Error($"Executable hash differs from profile {profile.ProfileId}. BFVR must not attach.");
        }

        var expectedModules = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in profile.ObservedEnvironment.ObservedRootModules ?? [])
        {
            var expected = ParseModuleEntry(entry);
            expectedModules.Add(expected.Name);
            var modulePath = Path.Combine(gameRoot, expected.Name);
            if (!File.Exists(modulePath))
            {
                result.Warning($"Profiled module is absent: {expected.Name}");
                continue;
            }

            var moduleHash = ComputeSha256(modulePath);
            if (HashesMatch(moduleHash, expected.Sha256))
            {
                result.Pass($"Profiled module matches: {expected.Name}");
            }
            else
            {
                result.Warning($"Profiled module hash differs: {expected.Name}. Coexistence is unvalidated.");
            }
        }

        foreach (var modulePath in Directory.EnumerateFiles(gameRoot, "*.dll", SearchOption.TopDirectoryOnly))
        {
            var moduleName = Path.GetFileName(modulePath);
            if (!expectedModules.Contains(moduleName))
            {
                result.Warning($"Unprofiled root DLL detected: {moduleName}. No BFVR attach decision is available.");
            }
        }

        result.Info("This launcher is read-only until a validated BFVR runtime exists.");
        return result;
    }

    private static int LaunchFlat(string gameRoot, BuildProfile profile, IReadOnlyList<string> gameArguments)
    {
        var executablePath = Path.Combine(gameRoot, profile.Executable.Filename);
        var startInfo = new ProcessStartInfo
        {
            FileName = executablePath,
            WorkingDirectory = gameRoot,
            UseShellExecute = false
        };

        foreach (var argument in gameArguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        var process = Process.Start(startInfo) ?? throw new InvalidOperationException("Unable to start the flat Battlefield 1942 client.");
        Console.WriteLine($"[PASS] Started flat game process {process.Id}; no BFVR module was loaded.");
        return 0;
    }

    private static (string Name, string Sha256) ParseModuleEntry(string entry)
    {
        var separator = entry.IndexOf(':');
        if (separator <= 0 || separator == entry.Length - 1)
        {
            throw new InvalidDataException($"Invalid module entry in BFVR profile: {entry}");
        }

        return (entry[..separator].Trim(), entry[(separator + 1)..].Trim());
    }

    private static string ComputeSha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private static bool HashesMatch(string left, string right) =>
        string.Equals(left, right, StringComparison.OrdinalIgnoreCase);

    private static void PrintUsage()
    {
        Console.WriteLine("BFVRLauncher (no-attach foundation)");
        Console.WriteLine("Usage: BFVRLauncher [--verify | --launch-flat] [--game-root <path>] [--profile <path>] [-- <game arguments>]");
        Console.WriteLine("       BFVRLauncher --enable-vr  (currently refuses; runtime is not implemented)");
    }

    private sealed record LauncherOptions(
        string GameRoot,
        string ProfilePath,
        bool LaunchFlat,
        bool EnableVr,
        bool ShowHelp,
        IReadOnlyList<string> GameArguments)
    {
        public static LauncherOptions Parse(string[] args)
        {
            var bfvrRoot = AppContext.BaseDirectory;
            var defaultGameRoot = Path.GetFullPath(Path.Combine(bfvrRoot, ".."));
            var gameRoot = defaultGameRoot;
            var profilePath = Path.Combine(bfvrRoot, "profiles", DefaultProfileName);
            var launchFlat = false;
            var enableVr = false;
            var showHelp = false;
            var gameArguments = new List<string>();

            for (var index = 0; index < args.Length; index++)
            {
                var argument = args[index];
                if (argument == "--")
                {
                    gameArguments.AddRange(args[(index + 1)..]);
                    break;
                }

                switch (argument)
                {
                    case "--game-root":
                        gameRoot = RequireValue(args, ref index, argument);
                        break;
                    case "--profile":
                        profilePath = RequireValue(args, ref index, argument);
                        break;
                    case "--launch-flat":
                        launchFlat = true;
                        break;
                    case "--verify":
                        break;
                    case "--enable-vr":
                        enableVr = true;
                        break;
                    case "--help":
                    case "-h":
                    case "/?":
                        showHelp = true;
                        break;
                    default:
                        throw new ArgumentException($"Unknown launcher argument: {argument}");
                }
            }

            return new LauncherOptions(
                Path.GetFullPath(gameRoot),
                Path.GetFullPath(profilePath),
                launchFlat,
                enableVr,
                showHelp,
                gameArguments);
        }

        private static string RequireValue(string[] args, ref int index, string option)
        {
            if (index + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {option}.");
            }

            return args[++index];
        }
    }

    private sealed class VerificationResult
    {
        private readonly List<(string Kind, string Message)> entries = [];

        public int ErrorCount { get; private set; }

        public void Pass(string message) => entries.Add(("PASS", message));

        public void Warning(string message) => entries.Add(("WARN", message));

        public void Error(string message)
        {
            ErrorCount++;
            entries.Add(("FAIL", message));
        }

        public void Info(string message) => entries.Add(("INFO", message));

        public void WriteToConsole()
        {
            foreach (var (kind, message) in entries)
            {
                Console.WriteLine($"[{kind}] {message}");
            }
        }
    }

    private sealed class BuildProfile
    {
        public int SchemaVersion { get; init; }
        public string ProfileId { get; init; } = string.Empty;
        public required ExecutableProfile Executable { get; init; }
        public required ObservedEnvironmentProfile ObservedEnvironment { get; init; }
    }

    private sealed class ExecutableProfile
    {
        public string Filename { get; init; } = string.Empty;
        public string Sha256 { get; init; } = string.Empty;
    }

    private sealed class ObservedEnvironmentProfile
    {
        public string[]? ObservedRootModules { get; init; }
    }
}
