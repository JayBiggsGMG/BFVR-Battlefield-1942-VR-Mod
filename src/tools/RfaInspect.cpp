#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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

bool PrintDdsSignature(const std::vector<std::uint8_t>& contents)
{
    if (contents.size() < 128 ||
        std::string_view(
            reinterpret_cast<const char*>(contents.data()),
            4) != "DDS " ||
        ReadU32(contents.data() + 4) != 124U)
    {
        return false;
    }

    const std::uint32_t height = ReadU32(contents.data() + 12);
    const std::uint32_t width = ReadU32(contents.data() + 16);
    const std::uint32_t mipCount = (std::max)(
        ReadU32(contents.data() + 28),
        1U);
    const std::uint32_t pixelFlags = ReadU32(contents.data() + 80);
    const std::uint32_t fourCc = ReadU32(contents.data() + 84);
    const std::uint32_t bitsPerPixel = ReadU32(contents.data() + 88);
    const std::uint32_t redMask = ReadU32(contents.data() + 92);
    const std::uint32_t greenMask = ReadU32(contents.data() + 96);
    const std::uint32_t blueMask = ReadU32(contents.data() + 100);
    const std::uint32_t alphaMask = ReadU32(contents.data() + 104);
    const char fourCcText[5] = {
        static_cast<char>(fourCc & 0xFFU),
        static_cast<char>((fourCc >> 8U) & 0xFFU),
        static_cast<char>((fourCc >> 16U) & 0xFFU),
        static_cast<char>((fourCc >> 24U) & 0xFFU),
        '\0'};

    std::printf(
        "dds width=%lu height=%lu mips=%lu pixelFlags=0x%08lX fourCC=%s bpp=%lu masks=%08lX/%08lX/%08lX/%08lX bytes=%llu\n",
        static_cast<unsigned long>(width),
        static_cast<unsigned long>(height),
        static_cast<unsigned long>(mipCount),
        static_cast<unsigned long>(pixelFlags),
        fourCcText,
        static_cast<unsigned long>(bitsPerPixel),
        static_cast<unsigned long>(redMask),
        static_cast<unsigned long>(greenMask),
        static_cast<unsigned long>(blueMask),
        static_cast<unsigned long>(alphaMask),
        static_cast<unsigned long long>(contents.size()));

    if (fourCc == 0U && bitsPerPixel == 16U &&
        redMask != 0U && greenMask != 0U && blueMask != 0U)
    {
        const auto decodeChannel = [](std::uint16_t pixel, std::uint32_t mask)
        {
            unsigned int shift = 0;
            while (((mask >> shift) & 1U) == 0U)
            {
                ++shift;
            }
            const std::uint32_t maximum = mask >> shift;
            return static_cast<double>((pixel & mask) >> shift) /
                static_cast<double>(maximum);
        };
        std::size_t offset = 128;
        std::uint32_t mipWidth = width;
        std::uint32_t mipHeight = height;
        for (std::uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const std::size_t pixelCount =
                static_cast<std::size_t>(mipWidth) * mipHeight;
            const std::size_t byteCount = pixelCount * 2U;
            if (byteCount > contents.size() - offset)
            {
                return false;
            }
            double sums[3] = {};
            double squareSums[3] = {};
            for (std::size_t pixelIndex = 0;
                 pixelIndex < pixelCount;
                 ++pixelIndex)
            {
                const std::uint16_t pixel = static_cast<std::uint16_t>(
                    contents[offset + pixelIndex * 2U] |
                    (contents[offset + pixelIndex * 2U + 1U] << 8U));
                const double channels[3] = {
                    decodeChannel(pixel, redMask),
                    decodeChannel(pixel, greenMask),
                    decodeChannel(pixel, blueMask)};
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    sums[channel] += channels[channel];
                    squareSums[channel] += channels[channel] * channels[channel];
                }
            }
            std::printf(
                "mip=%lu size=%lux%lu mean=%.6f/%.6f/%.6f sd=%.6f/%.6f/%.6f\n",
                static_cast<unsigned long>(mip),
                static_cast<unsigned long>(mipWidth),
                static_cast<unsigned long>(mipHeight),
                sums[0] / pixelCount,
                sums[1] / pixelCount,
                sums[2] / pixelCount,
                std::sqrt((std::max)(
                    squareSums[0] / pixelCount -
                        (sums[0] / pixelCount) * (sums[0] / pixelCount),
                    0.0)),
                std::sqrt((std::max)(
                    squareSums[1] / pixelCount -
                        (sums[1] / pixelCount) * (sums[1] / pixelCount),
                    0.0)),
                std::sqrt((std::max)(
                    squareSums[2] / pixelCount -
                        (sums[2] / pixelCount) * (sums[2] / pixelCount),
                    0.0)));
            offset += byteCount;
            mipWidth = (std::max)(mipWidth / 2U, 1U);
            mipHeight = (std::max)(mipHeight / 2U, 1U);
        }
    }
    return width != 0 && height != 0;
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    const bool printWaveSignature = argumentCount == 4 &&
        std::wstring_view(arguments[3]) == L"--wave-signature";
    const bool printDdsSignature = argumentCount == 4 &&
        std::wstring_view(arguments[3]) == L"--dds-signature";
    if (argumentCount != 3 && !printWaveSignature && !printDdsSignature)
    {
        std::fwprintf(
            stderr,
            L"Usage: BFVRRfaInspect <archive.rfa> <entry/path> [--wave-signature|--dds-signature]\n");
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
    if (printDdsSignature)
    {
        if (!PrintDdsSignature(contents))
        {
            std::fwprintf(
                stderr,
                L"Entry is not a supported DDS file: %ls\n",
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
