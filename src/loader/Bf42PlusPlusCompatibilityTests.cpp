#include "Bf42PlusPlusCompatibility.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
template <typename T>
T& At(std::vector<std::byte>& image, std::size_t offset)
{
    return *reinterpret_cast<T*>(image.data() + offset);
}

void PutString(std::vector<std::byte>& image, std::size_t offset, const char* value)
{
    std::memcpy(image.data() + offset, value, std::strlen(value) + 1);
}

std::vector<std::byte> MakeProxyFixture()
{
    std::vector<std::byte> image(0x600);
    IMAGE_DOS_HEADER& dos = At<IMAGE_DOS_HEADER>(image, 0);
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;

    At<DWORD>(image, 0x80) = IMAGE_NT_SIGNATURE;
    IMAGE_FILE_HEADER& file = At<IMAGE_FILE_HEADER>(image, 0x84);
    file.Machine = IMAGE_FILE_MACHINE_I386;
    file.NumberOfSections = 1;
    file.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);

    constexpr std::size_t optionalOffset = 0x84 + sizeof(IMAGE_FILE_HEADER);
    IMAGE_OPTIONAL_HEADER32& optional =
        At<IMAGE_OPTIONAL_HEADER32>(image, optionalOffset);
    optional.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    optional.SizeOfHeaders = 0x200;
    optional.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = {0x1000, 0x180};

    IMAGE_SECTION_HEADER& section = At<IMAGE_SECTION_HEADER>(
        image,
        optionalOffset + sizeof(IMAGE_OPTIONAL_HEADER32));
    section.Misc.VirtualSize = 0x400;
    section.VirtualAddress = 0x1000;
    section.SizeOfRawData = 0x400;
    section.PointerToRawData = 0x200;

    IMAGE_EXPORT_DIRECTORY& exports =
        At<IMAGE_EXPORT_DIRECTORY>(image, 0x200);
    exports.Name = 0x1100;
    exports.Base = 11;
    exports.NumberOfFunctions = 1;
    exports.NumberOfNames = 1;
    exports.AddressOfFunctions = 0x1120;
    exports.AddressOfNames = 0x1130;
    exports.AddressOfNameOrdinals = 0x1140;

    PutString(image, 0x300, "bf42++.dll");
    At<DWORD>(image, 0x320) = 0x1180;
    At<DWORD>(image, 0x330) = 0x1160;
    At<WORD>(image, 0x340) = 0;
    PutString(image, 0x360, "DirectSoundCreate8");
    PutString(image, 0x400, "bf42++.ini");
    PutString(image, 0x420, "bf42++_debug.log");
    return image;
}

bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    const std::vector<std::byte> valid = MakeProxyFixture();
    bool passed = Expect(
        bfvr::IsBf42PlusPlusProxyImage(valid),
        "valid structural BF42++ proxy was rejected");

    std::vector<std::byte> wrongMachine = valid;
    At<IMAGE_FILE_HEADER>(wrongMachine, 0x84).Machine = IMAGE_FILE_MACHINE_AMD64;
    passed &= Expect(
        !bfvr::IsBf42PlusPlusProxyImage(wrongMachine),
        "non-x86 image was accepted");

    std::vector<std::byte> wrongModule = valid;
    PutString(wrongModule, 0x300, "dsound.dll");
    passed &= Expect(
        !bfvr::IsBf42PlusPlusProxyImage(wrongModule),
        "unrelated export module was accepted");

    std::vector<std::byte> wrongOrdinal = valid;
    At<WORD>(wrongOrdinal, 0x340) = 1;
    passed &= Expect(
        !bfvr::IsBf42PlusPlusProxyImage(wrongOrdinal),
        "wrong DirectSoundCreate8 ordinal was accepted");

    std::vector<std::byte> missingMarker = valid;
    PutString(missingMarker, 0x420, "unrelated.log");
    passed &= Expect(
        !bfvr::IsBf42PlusPlusProxyImage(missingMarker),
        "proxy without stable BF42++ markers was accepted");
    return passed ? 0 : 1;
}
