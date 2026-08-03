#include "client/DirectSoundImportRoute.h"

#include <cstddef>
#include <cstring>

namespace bfvr
{
DirectSoundImportRouteResult RouteExecutableDirectSoundCreate8(
    DirectSoundCreate8Function replacement) noexcept
{
    DirectSoundImportRouteResult result = {};
    if (replacement == nullptr)
    {
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }

    auto* const imageBase =
        reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    if (imageBase == nullptr)
    {
        result.error = GetLastError();
        return result;
    }

    const auto* const dosHeader =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        result.error = ERROR_BAD_EXE_FORMAT;
        return result;
    }

    const auto* const ntHeaders =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(
            imageBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE ||
        ntHeaders->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        result.error = ERROR_BAD_EXE_FORMAT;
        return result;
    }

    const IMAGE_DATA_DIRECTORY importDirectory =
        ntHeaders->OptionalHeader
            .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDirectory.VirtualAddress == 0)
    {
        result.error = ERROR_PROC_NOT_FOUND;
        return result;
    }

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        imageBase + importDirectory.VirtualAddress);
    for (; descriptor->Name != 0; ++descriptor)
    {
        const auto* const moduleName =
            reinterpret_cast<const char*>(imageBase + descriptor->Name);
        if (_stricmp(moduleName, "dsound.dll") != 0 ||
            descriptor->OriginalFirstThunk == 0)
        {
            continue;
        }

        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            imageBase + descriptor->OriginalFirstThunk);
        auto* functions = reinterpret_cast<IMAGE_THUNK_DATA32*>(
            imageBase + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++functions)
        {
            constexpr WORD kDirectSoundCreate8Ordinal = 11;
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))
            {
                if (IMAGE_ORDINAL32(names->u1.Ordinal) !=
                    kDirectSoundCreate8Ordinal)
                {
                    continue;
                }
            }
            else
            {
                const auto* const importName =
                    reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                        imageBase + names->u1.AddressOfData);
                if (std::strcmp(
                        reinterpret_cast<const char*>(importName->Name),
                        "DirectSoundCreate8") != 0)
                {
                    continue;
                }
            }

            auto** const importAddress =
                reinterpret_cast<void**>(&functions->u1.Function);
            DWORD previousProtection = 0;
            if (!VirtualProtect(
                    importAddress,
                    sizeof(void*),
                    PAGE_READWRITE,
                    &previousProtection))
            {
                result.error = GetLastError();
                return result;
            }

            result.previous = reinterpret_cast<DirectSoundCreate8Function>(
                *importAddress);
            *importAddress = reinterpret_cast<void*>(replacement);
            FlushInstructionCache(
                GetCurrentProcess(),
                importAddress,
                sizeof(void*));

            DWORD ignoredProtection = 0;
            VirtualProtect(
                importAddress,
                sizeof(void*),
                previousProtection,
                &ignoredProtection);
            result.routed = true;
            return result;
        }
    }

    result.error = ERROR_PROC_NOT_FOUND;
    return result;
}
} // namespace bfvr
