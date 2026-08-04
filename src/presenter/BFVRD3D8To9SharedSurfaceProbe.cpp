#include "presenter/D3DSystemRuntime.h"
#include "presenter/D3D8To9CrossProcessProbe.h"

#include "bfvr_runtime_diagnostics.hpp"
#include "bfvr_shared_bridge.hpp"
#include "d3d8.hpp"

#include <d3d11.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

namespace
{
using Direct3DCreate8Fn = IDirect3D8*(WINAPI*)(UINT);

std::wstring ModuleDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path,
        static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path))
    {
        return {};
    }
    std::wstring directory(path, length);
    const std::size_t separator = directory.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : directory.substr(0, separator);
}

std::wstring Combine(
    const std::wstring& directory,
    const wchar_t* fileName)
{
    return directory.empty()
        ? std::wstring(fileName)
        : directory + L"\\" + fileName;
}

LRESULT CALLBACK ProbeWindowProcedure(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateProbeWindow(HINSTANCE instance)
{
    constexpr wchar_t kClassName[] =
        L"BFVRD3D8To9SharedSurfaceProbeWindow";
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = ProbeWindowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    if (RegisterClassW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return nullptr;
    }

    return CreateWindowExW(
        0,
        kClassName,
        L"BFVR shared surface probe",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        320,
        240,
        nullptr,
        nullptr,
        instance,
        nullptr);
}

template <typename T>
void ReleaseInterface(T*& interfacePointer)
{
    if (interfacePointer != nullptr)
    {
        interfacePointer->Release();
        interfacePointer = nullptr;
    }
}

int Fail(const wchar_t* operation, HRESULT result)
{
    fwprintf(
        stderr,
        L"[FAIL] %ls returned 0x%08lX.\n",
        operation,
        static_cast<unsigned long>(result));
    return 1;
}

void PrintD3D9SharedTextureDiagnostics(IDirect3DDevice8* device8)
{
    IDirect3DDevice9* device9 = nullptr;
    HRESULT result = device8->QueryInterface(
        __uuidof(IDirect3DDevice9),
        reinterpret_cast<void**>(&device9));
    if (FAILED(result) || device9 == nullptr)
    {
        fwprintf(
            stderr,
            L"[INFO] IDirect3DDevice9 query returned 0x%08lX.\n",
            static_cast<unsigned long>(result));
        return;
    }

    IDirect3DDevice9Ex* device9Ex = nullptr;
    const HRESULT exResult = device9->QueryInterface(
        __uuidof(IDirect3DDevice9Ex),
        reinterpret_cast<void**>(&device9Ex));
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    D3DDISPLAYMODE displayMode = {};
    IDirect3D9* direct3D9 = nullptr;
    const HRESULT creationResult =
        device9->GetCreationParameters(&creation);
    const HRESULT direct3DResult =
        device9->GetDirect3D(&direct3D9);
    const HRESULT displayResult =
        direct3D9 == nullptr
        ? E_FAIL
        : direct3D9->GetAdapterDisplayMode(
            creation.AdapterOrdinal,
            &displayMode);
    fwprintf(
        stderr,
        L"[INFO] D3D9 diagnostics: device9Ex=0x%08lX "
        L"creation=0x%08lX adapter=%u type=%u "
        L"getD3D=0x%08lX display=0x%08lX displayFormat=%u.\n",
        static_cast<unsigned long>(exResult),
        static_cast<unsigned long>(creationResult),
        creation.AdapterOrdinal,
        static_cast<unsigned int>(creation.DeviceType),
        static_cast<unsigned long>(direct3DResult),
        static_cast<unsigned long>(displayResult),
        static_cast<unsigned int>(displayMode.Format));

    constexpr std::array<D3DFORMAT, 4> kFormats = {
        D3DFMT_A8B8G8R8,
        D3DFMT_A8R8G8B8,
        D3DFMT_A2B10G10R10,
        D3DFMT_A16B16G16R16F};
    for (const D3DFORMAT format : kFormats)
    {
        const HRESULT formatResult =
            direct3D9 == nullptr
            ? E_FAIL
            : direct3D9->CheckDeviceFormat(
                creation.AdapterOrdinal,
                creation.DeviceType,
                displayMode.Format,
                D3DUSAGE_RENDERTARGET,
                D3DRTYPE_TEXTURE,
                format);
        IDirect3DTexture9* ordinaryTexture = nullptr;
        const HRESULT ordinaryResult = device9->CreateTexture(
            64,
            64,
            1,
            D3DUSAGE_RENDERTARGET,
            format,
            D3DPOOL_DEFAULT,
            &ordinaryTexture,
            nullptr);
        HANDLE sharedHandle = nullptr;
        IDirect3DTexture9* sharedTexture = nullptr;
        const HRESULT sharedResult = device9->CreateTexture(
            64,
            64,
            1,
            D3DUSAGE_RENDERTARGET,
            format,
            D3DPOOL_DEFAULT,
            &sharedTexture,
            &sharedHandle);
        fwprintf(
            stderr,
            L"[INFO] D3D9 format=%u checkRT=0x%08lX "
            L"ordinary=0x%08lX shared=0x%08lX handle=%p.\n",
            static_cast<unsigned int>(format),
            static_cast<unsigned long>(formatResult),
            static_cast<unsigned long>(ordinaryResult),
            static_cast<unsigned long>(sharedResult),
            sharedHandle);
        ReleaseInterface(sharedTexture);
        ReleaseInterface(ordinaryTexture);
    }

    ReleaseInterface(direct3D9);
    ReleaseInterface(device9Ex);
    ReleaseInterface(device9);
}

bool VerifyManagedPoolCompatibility(IDirect3DDevice8* device8)
{
    IDirect3DTexture8* texture = nullptr;
    IDirect3DSurface8* textureSurface = nullptr;
    IDirect3DVertexBuffer8* vertexBuffer = nullptr;
    IDirect3DIndexBuffer8* indexBuffer = nullptr;
    D3DLOCKED_RECT lockedTexture = {};
    D3DSURFACE_DESC8 textureDescription = {};
    D3DSURFACE_DESC8 surfaceDescription = {};
    D3DVERTEXBUFFER_DESC vertexDescription = {};
    D3DINDEXBUFFER_DESC indexDescription = {};
    BYTE* bufferBytes = nullptr;

    HRESULT result = device8->CreateTexture(
        4,
        4,
        1,
        0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &texture);
    if (FAILED(result) || texture == nullptr)
    {
        Fail(L"IDirect3DDevice8::CreateTexture(MANAGED)", result);
        goto failure;
    }
    result = texture->LockRect(0, &lockedTexture, nullptr, 0);
    if (FAILED(result) || lockedTexture.pBits == nullptr)
    {
        Fail(L"IDirect3DTexture8::LockRect(MANAGED)", result);
        goto failure;
    }
    std::memset(lockedTexture.pBits, 0x5A, 4 * sizeof(DWORD));
    result = texture->UnlockRect(0);
    if (FAILED(result))
    {
        Fail(L"IDirect3DTexture8::UnlockRect(MANAGED)", result);
        goto failure;
    }
    result = texture->GetLevelDesc(0, &textureDescription);
    if (FAILED(result) ||
        textureDescription.Pool != D3DPOOL_MANAGED ||
        textureDescription.Usage != 0)
    {
        Fail(L"IDirect3DTexture8::GetLevelDesc(MANAGED)", result);
        goto failure;
    }
    result = texture->GetSurfaceLevel(0, &textureSurface);
    if (FAILED(result) || textureSurface == nullptr)
    {
        Fail(L"IDirect3DTexture8::GetSurfaceLevel(MANAGED)", result);
        goto failure;
    }
    result = textureSurface->GetDesc(&surfaceDescription);
    if (FAILED(result) ||
        surfaceDescription.Pool != D3DPOOL_MANAGED ||
        surfaceDescription.Usage != 0)
    {
        Fail(L"IDirect3DSurface8::GetDesc(MANAGED)", result);
        goto failure;
    }

    result = device8->CreateVertexBuffer(
        256,
        D3DUSAGE_WRITEONLY,
        D3DFVF_XYZ,
        D3DPOOL_MANAGED,
        &vertexBuffer);
    if (FAILED(result) || vertexBuffer == nullptr)
    {
        Fail(L"IDirect3DDevice8::CreateVertexBuffer(MANAGED)", result);
        goto failure;
    }
    result = vertexBuffer->Lock(0, 0, &bufferBytes, 0);
    if (FAILED(result) || bufferBytes == nullptr)
    {
        Fail(L"IDirect3DVertexBuffer8::Lock(MANAGED)", result);
        goto failure;
    }
    std::memset(bufferBytes, 0x3C, 256);
    result = vertexBuffer->Unlock();
    if (FAILED(result))
    {
        Fail(L"IDirect3DVertexBuffer8::Unlock(MANAGED)", result);
        goto failure;
    }
    result = vertexBuffer->GetDesc(&vertexDescription);
    if (FAILED(result) ||
        vertexDescription.Pool != D3DPOOL_MANAGED ||
        vertexDescription.Usage != D3DUSAGE_WRITEONLY)
    {
        Fail(L"IDirect3DVertexBuffer8::GetDesc(MANAGED)", result);
        goto failure;
    }

    result = device8->CreateIndexBuffer(
        256,
        D3DUSAGE_WRITEONLY,
        D3DFMT_INDEX16,
        D3DPOOL_MANAGED,
        &indexBuffer);
    if (FAILED(result) || indexBuffer == nullptr)
    {
        Fail(L"IDirect3DDevice8::CreateIndexBuffer(MANAGED)", result);
        goto failure;
    }
    bufferBytes = nullptr;
    result = indexBuffer->Lock(0, 0, &bufferBytes, 0);
    if (FAILED(result) || bufferBytes == nullptr)
    {
        Fail(L"IDirect3DIndexBuffer8::Lock(MANAGED)", result);
        goto failure;
    }
    std::memset(bufferBytes, 0x1E, 256);
    result = indexBuffer->Unlock();
    if (FAILED(result))
    {
        Fail(L"IDirect3DIndexBuffer8::Unlock(MANAGED)", result);
        goto failure;
    }
    result = indexBuffer->GetDesc(&indexDescription);
    if (FAILED(result) ||
        indexDescription.Pool != D3DPOOL_MANAGED ||
        indexDescription.Usage != D3DUSAGE_WRITEONLY)
    {
        Fail(L"IDirect3DIndexBuffer8::GetDesc(MANAGED)", result);
        goto failure;
    }

    ReleaseInterface(indexBuffer);
    ReleaseInterface(vertexBuffer);
    ReleaseInterface(textureSurface);
    ReleaseInterface(texture);
    return true;

failure:
    ReleaseInterface(indexBuffer);
    ReleaseInterface(vertexBuffer);
    ReleaseInterface(textureSurface);
    ReleaseInterface(texture);
    return false;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    constexpr D3DCOLOR kClearColor = 0xFF123456u;
    constexpr D3DFORMAT kSharedFormat =
        D3DFMT_A2B10G10R10;
    const wchar_t* consumerPath = nullptr;
    const wchar_t* consumerLogPath = nullptr;
    bool enableAmbientOcclusion = false;
    bool enableScreenSpaceGlobalIllumination = false;
    bool enableWaterReflections = false;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--consumer") == 0 && index + 1 < argc)
        {
            consumerPath = argv[++index];
        }
        else if (wcscmp(argv[index], L"--consumer-log") == 0 &&
            index + 1 < argc)
        {
            consumerLogPath = argv[++index];
        }
        else if (wcscmp(argv[index], L"--ambient-occlusion") == 0)
        {
            enableAmbientOcclusion = true;
        }
        else if (wcscmp(argv[index], L"--ssgi") == 0)
        {
            enableScreenSpaceGlobalIllumination = true;
        }
        else if (wcscmp(argv[index], L"--water-reflections") == 0)
        {
            enableWaterReflections = true;
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVRD3D8To9SharedSurfaceProbe [--consumer <x64-path> [--consumer-log <path>] [--ambient-occlusion] [--ssgi] [--water-reflections]]\n");
            return 2;
        }
    }

    const std::wstring translatorPath =
        Combine(ModuleDirectory(), L"BFVRD3D8To9.dll");
    HMODULE translator = LoadLibraryExW(
        translatorPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (translator == nullptr)
    {
        fwprintf(
            stderr,
            L"[FAIL] Unable to load %ls (error=%lu).\n",
            translatorPath.c_str(),
            GetLastError());
        return 1;
    }

    const auto createDirect3D =
        reinterpret_cast<Direct3DCreate8Fn>(
            GetProcAddress(translator, "Direct3DCreate8"));
    const auto getBridgeVersion =
        reinterpret_cast<BFVRD3D8To9GetSharedBridgeVersionFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetSharedBridgeVersion"));
    const auto getRuntimeDiagnostics =
        reinterpret_cast<BFVRD3D8To9GetRuntimeDiagnosticsFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetRuntimeDiagnostics"));
    const auto createSharedTarget =
        reinterpret_cast<BFVRD3D8To9CreateSharedRenderTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateSharedRenderTarget"));
    const auto createDepthTarget =
        reinterpret_cast<BFVRD3D8To9CreateTextureBackedDepthStencilFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateTextureBackedDepthStencil"));
    const auto resolveDepthTarget =
        reinterpret_cast<BFVRD3D8To9ResolveDepthToSharedTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9ResolveDepthToSharedTarget"));
    const auto waitForGpu =
        reinterpret_cast<BFVRD3D8To9WaitForGpuFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9WaitForGpu"));
    if (createDirect3D == nullptr ||
        getRuntimeDiagnostics == nullptr ||
        getBridgeVersion == nullptr ||
        createSharedTarget == nullptr ||
        ((enableAmbientOcclusion || enableScreenSpaceGlobalIllumination ||
             enableWaterReflections) &&
            (createDepthTarget == nullptr || resolveDepthTarget == nullptr)) ||
        waitForGpu == nullptr ||
        getBridgeVersion() != BFVR_D3D8TO9_SHARED_BRIDGE_VERSION)
    {
        fwprintf(stderr, L"[FAIL] Translator shared-bridge ABI is unavailable.\n");
        FreeLibrary(translator);
        return 1;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND window = CreateProbeWindow(instance);
    if (window == nullptr)
    {
        fwprintf(stderr, L"[FAIL] Probe window creation failed (%lu).\n", GetLastError());
        FreeLibrary(translator);
        return 1;
    }

    IDirect3D8* direct3D = createDirect3D(D3D_SDK_VERSION);
    IDirect3DDevice8* device8 = nullptr;
    IDirect3DSurface8* sharedSurface8 = nullptr;
    IDirect3DSurface8* priorColor8 = nullptr;
    IDirect3DSurface8* priorDepth8 = nullptr;
    ID3D11Device* device11 = nullptr;
    ID3D11DeviceContext* context11 = nullptr;
    ID3D11Texture2D* sharedTexture11 = nullptr;
    ID3D11Texture2D* stagingTexture11 = nullptr;
    HANDLE sharedHandle = nullptr;
    D3DPRESENT_PARAMETERS8 presentation = {};
    D3DSURFACE_DESC8 surfaceDescription = {};
    D3DVIEWPORT8 priorViewport = {};
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    D3D11_TEXTURE2D_DESC sharedDescription = {};
    D3D11_TEXTURE2D_DESC stagingDescription = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const bfvr::D3DSystemRuntime* runtime = nullptr;
    const unsigned char* pixel = nullptr;
    std::array<unsigned char, 4> observed = {};
    bool pixelMatches = false;
    DWORD packedPixel = 0;
    UINT red10 = 0;
    UINT green10 = 0;
    UINT blue10 = 0;
    UINT alpha2 = 0;
    HRESULT result = E_FAIL;
    HRESULT restoreResult = E_FAIL;
    HRESULT viewportRestoreResult = E_FAIL;
    int exitCode = 1;

    if (direct3D == nullptr)
    {
        fwprintf(stderr, L"[FAIL] Direct3DCreate8 returned null.\n");
        goto cleanup;
    }

    presentation.BackBufferWidth = 320;
    presentation.BackBufferHeight = 240;
    presentation.BackBufferFormat = D3DFMT_UNKNOWN;
    presentation.BackBufferCount = 1;
    presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentation.hDeviceWindow = window;
    presentation.Windowed = TRUE;
    result = direct3D->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING |
            D3DCREATE_PUREDEVICE |
            D3DCREATE_FPU_PRESERVE,
        &presentation,
        &device8);
    if (FAILED(result) || device8 == nullptr)
    {
        exitCode = Fail(L"IDirect3D8::CreateDevice", result);
        goto cleanup;
    }
    if (!VerifyManagedPoolCompatibility(device8))
    {
        goto cleanup;
    }
    {
        BFVRD3D8To9RuntimeDiagnostics diagnostics = {};
        diagnostics.size = sizeof(diagnostics);
        result = getRuntimeDiagnostics(&diagnostics);
        if (FAILED(result) ||
            diagnostics.version !=
                BFVR_D3D8TO9_RUNTIME_DIAGNOSTICS_VERSION ||
            diagnostics.managedTextureTranslations != 1 ||
            diagnostics.managedVertexBufferTranslations != 1 ||
            diagnostics.managedIndexBufferTranslations != 1 ||
            diagnostics.managedTranslationFailures != 0)
        {
            exitCode = Fail(
                L"BFVRD3D8To9GetRuntimeDiagnostics(managed)",
                result);
            goto cleanup;
        }
    }

    result = createSharedTarget(
        device8,
        kWidth,
        kHeight,
        kSharedFormat,
        &sharedHandle,
        reinterpret_cast<void**>(&sharedSurface8));
    if (FAILED(result) || sharedSurface8 == nullptr || sharedHandle == nullptr)
    {
        exitCode = Fail(L"BFVRD3D8To9CreateSharedRenderTarget", result);
        PrintD3D9SharedTextureDiagnostics(device8);
        goto cleanup;
    }

    result = sharedSurface8->GetDesc(&surfaceDescription);
    if (FAILED(result) ||
        surfaceDescription.Width != kWidth ||
        surfaceDescription.Height != kHeight ||
        surfaceDescription.Format != kSharedFormat)
    {
        exitCode = Fail(L"IDirect3DSurface8::GetDesc", result);
        goto cleanup;
    }

    result = device8->GetRenderTarget(&priorColor8);
    if (SUCCEEDED(result))
    {
        const HRESULT depthResult =
            device8->GetDepthStencilSurface(&priorDepth8);
        if (FAILED(depthResult) && priorDepth8 != nullptr)
        {
            result = depthResult;
        }
    }
    result = SUCCEEDED(result)
        ? device8->GetViewport(&priorViewport)
        : result;
    result = SUCCEEDED(result)
        ? device8->SetRenderTarget(sharedSurface8, nullptr)
        : result;
    result = SUCCEEDED(result)
        ? device8->Clear(
            0,
            nullptr,
            D3DCLEAR_TARGET,
            kClearColor,
            1.0f,
            0)
        : result;
    restoreResult =
        priorColor8 == nullptr
        ? E_FAIL
        : device8->SetRenderTarget(priorColor8, priorDepth8);
    viewportRestoreResult =
        device8->SetViewport(&priorViewport);
    if (FAILED(result) ||
        FAILED(restoreResult) ||
        FAILED(viewportRestoreResult))
    {
        exitCode = Fail(
            L"D3D8 shared-target clear/restoration",
            FAILED(result)
                ? result
                : FAILED(restoreResult)
                ? restoreResult
                : viewportRestoreResult);
        goto cleanup;
    }

    result = waitForGpu(device8, 5000);
    if (FAILED(result))
    {
        exitCode = Fail(L"BFVRD3D8To9WaitForGpu", result);
        goto cleanup;
    }

    runtime = &bfvr::GetD3DSystemRuntime();
    if (!runtime->IsAvailable())
    {
        fwprintf(
            stderr,
            L"[FAIL] System D3D11/DXGI runtime resolution failed (%lu).\n",
            runtime->error);
        goto cleanup;
    }

    result = runtime->createD3D11Device(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device11,
        &featureLevel,
        &context11);
    if (FAILED(result))
    {
        exitCode = Fail(L"D3D11CreateDevice", result);
        goto cleanup;
    }

    result = device11->OpenSharedResource(
        sharedHandle,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&sharedTexture11));
    if (FAILED(result) || sharedTexture11 == nullptr)
    {
        exitCode = Fail(L"ID3D11Device::OpenSharedResource", result);
        goto cleanup;
    }

    sharedTexture11->GetDesc(&sharedDescription);
    if (sharedDescription.Width != kWidth ||
        sharedDescription.Height != kHeight ||
        sharedDescription.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
    {
        fwprintf(
            stderr,
            L"[FAIL] Unexpected D3D11 shared texture: %ux%u format=%u.\n",
            sharedDescription.Width,
            sharedDescription.Height,
            static_cast<unsigned int>(sharedDescription.Format));
        goto cleanup;
    }

    stagingDescription = sharedDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;
    result = device11->CreateTexture2D(
        &stagingDescription,
        nullptr,
        &stagingTexture11);
    if (FAILED(result))
    {
        exitCode = Fail(L"ID3D11Device::CreateTexture2D(staging)", result);
        goto cleanup;
    }

    context11->CopyResource(stagingTexture11, sharedTexture11);
    context11->Flush();
    result = context11->Map(
        stagingTexture11,
        0,
        D3D11_MAP_READ,
        0,
        &mapped);
    if (FAILED(result))
    {
        exitCode = Fail(L"ID3D11DeviceContext::Map", result);
        goto cleanup;
    }

    pixel = static_cast<const unsigned char*>(mapped.pData);
    packedPixel = *reinterpret_cast<const DWORD*>(pixel);
    red10 = packedPixel & 0x3FFu;
    green10 = (packedPixel >> 10) & 0x3FFu;
    blue10 = (packedPixel >> 20) & 0x3FFu;
    alpha2 = (packedPixel >> 30) & 0x3u;
    observed = {
        static_cast<unsigned char>((red10 * 255u + 511u) / 1023u),
        static_cast<unsigned char>((green10 * 255u + 511u) / 1023u),
        static_cast<unsigned char>((blue10 * 255u + 511u) / 1023u),
        static_cast<unsigned char>((alpha2 * 255u + 1u) / 3u)};
    pixelMatches =
        observed[0] == 0x12 &&
        observed[1] == 0x34 &&
        observed[2] == 0x56 &&
        observed[3] == 0xFF;
    context11->Unmap(stagingTexture11, 0);
    if (!pixelMatches)
    {
        fwprintf(
            stderr,
            L"[FAIL] Shared pixel mismatch: rgba=%02X%02X%02X%02X.\n",
            observed[0],
            observed[1],
            observed[2],
            observed[3]);
        goto cleanup;
    }

    wprintf(
        L"[PASS] D3D8->D3D9->D3D11 shared surface verified: "
        L"%ux%u D3DFMT_A2B10G10R10/DXGI_FORMAT_R10G10B10A2_UNORM, "
        L"rgba=%02X%02X%02X%02X, featureLevel=0x%04X.\n",
        kWidth,
        kHeight,
        observed[0],
        observed[1],
        observed[2],
        observed[3],
        static_cast<unsigned int>(featureLevel));
    exitCode =
        consumerPath == nullptr ||
        bfvr::shared::RunD3D8To9CrossProcessProbe(
            device8,
            createSharedTarget,
            createDepthTarget,
            resolveDepthTarget,
            waitForGpu,
            consumerPath,
            consumerLogPath,
            enableAmbientOcclusion,
            enableScreenSpaceGlobalIllumination,
            enableWaterReflections)
        ? 0
        : 1;

cleanup:
    ReleaseInterface(stagingTexture11);
    ReleaseInterface(sharedTexture11);
    ReleaseInterface(context11);
    ReleaseInterface(device11);
    ReleaseInterface(priorDepth8);
    ReleaseInterface(priorColor8);
    ReleaseInterface(sharedSurface8);
    ReleaseInterface(device8);
    ReleaseInterface(direct3D);
    DestroyWindow(window);
    FreeLibrary(translator);
    return exitCode;
}
