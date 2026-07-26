#include "presenter/D3DSystemRuntime.h"

#include <cwchar>
#include <iterator>

namespace bfvr
{
namespace
{
HMODULE LoadSystemLibrary(
    const wchar_t* fileName,
    DWORD& error) noexcept
{
    wchar_t path[MAX_PATH] = {};
    const UINT directoryLength = GetSystemDirectoryW(
        path,
        static_cast<UINT>(std::size(path)));
    if (directoryLength == 0)
    {
        error = GetLastError();
        return nullptr;
    }
    if (directoryLength >= std::size(path) - 1)
    {
        error = ERROR_INSUFFICIENT_BUFFER;
        return nullptr;
    }

    const std::size_t fileLength = std::wcslen(fileName);
    const bool needsSeparator =
        path[directoryLength - 1] != L'\\';
    const std::size_t required =
        directoryLength +
        (needsSeparator ? 1u : 0u) +
        fileLength +
        1u;
    if (required > std::size(path))
    {
        error = ERROR_INSUFFICIENT_BUFFER;
        return nullptr;
    }

    std::size_t offset = directoryLength;
    if (needsSeparator)
    {
        path[offset++] = L'\\';
    }
    std::wmemcpy(path + offset, fileName, fileLength + 1);

    HMODULE module = LoadLibraryExW(
        path,
        nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr)
    {
        error = GetLastError();
    }
    return module;
}

D3DSystemRuntime LoadRuntime() noexcept
{
    D3DSystemRuntime runtime = {};
    runtime.d3d11Module =
        LoadSystemLibrary(L"d3d11.dll", runtime.error);
    if (runtime.d3d11Module == nullptr)
    {
        return runtime;
    }

    runtime.dxgiModule =
        LoadSystemLibrary(L"dxgi.dll", runtime.error);
    if (runtime.dxgiModule == nullptr)
    {
        return runtime;
    }

    runtime.createD3D11Device =
        reinterpret_cast<D3DSystemRuntime::D3D11CreateDeviceFunction>(
            GetProcAddress(
                runtime.d3d11Module,
                "D3D11CreateDevice"));
    runtime.createDXGIFactory1 =
        reinterpret_cast<D3DSystemRuntime::CreateDXGIFactory1Function>(
            GetProcAddress(
                runtime.dxgiModule,
                "CreateDXGIFactory1"));
    if (runtime.createD3D11Device == nullptr ||
        runtime.createDXGIFactory1 == nullptr)
    {
        runtime.error = ERROR_PROC_NOT_FOUND;
        return runtime;
    }

    runtime.error = ERROR_SUCCESS;
    return runtime;
}
} // namespace

const D3DSystemRuntime& GetD3DSystemRuntime() noexcept
{
    static const D3DSystemRuntime runtime = LoadRuntime();
    return runtime;
}
} // namespace bfvr
