#include "client/PlayerInputProbe.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
constexpr std::ptrdiff_t kPlayerInputSetInputRva = 0x00091210;
constexpr std::ptrdiff_t kRawButtonBridgeRva = 0x00091300;
constexpr std::ptrdiff_t kPlayerInputNormalizerRva = 0x000913A0;
constexpr DWORD kPlayerInputCount = 0x37;
constexpr DWORD kObservationWindowMs = 120000;
constexpr DWORD kReportIntervalMs = 500;
constexpr DWORD kAxisActiveMaskLow = 0x0000003F;
constexpr std::size_t kEventCapacity = 128;
constexpr std::size_t kFrameCapacity = 16;
constexpr std::size_t kMaskEdgeCapacity = 256;

constexpr BYTE kSetInputPrefix[] = {
    0x81, 0xEC, 0xFC, 0x00, 0x00, 0x00, 0x53, 0x56,
    0x8B, 0xB4, 0x24, 0x08, 0x01, 0x00, 0x00, 0x83};
constexpr BYTE kNormalizerPrefix[] = {
    0x83, 0xEC, 0x10, 0x89, 0x4C, 0x24, 0x00, 0x33,
    0xC0, 0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00};

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t expectedLength) noexcept
{
    if (target == nullptr || expected == nullptr || expectedLength == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, expectedLength) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct InputEvent
{
    volatile LONG sequence = 0;
    void* receiver = nullptr;
    DWORD inputId = 0;
    BYTE value = 0;
    DWORD threadId = 0;
    DWORD callerRva = 0;
};

struct NormalizedFrame
{
    volatile LONG sequence = 0;
    void* rawSource = nullptr;
    void* destination = nullptr;
    DWORD threadId = 0;
    DWORD callerRva = 0;
    DWORD enabledMaskLow = 0;
    DWORD enabledMaskHigh = 0;
    DWORD activeMaskLow = 0;
    DWORD activeMaskHigh = 0;
    std::array<float, kPlayerInputCount> values = {};
};

struct ActiveMaskEdge
{
    volatile LONG sequence = 0;
    void* rawSource = nullptr;
    void* destination = nullptr;
    DWORD threadId = 0;
    DWORD callerRva = 0;
    DWORD previousMaskLow = 0;
    DWORD previousMaskHigh = 0;
    DWORD activeMaskLow = 0;
    DWORD activeMaskHigh = 0;
    std::array<float, 6> axes = {};
};

class PlayerInputProbe
{
public:
    using SetInputFn = void(__thiscall*)(void* receiver, DWORD inputId, BYTE value);
    using NormalizeFn = DWORD(__thiscall*)(void* rawSource, float* destination);

    void Start(void* image, void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            WriteLog(L"Player-input probe ignored a duplicate start request.");
            return;
        }

        gameImage = static_cast<std::byte*>(image);
        appendLog = log;
        for (volatile LONG& lastValue : lastExternalInputValues)
        {
            InterlockedExchange(&lastValue, -1);
        }
        InterlockedExchange(&lastNormalizedButtonMaskLow, 0);
        InterlockedExchange(&lastNormalizedButtonMaskHigh, 0);
        setInputTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kPlayerInputSetInputRva;
        normalizerTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kPlayerInputNormalizerRva;
        if (!HasExpectedPrefix(
                setInputTarget,
                kSetInputPrefix,
                sizeof(kSetInputPrefix)) ||
            !HasExpectedPrefix(
                normalizerTarget,
                kNormalizerPrefix,
                sizeof(kNormalizerPrefix)))
        {
            WriteLog(
                L"Player-input probe rejected profiled targets setInput=%p normalizer=%p: one or both WinPC prefixes differ.",
                setInputTarget,
                normalizerTarget);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Player-input probe could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            return;
        }

        const MH_STATUS createSetter = MH_CreateHook(
            setInputTarget,
            reinterpret_cast<LPVOID>(&PlayerInputProbe::SetInputHook),
            reinterpret_cast<LPVOID*>(&originalSetInput));
        const MH_STATUS createNormalizer = createSetter == MH_OK
            ? MH_CreateHook(
                normalizerTarget,
                reinterpret_cast<LPVOID>(&PlayerInputProbe::NormalizeHook),
                reinterpret_cast<LPVOID*>(&originalNormalize))
            : MH_ERROR_NOT_CREATED;
        if (createSetter != MH_OK || createNormalizer != MH_OK ||
            originalSetInput == nullptr || originalNormalize == nullptr)
        {
            WriteLog(
                L"Player-input probe could not create its forwarding hooks (setInput=%d normalizer=%d).",
                static_cast<int>(createSetter),
                static_cast<int>(createNormalizer));
            RemoveHooks();
            return;
        }
        setterCreated = true;
        normalizerCreated = true;

        active = this;
        const MH_STATUS enableSetter = MH_EnableHook(setInputTarget);
        const MH_STATUS enableNormalizer = enableSetter == MH_OK
            ? MH_EnableHook(normalizerTarget)
            : MH_ERROR_ENABLED;
        if (enableSetter != MH_OK || enableNormalizer != MH_OK)
        {
            WriteLog(
                L"Player-input probe could not enable its forwarding hooks (setInput=%d normalizer=%d).",
                static_cast<int>(enableSetter),
                static_cast<int>(enableNormalizer));
            RemoveHooks();
            return;
        }
        setterEnabled = true;
        normalizerEnabled = true;

        HANDLE worker = CreateThread(
            nullptr,
            0,
            &PlayerInputProbe::Run,
            this,
            0,
            nullptr);
        if (worker == nullptr)
        {
            WriteLog(
                L"Player-input probe could not start its reporting worker (%lu).",
                GetLastError());
            RemoveHooks();
            return;
        }
        CloseHandle(worker);
        WriteLog(
            L"Player-input probe enabled verified forwarding hooks at 0x00491210 and 0x004913A0 for %lu seconds. Normalizer-internal setter traffic is aggregated as frame active-slot masks; only non-normalizer setter callers are individually logged. No BF1942 input/state is authored.",
            kObservationWindowMs / 1000);
    }

private:
    static void __fastcall SetInputHook(
        void* receiver,
        void*,
        DWORD inputId,
        BYTE value)
    {
        PlayerInputProbe* const probe = active;
        if (probe == nullptr || probe->originalSetInput == nullptr)
        {
            return;
        }
        probe->RecordInputEvent(receiver, inputId, value, _ReturnAddress());
        probe->originalSetInput(receiver, inputId, value);
    }

    static DWORD __fastcall NormalizeHook(
        void* rawSource,
        void*,
        float* destination)
    {
        PlayerInputProbe* const probe = active;
        if (probe == nullptr || probe->originalNormalize == nullptr)
        {
            return 0;
        }
        const DWORD result = probe->originalNormalize(rawSource, destination);
        probe->RecordNormalizedFrame(rawSource, destination, _ReturnAddress());
        return result;
    }

    static DWORD WINAPI Run(LPVOID parameter)
    {
        auto* const probe = static_cast<PlayerInputProbe*>(parameter);
        if (probe != nullptr)
        {
            probe->ReportUntilComplete();
        }
        return 0;
    }

    void RecordInputEvent(
        void* receiver,
        DWORD inputId,
        BYTE value,
        const void* callerReturn) noexcept
    {
        if (inputId >= kPlayerInputCount)
        {
            return;
        }
        const DWORD callerRva = ToRva(callerReturn);
        if (callerRva >= kRawButtonBridgeRva &&
            callerRva < kPlayerInputNormalizerRva)
        {
            return;
        }
        volatile LONG& previousValue = lastExternalInputValues[static_cast<std::size_t>(inputId)];
        if (InterlockedExchange(&previousValue, static_cast<LONG>(value)) ==
            static_cast<LONG>(value))
        {
            return;
        }
        const LONG sequence = InterlockedIncrement(&nextEventSequence);
        InputEvent& event = events[static_cast<std::size_t>(sequence) % events.size()];
        InterlockedExchange(&event.sequence, 0);
        event.receiver = receiver;
        event.inputId = inputId;
        event.value = value;
        event.threadId = GetCurrentThreadId();
        event.callerRva = callerRva;
        MemoryBarrier();
        InterlockedExchange(&event.sequence, sequence);
    }

    void RecordNormalizedFrame(
        void* rawSource,
        float* destination,
        const void* callerReturn) noexcept
    {
        if (destination == nullptr)
        {
            return;
        }
        const LONG sequence = InterlockedIncrement(&nextFrameSequence);
        NormalizedFrame& frame = frames[static_cast<std::size_t>(sequence) % frames.size()];
        InterlockedExchange(&frame.sequence, 0);
        frame.activeMaskLow = 0;
        frame.activeMaskHigh = 0;
        bool readable = false;
        __try
        {
            std::memcpy(frame.values.data(), destination, sizeof(frame.values));
            frame.enabledMaskLow = *reinterpret_cast<const DWORD*>(
                reinterpret_cast<const std::byte*>(destination) + 0xE0);
            frame.enabledMaskHigh = *reinterpret_cast<const DWORD*>(
                reinterpret_cast<const std::byte*>(destination) + 0xE4);
            for (std::size_t index = 0; index < frame.values.size(); ++index)
            {
                if (std::fabs(frame.values[index]) <= 0.001F)
                {
                    continue;
                }
                if (index < 32)
                {
                    frame.activeMaskLow |= 1U << index;
                }
                else
                {
                    frame.activeMaskHigh |= 1U << (index - 32);
                }
            }
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (!readable)
        {
            return;
        }
        frame.rawSource = rawSource;
        frame.destination = destination;
        frame.threadId = GetCurrentThreadId();
        frame.callerRva = ToRva(callerReturn);
        InterlockedOr(
            &observedActiveMaskLow,
            static_cast<LONG>(frame.activeMaskLow));
        InterlockedOr(
            &observedActiveMaskHigh,
            static_cast<LONG>(frame.activeMaskHigh));
        RecordActiveMaskEdge(frame);
        MemoryBarrier();
        InterlockedExchange(&frame.sequence, sequence);
    }

    void RecordActiveMaskEdge(const NormalizedFrame& frame) noexcept
    {
        const DWORD buttonMaskLow = frame.activeMaskLow & ~kAxisActiveMaskLow;
        const DWORD buttonMaskHigh = frame.activeMaskHigh;
        const DWORD previousMaskLow = static_cast<DWORD>(
            InterlockedExchange(
                &lastNormalizedButtonMaskLow,
                static_cast<LONG>(buttonMaskLow)));
        const DWORD previousMaskHigh = static_cast<DWORD>(
            InterlockedExchange(
                &lastNormalizedButtonMaskHigh,
                static_cast<LONG>(buttonMaskHigh)));
        if (previousMaskLow == buttonMaskLow &&
            previousMaskHigh == buttonMaskHigh)
        {
            return;
        }

        const LONG sequence = InterlockedIncrement(&nextMaskEdgeSequence);
        ActiveMaskEdge& edge = maskEdges[
            static_cast<std::size_t>(sequence) % maskEdges.size()];
        InterlockedExchange(&edge.sequence, 0);
        edge.rawSource = frame.rawSource;
        edge.destination = frame.destination;
        edge.threadId = frame.threadId;
        edge.callerRva = frame.callerRva;
        edge.previousMaskLow = previousMaskLow;
        edge.previousMaskHigh = previousMaskHigh;
        edge.activeMaskLow = buttonMaskLow;
        edge.activeMaskHigh = buttonMaskHigh;
        for (std::size_t index = 0; index < edge.axes.size(); ++index)
        {
            edge.axes[index] = frame.values[index];
        }
        MemoryBarrier();
        InterlockedExchange(&edge.sequence, sequence);
    }

    void ReportUntilComplete()
    {
        LONG nextEventToReport = 1;
        LONG nextMaskEdgeToReport = 1;
        LONG lastFrameReported = 0;
        NormalizedFrame lastReportedFrame = {};
        bool haveLastReportedFrame = false;
        const DWORD startedAt = GetTickCount();
        while (GetTickCount() - startedAt < kObservationWindowMs)
        {
            Sleep(kReportIntervalMs);
            ReportInputEvents(nextEventToReport);
            ReportActiveMaskEdges(nextMaskEdgeToReport);
            ReportLatestFrame(lastFrameReported, lastReportedFrame, haveLastReportedFrame);
        }
        ReportInputEvents(nextEventToReport);
        ReportActiveMaskEdges(nextMaskEdgeToReport);
        ReportLatestFrame(lastFrameReported, lastReportedFrame, haveLastReportedFrame);
        WriteLog(
            L"Player-input probe completed: external setter events=%ld button-mask edges=%ld normalized frames=%ld observedActiveSlots=[0x%08lX 0x%08lX]. Removing forwarding hooks without writing BF1942 input/state.",
            InterlockedCompareExchange(&nextEventSequence, 0, 0),
            InterlockedCompareExchange(&nextMaskEdgeSequence, 0, 0),
            InterlockedCompareExchange(&nextFrameSequence, 0, 0),
            static_cast<DWORD>(InterlockedCompareExchange(&observedActiveMaskLow, 0, 0)),
            static_cast<DWORD>(InterlockedCompareExchange(&observedActiveMaskHigh, 0, 0)));
        RemoveHooks();
    }

    void ReportInputEvents(LONG& nextSequence)
    {
        const LONG latest = InterlockedCompareExchange(&nextEventSequence, 0, 0);
        if (latest < nextSequence)
        {
            return;
        }
        if (latest - nextSequence >= static_cast<LONG>(events.size()))
        {
            WriteLog(
                L"Player-input probe event ring overran %ld transition(s); retaining the newest %zu observations.",
                latest - nextSequence - static_cast<LONG>(events.size()) + 1,
                events.size());
            nextSequence = latest - static_cast<LONG>(events.size()) + 1;
        }
        for (; nextSequence <= latest; ++nextSequence)
        {
            const InputEvent& event = events[
                static_cast<std::size_t>(nextSequence) % events.size()];
            if (InterlockedCompareExchange(
                    const_cast<volatile LONG*>(&event.sequence),
                    0,
                    0) != nextSequence)
            {
                continue;
            }
            WriteLog(
                L"Player-input setInput seq=%ld id=%lu value=%u receiver=%p thread=%lu callerRva=0x%05lX.",
                nextSequence,
                event.inputId,
                static_cast<unsigned int>(event.value),
                event.receiver,
                event.threadId,
                event.callerRva);
        }
    }

    void ReportActiveMaskEdges(LONG& nextSequence)
    {
        const LONG latest = InterlockedCompareExchange(&nextMaskEdgeSequence, 0, 0);
        if (latest < nextSequence)
        {
            return;
        }
        if (latest - nextSequence >= static_cast<LONG>(maskEdges.size()))
        {
            WriteLog(
                L"Player-input button-mask edge ring overran %ld transition(s); retaining the newest %zu observations.",
                latest - nextSequence - static_cast<LONG>(maskEdges.size()) + 1,
                maskEdges.size());
            nextSequence = latest - static_cast<LONG>(maskEdges.size()) + 1;
        }
        for (; nextSequence <= latest; ++nextSequence)
        {
            const ActiveMaskEdge& edge = maskEdges[
                static_cast<std::size_t>(nextSequence) % maskEdges.size()];
            if (InterlockedCompareExchange(
                    const_cast<volatile LONG*>(&edge.sequence),
                    0,
                    0) != nextSequence)
            {
                continue;
            }
            WriteLog(
                L"Player-input button-mask edge seq=%ld source=%p destination=%p thread=%lu callerRva=0x%05lX buttons=[0x%08lX 0x%08lX]->[0x%08lX 0x%08lX] axes[0..5]=[%.3f %.3f %.3f %.3f %.3f %.3f].",
                nextSequence,
                edge.rawSource,
                edge.destination,
                edge.threadId,
                edge.callerRva,
                edge.previousMaskLow,
                edge.previousMaskHigh,
                edge.activeMaskLow,
                edge.activeMaskHigh,
                edge.axes[0],
                edge.axes[1],
                edge.axes[2],
                edge.axes[3],
                edge.axes[4],
                edge.axes[5]);
        }
    }

    void ReportLatestFrame(
        LONG& lastSequence,
        NormalizedFrame& previous,
        bool& hasPrevious)
    {
        const LONG latest = InterlockedCompareExchange(&nextFrameSequence, 0, 0);
        if (latest <= lastSequence)
        {
            return;
        }
        const NormalizedFrame& source = frames[
            static_cast<std::size_t>(latest) % frames.size()];
        if (InterlockedCompareExchange(
                const_cast<volatile LONG*>(&source.sequence),
                0,
                0) != latest)
        {
            return;
        }
        NormalizedFrame snapshot = {};
        snapshot.sequence = latest;
        snapshot.rawSource = source.rawSource;
        snapshot.destination = source.destination;
        snapshot.threadId = source.threadId;
        snapshot.callerRva = source.callerRva;
        snapshot.enabledMaskLow = source.enabledMaskLow;
        snapshot.enabledMaskHigh = source.enabledMaskHigh;
        snapshot.activeMaskLow = source.activeMaskLow;
        snapshot.activeMaskHigh = source.activeMaskHigh;
        snapshot.values = source.values;
        lastSequence = latest;
        if (hasPrevious && !FrameChanged(previous, snapshot))
        {
            return;
        }
        WriteLog(
            L"Player-input normalized frame seq=%ld source=%p destination=%p thread=%lu callerRva=0x%05lX axes[0..5]=[%.3f %.3f %.3f %.3f %.3f %.3f] enabled=[0x%08lX 0x%08lX] active=[0x%08lX 0x%08lX].",
            latest,
            snapshot.rawSource,
            snapshot.destination,
            snapshot.threadId,
            snapshot.callerRva,
            snapshot.values[0],
            snapshot.values[1],
            snapshot.values[2],
            snapshot.values[3],
            snapshot.values[4],
            snapshot.values[5],
            snapshot.enabledMaskLow,
            snapshot.enabledMaskHigh,
            snapshot.activeMaskLow,
            snapshot.activeMaskHigh);
        previous = snapshot;
        hasPrevious = true;
    }

    static bool FrameChanged(
        const NormalizedFrame& first,
        const NormalizedFrame& second) noexcept
    {
        if (first.rawSource != second.rawSource ||
            first.destination != second.destination ||
            first.enabledMaskLow != second.enabledMaskLow ||
            first.enabledMaskHigh != second.enabledMaskHigh ||
            first.activeMaskLow != second.activeMaskLow ||
            first.activeMaskHigh != second.activeMaskHigh)
        {
            return true;
        }
        for (std::size_t index = 0; index < 6; ++index)
        {
            if (std::fabs(first.values[index] - second.values[index]) > 0.001F)
            {
                return true;
            }
        }
        return false;
    }

    DWORD ToRva(const void* address) const noexcept
    {
        const auto image = reinterpret_cast<std::uintptr_t>(gameImage);
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        return image != 0 && value >= image
            ? static_cast<DWORD>(value - image)
            : 0;
    }

    void RemoveHooks()
    {
        if (normalizerEnabled)
        {
            MH_DisableHook(normalizerTarget);
            normalizerEnabled = false;
        }
        if (setterEnabled)
        {
            MH_DisableHook(setInputTarget);
            setterEnabled = false;
        }
        if (normalizerCreated)
        {
            MH_RemoveHook(normalizerTarget);
            normalizerCreated = false;
        }
        if (setterCreated)
        {
            MH_RemoveHook(setInputTarget);
            setterCreated = false;
        }
        if (active == this)
        {
            active = nullptr;
        }
        originalSetInput = nullptr;
        originalNormalize = nullptr;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
        {
            return;
        }
        std::array<wchar_t, 900> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog(message.data());
    }

    static PlayerInputProbe* active;

    std::byte* gameImage = nullptr;
    void (*appendLog)(const wchar_t* message) = nullptr;
    void* setInputTarget = nullptr;
    void* normalizerTarget = nullptr;
    SetInputFn originalSetInput = nullptr;
    NormalizeFn originalNormalize = nullptr;
    std::array<InputEvent, kEventCapacity> events = {};
    std::array<NormalizedFrame, kFrameCapacity> frames = {};
    std::array<ActiveMaskEdge, kMaskEdgeCapacity> maskEdges = {};
    std::array<volatile LONG, kPlayerInputCount> lastExternalInputValues = {};
    volatile LONG nextEventSequence = 0;
    volatile LONG nextMaskEdgeSequence = 0;
    volatile LONG nextFrameSequence = 0;
    volatile LONG lastNormalizedButtonMaskLow = 0;
    volatile LONG lastNormalizedButtonMaskHigh = 0;
    volatile LONG observedActiveMaskLow = 0;
    volatile LONG observedActiveMaskHigh = 0;
    volatile LONG started = 0;
    bool ownsMinHook = false;
    bool setterCreated = false;
    bool normalizerCreated = false;
    bool setterEnabled = false;
    bool normalizerEnabled = false;
};

PlayerInputProbe* PlayerInputProbe::active = nullptr;
PlayerInputProbe g_playerInputProbe = {};
} // namespace

namespace bfvr
{
void StartPlayerInputProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_playerInputProbe.Start(gameImage, appendLog);
}
} // namespace bfvr
