#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct ArchiveEntry
{
    std::string name;
    std::uint32_t packedSize = 0;
    std::uint32_t unpackedSize = 0;
    std::uint32_t offset = 0;
    bool storedRaw = false;
};

struct LzoBlock
{
    std::uint32_t packedSize = 0;
    std::uint32_t unpackedSize = 0;
    std::uint32_t relativeOffset = 0;
};

bool ReadU32(std::ifstream& stream, std::uint32_t& value)
{
    std::uint8_t bytes[4] = {};
    stream.read(
        reinterpret_cast<char*>(bytes),
        static_cast<std::streamsize>(sizeof(bytes)));
    if (!stream)
    {
        return false;
    }
    value =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}

std::string NormalizePath(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return character == '\\'
                ? '/'
                : static_cast<char>(std::tolower(character));
        });
    return value;
}

std::optional<ArchiveEntry> FindEntry(
    std::ifstream& stream,
    const std::string& requested)
{
    stream.seekg(0, std::ios::beg);
    std::uint32_t indexOffset = 0;
    std::uint32_t version = 0;
    if (!ReadU32(stream, indexOffset) || !ReadU32(stream, version) ||
        version > 1)
    {
        return std::nullopt;
    }
    stream.seekg(indexOffset, std::ios::beg);
    std::uint32_t entryCount = 0;
    if (!ReadU32(stream, entryCount) || entryCount > 1000000U)
    {
        return std::nullopt;
    }

    const std::string normalizedRequested = NormalizePath(requested);
    for (std::uint32_t index = 0; index < entryCount; ++index)
    {
        std::uint32_t nameLength = 0;
        if (!ReadU32(stream, nameLength) || nameLength > 32768U)
        {
            return std::nullopt;
        }
        ArchiveEntry entry = {};
        entry.storedRaw = version == 0;
        entry.name.resize(nameLength);
        stream.read(entry.name.data(), static_cast<std::streamsize>(nameLength));
        std::uint32_t ignored[3] = {};
        if (!stream ||
            !ReadU32(stream, entry.packedSize) ||
            !ReadU32(stream, entry.unpackedSize) ||
            !ReadU32(stream, entry.offset) ||
            !ReadU32(stream, ignored[0]) ||
            !ReadU32(stream, ignored[1]) ||
            !ReadU32(stream, ignored[2]))
        {
            return std::nullopt;
        }
        if (NormalizePath(entry.name) == normalizedRequested)
        {
            return entry;
        }
    }
    return std::nullopt;
}

bool CopyLiterals(
    const std::uint8_t*& input,
    const std::uint8_t* inputEnd,
    std::uint8_t*& output,
    const std::uint8_t* outputEnd,
    std::size_t count)
{
    if (count > static_cast<std::size_t>(inputEnd - input) ||
        count > static_cast<std::size_t>(outputEnd - output))
    {
        return false;
    }
    std::copy_n(input, count, output);
    input += count;
    output += count;
    return true;
}

bool CopyMatch(
    std::uint8_t*& output,
    const std::uint8_t* outputBegin,
    const std::uint8_t* outputEnd,
    std::size_t distance,
    std::size_t count)
{
    if (distance == 0 ||
        distance > static_cast<std::size_t>(output - outputBegin) ||
        count > static_cast<std::size_t>(outputEnd - output))
    {
        return false;
    }
    std::uint8_t* source = output - distance;
    for (std::size_t index = 0; index < count; ++index)
    {
        *output++ = *source++;
    }
    return true;
}

bool ReadExtendedLength(
    const std::uint8_t*& input,
    const std::uint8_t* inputEnd,
    std::size_t base,
    std::size_t& length)
{
    length = 0;
    while (input < inputEnd && *input == 0)
    {
        length += 255U;
        ++input;
    }
    if (input >= inputEnd)
    {
        return false;
    }
    length += base + *input++;
    return true;
}

