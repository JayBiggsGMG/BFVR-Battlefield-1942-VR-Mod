#include "Bf42PlusPlusCompatibility.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
template <typename T>
const T* At(std::span<const std::byte> image, std::size_t offset) noexcept
{
    if (offset > image.size() || sizeof(T) > image.size() - offset)
    {
        return nullptr;
    }
    return reinterpret_cast<const T*>(image.data() + offset);
}

bool AddWithin(std::size_t left, std::size_t right, std::size_t limit,
    std::size_t& result) noexcept
{
    if (right > limit || left > limit - right)
    {
        return false;
    }
    result = left + right;
    return true;
}

bool EqualsAsciiInsensitive(const char* left, const char* right) noexcept
{
    if (left == nullptr || right == nullptr)
    {
        return false;
    }
    while (*left != '\0' && *right != '\0')
    {
        const unsigned char a = static_cast<unsigned char>(*left++);
        const unsigned char b = static_cast<unsigned char>(*right++);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return *left == *right;
}

bool ContainsAsciiInsensitive(
    std::span<const std::byte> image,
    const char* marker) noexcept
{
    if (marker == nullptr)
    {
        return false;
    }
    const std::size_t markerLength = std::strlen(marker);
    if (markerLength == 0 || markerLength > image.size())
    {
        return false;
    }
    for (std::size_t offset = 0; offset <= image.size() - markerLength; ++offset)
    {
        bool match = true;
        for (std::size_t index = 0; index < markerLength; ++index)
        {
            const unsigned char candidate = static_cast<unsigned char>(image[offset + index]);
            const unsigned char expected = static_cast<unsigned char>(marker[index]);
            if (std::tolower(candidate) != std::tolower(expected))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

struct PeView
{
    std::span<const std::byte> image;
    const IMAGE_OPTIONAL_HEADER32* optional = nullptr;
    const IMAGE_SECTION_HEADER* sections = nullptr;
    WORD sectionCount = 0;

    bool RvaToOffset(DWORD rva, std::size_t byteCount,
        std::size_t& offset) const noexcept
    {
        if (rva < optional->SizeOfHeaders)
        {
            offset = rva;
            return offset <= image.size() && byteCount <= image.size() - offset;
        }
        for (WORD index = 0; index < sectionCount; ++index)
        {
            const IMAGE_SECTION_HEADER& section = sections[index];
            const DWORD span = std::max(
                section.Misc.VirtualSize,
                section.SizeOfRawData);
            if (rva < section.VirtualAddress ||
                rva - section.VirtualAddress >= span)
            {
                continue;
            }
            const std::size_t sectionOffset =
                static_cast<std::size_t>(rva - section.VirtualAddress);
            if (sectionOffset > section.SizeOfRawData ||
                byteCount > section.SizeOfRawData - sectionOffset)
            {
                return false;
            }
            offset = static_cast<std::size_t>(section.PointerToRawData) + sectionOffset;
            return offset <= image.size() && byteCount <= image.size() - offset;
        }
        return false;
    }

    const char* StringAtRva(DWORD rva) const noexcept
    {
        std::size_t offset = 0;
        if (!RvaToOffset(rva, 1, offset))
        {
            return nullptr;
        }
        const char* value = reinterpret_cast<const char*>(image.data() + offset);
        const std::size_t available = image.size() - offset;
        return std::memchr(value, '\0', available) != nullptr ? value : nullptr;
    }
};

bool ParsePe(std::span<const std::byte> image, PeView& pe) noexcept
{
    const IMAGE_DOS_HEADER* dos = At<IMAGE_DOS_HEADER>(image, 0);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
    {
        return false;
    }
    const std::size_t ntOffset = static_cast<std::size_t>(dos->e_lfanew);
    const DWORD* signature = At<DWORD>(image, ntOffset);
    const IMAGE_FILE_HEADER* file = At<IMAGE_FILE_HEADER>(image, ntOffset + sizeof(DWORD));
    if (signature == nullptr || *signature != IMAGE_NT_SIGNATURE || file == nullptr ||
        file->Machine != IMAGE_FILE_MACHINE_I386 || file->NumberOfSections == 0 ||
        file->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32))
    {
        return false;
    }
    const std::size_t optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    const IMAGE_OPTIONAL_HEADER32* optional = At<IMAGE_OPTIONAL_HEADER32>(image, optionalOffset);
    if (optional == nullptr || optional->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        optional->NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
    {
        return false;
    }
    std::size_t sectionOffset = 0;
    if (!AddWithin(optionalOffset, file->SizeOfOptionalHeader, image.size(), sectionOffset))
    {
        return false;
    }
    const IMAGE_SECTION_HEADER* sections = At<IMAGE_SECTION_HEADER>(image, sectionOffset);
    const std::size_t sectionBytes =
        static_cast<std::size_t>(file->NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (sections == nullptr || sectionBytes > image.size() - sectionOffset)
    {
        return false;
    }
    pe = {image, optional, sections, file->NumberOfSections};
    return true;
}
} // namespace

namespace bfvr
{
bool IsBf42PlusPlusProxyImage(std::span<const std::byte> image) noexcept
{
    PeView pe = {};
    if (!ParsePe(image, pe))
    {
        return false;
    }
    const IMAGE_DATA_DIRECTORY& exportData =
        pe.optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    std::size_t exportOffset = 0;
    if (exportData.VirtualAddress == 0 ||
        !pe.RvaToOffset(exportData.VirtualAddress,
            sizeof(IMAGE_EXPORT_DIRECTORY), exportOffset))
    {
        return false;
    }
    const IMAGE_EXPORT_DIRECTORY* exports =
        At<IMAGE_EXPORT_DIRECTORY>(image, exportOffset);
    if (exports == nullptr || exports->NumberOfNames == 0 ||
        exports->NumberOfNames > 4096 ||
        !EqualsAsciiInsensitive(pe.StringAtRva(exports->Name), "bf42++.dll"))
    {
        return false;
    }

    std::size_t namesOffset = 0;
    std::size_t ordinalsOffset = 0;
    const std::size_t namesBytes =
        static_cast<std::size_t>(exports->NumberOfNames) * sizeof(DWORD);
    const std::size_t ordinalsBytes =
        static_cast<std::size_t>(exports->NumberOfNames) * sizeof(WORD);
    if (!pe.RvaToOffset(exports->AddressOfNames, namesBytes, namesOffset) ||
        !pe.RvaToOffset(exports->AddressOfNameOrdinals, ordinalsBytes, ordinalsOffset))
    {
        return false;
    }
    const DWORD* names = At<DWORD>(image, namesOffset);
    const WORD* ordinals = At<WORD>(image, ordinalsOffset);
    bool directSoundCreate8Exported = false;
    for (DWORD index = 0; index < exports->NumberOfNames; ++index)
    {
        if (EqualsAsciiInsensitive(pe.StringAtRva(names[index]), "DirectSoundCreate8") &&
            static_cast<DWORD>(exports->Base) + ordinals[index] == 11)
        {
            directSoundCreate8Exported = true;
            break;
        }
    }
    return directSoundCreate8Exported &&
        ContainsAsciiInsensitive(image, "bf42++.ini") &&
        ContainsAsciiInsensitive(image, "bf42++_debug.log");
}

bool IsBf42PlusPlusProxyFile(const std::wstring& path) noexcept
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    const std::streamoff length = file.tellg();
    constexpr std::streamoff kMaximumInspectionBytes = 64 * 1024 * 1024;
    if (length <= 0 || length > kMaximumInspectionBytes)
    {
        return false;
    }
    std::vector<std::byte> image(static_cast<std::size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(image.data()), length))
    {
        return false;
    }
    return IsBf42PlusPlusProxyImage(image);
}
} // namespace bfvr
