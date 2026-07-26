#include "client/D3D8RuntimeRedirect.h"

#include <cwchar>
#include <iterator>

namespace bfvr
{
namespace
{
constexpr wchar_t kBundledTranslatorName[] = L"BFVRD3D8To9.dll";
}

D3D8RuntimeRedirectResult LoadBundledD3D8To9(
    HMODULE clientModule) noexcept
{
    D3D8RuntimeRedirectResult result = {};
    if (clientModule == nullptr)
    {
        result.error = ERROR_INVALID_HANDLE;
        return result;
    }

    const DWORD pathLength = GetModuleFileNameW(
        clientModule,
        result.path,
        static_cast<DWORD>(std::size(result.path)));
    if (pathLength == 0)
    {
        result.error = GetLastError();
        return result;
    }
    if (pathLength >= std::size(result.path) - 1)
    {
        result.error = ERROR_INSUFFICIENT_BUFFER;
        return result;
    }

    wchar_t* const separator = wcsrchr(result.path, L'\\');
    if (separator == nullptr)
    {
        result.error = ERROR_BAD_PATHNAME;
        return result;
    }

    const std::size_t prefixLength =
        static_cast<std::size_t>(separator - result.path) + 1;
    const std::size_t translatorLength = std::size(kBundledTranslatorName);
    if (prefixLength + translatorLength > std::size(result.path))
    {
        result.error = ERROR_INSUFFICIENT_BUFFER;
        return result;
    }
    std::wmemcpy(
        result.path + prefixLength,
        kBundledTranslatorName,
        translatorLength);

    result.module = LoadLibraryExW(
        result.path,
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (result.module == nullptr)
    {
        result.error = GetLastError();
        return result;
    }

    result.direct3DCreate8 =
        GetProcAddress(result.module, "Direct3DCreate8");
    if (result.direct3DCreate8 == nullptr)
    {
        result.error = GetLastError();
        if (result.error == ERROR_SUCCESS)
        {
            result.error = ERROR_PROC_NOT_FOUND;
        }
        FreeLibrary(result.module);
        result.module = nullptr;
        return result;
    }

    return result;
}
} // namespace bfvr