bool DecompressLzo1x(
    const std::vector<std::uint8_t>& packed,
    std::size_t unpackedSize,
    std::vector<std::uint8_t>& unpacked)
{
    unpacked.assign(unpackedSize, 0);
    if (packed.empty())
    {
        return unpackedSize == 0;
    }
    const std::uint8_t* input = packed.data();
    const std::uint8_t* const inputEnd = input + packed.size();
    std::uint8_t* output = unpacked.data();
    std::uint8_t* const outputBegin = output;
    const std::uint8_t* const outputEnd = output + unpacked.size();
    std::size_t token = 0;

    if (*input > 17U)
    {
        token = *input++ - 17U;
        if (token >= 4U)
        {
            if (!CopyLiterals(
                    input,
                    inputEnd,
                    output,
                    outputEnd,
                    token))
            {
                return false;
            }
            if (input >= inputEnd)
            {
                return false;
            }
            token = *input++;
            if (token < 16U)
            {
                if (input >= inputEnd)
                {
                    return false;
                }
                const std::size_t distance =
                    0x801U + (token >> 2U) +
                    (static_cast<std::size_t>(*input++) << 2U);
                if (!CopyMatch(
                        output,
                        outputBegin,
                        outputEnd,
                        distance,
                        3U))
                {
                    return false;
                }
                goto match_done;
            }
            goto match;
        }
        goto match_next;
    }

    while (input < inputEnd)
    {
        token = *input++;
        if (token >= 16U)
        {
            goto match;
        }
        if (token == 0U &&
            !ReadExtendedLength(input, inputEnd, 15U, token))
        {
            return false;
        }
        token += 3U;
        if (!CopyLiterals(
                input,
                inputEnd,
                output,
                outputEnd,
                token) ||
            input >= inputEnd)
        {
            return false;
        }
        token = *input++;
        if (token < 16U)
        {
            if (input >= inputEnd)
            {
                return false;
            }
            const std::size_t distance =
                0x801U + (token >> 2U) +
                (static_cast<std::size_t>(*input++) << 2U);
            if (!CopyMatch(
                    output,
                    outputBegin,
                    outputEnd,
                    distance,
                    3U))
            {
                return false;
            }
            goto match_done;
        }

match:
        if (token >= 64U)
        {
            if (input >= inputEnd)
            {
                return false;
            }
            const std::size_t distance =
                1U + ((token >> 2U) & 7U) +
                (static_cast<std::size_t>(*input++) << 3U);
            const std::size_t length = (token >> 5U) + 1U;
            if (!CopyMatch(
                    output,
                    outputBegin,
                    outputEnd,
                    distance,
                    length))
            {
                return false;
            }
        }
        else if (token >= 32U)
        {
            std::size_t length = token & 31U;
            if (length == 0U &&
                !ReadExtendedLength(input, inputEnd, 31U, length))
            {
                return false;
            }
            if (inputEnd - input < 2)
            {
                return false;
            }
            const std::size_t distance =
                1U + (input[0] >> 2U) +
                (static_cast<std::size_t>(input[1]) << 6U);
            input += 2;
            if (!CopyMatch(
                    output,
                    outputBegin,
                    outputEnd,
                    distance,
                    length + 2U))
            {
                return false;
            }
        }
        else if (token >= 16U)
        {
            std::size_t length = token & 7U;
            if (length == 0U &&
                !ReadExtendedLength(input, inputEnd, 7U, length))
            {
                return false;
            }
            if (inputEnd - input < 2)
            {
                return false;
            }
            const std::size_t encodedDistance =
                ((token & 8U) << 11U) +
                (input[0] >> 2U) +
                (static_cast<std::size_t>(input[1]) << 6U);
            input += 2;
            if (encodedDistance == 0U)
            {
                return output == outputEnd && input == inputEnd;
            }
            if (!CopyMatch(
                    output,
                    outputBegin,
                    outputEnd,
                    encodedDistance + 0x4000U,
                    length + 2U))
            {
                return false;
            }
        }
        else
        {
            if (input >= inputEnd)
            {
                return false;
            }
            const std::size_t distance =
                1U + (token >> 2U) +
                (static_cast<std::size_t>(*input++) << 2U);
            if (!CopyMatch(
                    output,
                    outputBegin,
                    outputEnd,
                    distance,
                    2U))
            {
                return false;
            }
        }

match_done:
        if (input - packed.data() < 2)
        {
            return false;
        }
        token = input[-2] & 3U;
        if (token == 0U)
        {
            continue;
        }

match_next:
        if (!CopyLiterals(
                input,
                inputEnd,
                output,
                outputEnd,
                token) ||
            input >= inputEnd)
        {
            return false;
        }
        token = *input++;
        goto match;
    }
    return false;
}

bool ExtractEntry(
    std::ifstream& stream,
    const ArchiveEntry& entry,
    std::vector<std::uint8_t>& contents)
{
    stream.seekg(entry.offset, std::ios::beg);
    if (entry.storedRaw)
    {
        if (entry.packedSize != entry.unpackedSize)
        {
            return false;
        }
        contents.resize(entry.unpackedSize);
        stream.read(
            reinterpret_cast<char*>(contents.data()),
            static_cast<std::streamsize>(contents.size()));
        return static_cast<std::size_t>(stream.gcount()) == contents.size();
    }
    std::uint32_t blockCount = 0;
    if (!ReadU32(stream, blockCount) || blockCount == 0U ||
        blockCount > 100000U)
    {
        return false;
    }
    std::vector<LzoBlock> blocks(blockCount);
    for (LzoBlock& block : blocks)
    {
        if (!ReadU32(stream, block.packedSize) ||
            !ReadU32(stream, block.unpackedSize) ||
            !ReadU32(stream, block.relativeOffset))
        {
            return false;
        }
    }
    const auto dataBase =
        static_cast<std::uint64_t>(entry.offset) + 4ULL +
        static_cast<std::uint64_t>(blockCount) * 12ULL;
    contents.clear();
    contents.reserve(entry.unpackedSize);
    for (const LzoBlock& block : blocks)
    {
        stream.seekg(
            static_cast<std::streamoff>(
                dataBase + block.relativeOffset),
            std::ios::beg);
        std::vector<std::uint8_t> packed(block.packedSize);
        stream.read(
            reinterpret_cast<char*>(packed.data()),
            static_cast<std::streamsize>(packed.size()));
        if (!stream)
        {
            return false;
        }
        std::vector<std::uint8_t> unpacked;
        if (block.packedSize == block.unpackedSize)
        {
            unpacked = std::move(packed);
        }
        else if (!DecompressLzo1x(
                     packed,
                     block.unpackedSize,
                     unpacked))
        {
            return false;
        }
        contents.insert(
            contents.end(),
            unpacked.begin(),
            unpacked.end());
    }
    return contents.size() == entry.unpackedSize;
}

std::uint16_t ReadU16(const std::uint8_t* bytes)
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1] << 8U);
}

std::uint32_t ReadU32(const std::uint8_t* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool PrintWaveSignature(const std::vector<std::uint8_t>& contents)
{
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    if (contents.size() < 12 ||
        std::string_view(
            reinterpret_cast<const char*>(contents.data()),
            4) != "RIFF" ||
        std::string_view(
            reinterpret_cast<const char*>(contents.data() + 8),
            4) != "WAVE")
    {
        return false;
    }

    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t samplesPerSecond = 0;
    std::span<const std::uint8_t> pcm = {};
    std::size_t offset = 12;
    while (offset + 8 <= contents.size())
    {
        const std::string_view id(
            reinterpret_cast<const char*>(contents.data() + offset),
            4);
        const std::uint32_t chunkBytes = ReadU32(contents.data() + offset + 4);
        const std::size_t dataOffset = offset + 8;
        if (chunkBytes > contents.size() - dataOffset)
        {
            return false;
        }
        if (id == "fmt " && chunkBytes >= 16)
        {
            channels = ReadU16(contents.data() + dataOffset + 2);
            samplesPerSecond = ReadU32(contents.data() + dataOffset + 4);
            bitsPerSample = ReadU16(contents.data() + dataOffset + 14);
        }
        else if (id == "data")
        {
            pcm = std::span<const std::uint8_t>(
                contents.data() + dataOffset,
                chunkBytes);
        }
        offset = dataOffset + chunkBytes + (chunkBytes & 1U);
    }
    if (pcm.empty() || channels == 0 || samplesPerSecond == 0 ||
        bitsPerSample == 0)
    {
        return false;
    }

    std::uint64_t hash = kFnvOffset;
    for (const std::uint8_t value : pcm)
    {
        hash ^= value;
        hash *= kFnvPrime;
    }
    std::printf(
        "fnv64=0x%016llX bytes=%llu channels=%u rate=%lu bits=%u\n",
        static_cast<unsigned long long>(hash),
        static_cast<unsigned long long>(pcm.size()),
        static_cast<unsigned int>(channels),
        static_cast<unsigned long>(samplesPerSecond),
        static_cast<unsigned int>(bitsPerSample));
    return true;
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    const bool printWaveSignature = argumentCount == 4 &&
        std::wstring_view(arguments[3]) == L"--wave-signature";
    if (argumentCount != 3 && !printWaveSignature)
    {
        std::fwprintf(
            stderr,
            L"Usage: BFVRRfaInspect <archive.rfa> <entry/path> [--wave-signature]\n");
        return 2;
    }
    std::ifstream archive(
        std::filesystem::path(arguments[1]),
        std::ios::binary);
    if (!archive)
    {
        std::fwprintf(stderr, L"Could not open archive: %ls\n", arguments[1]);
        return 2;
    }
    const std::filesystem::path requestedPath(arguments[2]);
    const std::string requested = requestedPath.generic_string();
    const auto entry = FindEntry(archive, requested);
    if (!entry.has_value())
    {
        std::fwprintf(
            stderr,
            L"Entry not found or archive index invalid: %ls\n",
            arguments[2]);
        return 1;
    }
    std::vector<std::uint8_t> contents;
    if (!ExtractEntry(archive, *entry, contents))
    {
        std::fwprintf(
            stderr,
            L"Could not decompress entry: %ls\n",
            arguments[2]);
        return 1;
    }
    if (printWaveSignature)
    {
        if (!PrintWaveSignature(contents))
        {
            std::fwprintf(
                stderr,
                L"Entry is not a supported PCM WAVE file: %ls\n",
                arguments[2]);
            return 1;
        }
        return 0;
    }
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
    {
        return 1;
    }
    return std::fwrite(
               contents.data(),
               1,
               contents.size(),
               stdout) == contents.size()
        ? 0
        : 1;
}
