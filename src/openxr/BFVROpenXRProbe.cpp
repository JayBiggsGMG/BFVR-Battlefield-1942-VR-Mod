#include "openxr/OpenXRBootstrap.h"
#include "openxr/OpenXRPresentation.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
FILE* g_output = stdout;

void WriteConsole(void*, const wchar_t* message)
{
    fwprintf(g_output, L"[INFO] %s\n", message);
    fflush(g_output);
}

bool GetExecutableDirectory(wchar_t* directory, std::size_t directorySize)
{
    if (GetModuleFileNameW(nullptr, directory, static_cast<DWORD>(directorySize)) == 0)
    {
        return false;
    }

    wchar_t* separator = wcsrchr(directory, L'\\');
    if (separator == nullptr)
    {
        return false;
    }
    *separator = L'\0';
    return true;
}

struct TestTexture
{
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;

    void Release()
    {
        if (renderTargetView != nullptr)
        {
            renderTargetView->Release();
            renderTargetView = nullptr;
        }
        if (texture != nullptr)
        {
            texture->Release();
            texture = nullptr;
        }
    }
};

class ScopedComInitialization
{
public:
    ScopedComInitialization()
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , shouldUninitialize_(SUCCEEDED(result_))
    {
    }

    ~ScopedComInitialization()
    {
        if (shouldUninitialize_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] HRESULT Result() const noexcept
    {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
    bool shouldUninitialize_ = false;
};

bool CreateTestTexture(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    const float color[4],
    TestTexture& output)
{
    if (device == nullptr || context == nullptr || width == 0 || height == 0 ||
        format == DXGI_FORMAT_UNKNOWN)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    const HRESULT textureResult = device->CreateTexture2D(
        &description,
        nullptr,
        &output.texture);
    const HRESULT viewResult = SUCCEEDED(textureResult)
        ? device->CreateRenderTargetView(output.texture, nullptr, &output.renderTargetView)
        : E_FAIL;
    if (FAILED(textureResult) || FAILED(viewResult) || output.texture == nullptr ||
        output.renderTargetView == nullptr)
    {
        output.Release();
        return false;
    }
    context->ClearRenderTargetView(output.renderTargetView, color);
    return true;
}

bool RunPresentationProbe(
    const wchar_t* payloadDirectory,
    bool useCylinderUi,
    bool omitGraphicsBinding,
    bool directD3D11Instance,
    DWORD durationMs)
{
    const ScopedComInitialization comInitialization;
    if (FAILED(comInitialization.Result()) &&
        comInitialization.Result() != RPC_E_CHANGED_MODE)
    {
        wprintf(
            L"[FAIL] Could not initialize COM for the standalone presentation probe (HRESULT=0x%08lX).\n",
            static_cast<unsigned long>(comInitialization.Result()));
        return false;
    }
    fwprintf(
        g_output,
        L"[INFO] Standalone presentation probe COM initialization result=0x%08lX.\n",
        static_cast<unsigned long>(comInitialization.Result()));
    fflush(g_output);

    bfvr::OpenXRPresentationConfiguration configuration = {};
    configuration.uiLayerMode = useCylinderUi
        ? bfvr::OpenXRUiLayerMode::Cylinder
        : bfvr::OpenXRUiLayerMode::Quad;
    configuration.diagnosticOmitGraphicsBinding = omitGraphicsBinding;
    configuration.diagnosticDirectD3D11Instance = directD3D11Instance;
    bfvr::OpenXRPresentation presentation;
    if (!presentation.Initialize(payloadDirectory, configuration, WriteConsole, nullptr))
    {
        wprintf(L"[INFO] OpenXR presentation initialization was unavailable; flat fallback remains active.\n");
        return true;
    }

    const bfvr::OpenXRPresentationTextureRequirements requirements =
        presentation.GetTextureRequirements();
    TestTexture leftWorld;
    TestTexture rightWorld;
    TestTexture ref2Ui;
    constexpr float kLeftColor[4] = {0.95F, 0.08F, 0.08F, 1.0F};
    constexpr float kRightColor[4] = {0.08F, 0.80F, 0.18F, 1.0F};
    constexpr float kUiColor[4] = {0.08F, 0.20F, 0.95F, 0.85F};
    const bool texturesReady =
        CreateTestTexture(
            presentation.GetD3D11Device(),
            presentation.GetD3D11Context(),
            requirements.leftWorldWidth,
            requirements.leftWorldHeight,
            requirements.format,
            kLeftColor,
            leftWorld) &&
        CreateTestTexture(
            presentation.GetD3D11Device(),
            presentation.GetD3D11Context(),
            requirements.rightWorldWidth,
            requirements.rightWorldHeight,
            requirements.format,
            kRightColor,
            rightWorld) &&
        CreateTestTexture(
            presentation.GetD3D11Device(),
            presentation.GetD3D11Context(),
            requirements.uiWidth,
            requirements.uiHeight,
            requirements.format,
            kUiColor,
            ref2Ui);
    if (!texturesReady)
    {
        leftWorld.Release();
        rightWorld.Release();
        ref2Ui.Release();
        presentation.Shutdown();
        wprintf(L"[FAIL] Could not create the BFVR-owned D3D11 test textures.\n");
        return false;
    }

    bfvr::OpenXRPresentationTextures textures = {};
    textures.leftWorld = leftWorld.texture;
    textures.rightWorld = rightWorld.texture;
    textures.ref2Ui = ref2Ui.texture;
    bool submittedLayeredFrame = false;
    bool lifecycleHealthy = true;
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < durationMs)
    {
        if (!presentation.PollEvents())
        {
            lifecycleHealthy = false;
            break;
        }
        if (presentation.IsSessionRunning())
        {
            if (!presentation.SubmitFrame(textures))
            {
                lifecycleHealthy = false;
                break;
            }
            submittedLayeredFrame = true;
        }
        else
        {
            Sleep(10);
        }
    }

    ref2Ui.Release();
    rightWorld.Release();
    leftWorld.Release();
    presentation.Shutdown();
    if (lifecycleHealthy && submittedLayeredFrame)
    {
        wprintf(L"[INFO] OpenXR presentation probe submitted projection and UI composition layers.\n");
    }
    else if (lifecycleHealthy)
    {
        wprintf(L"[INFO] OpenXR session did not enter READY during the bounded probe; no frame was submitted.\n");
    }
    return lifecycleHealthy;
}
}

int wmain(int argc, wchar_t** argv)
{
    bool presentationMode = false;
    bool cylinderUi = false;
    bool omitGraphicsBinding = false;
    bool directD3D11Instance = false;
    DWORD durationMs = 10000;
    for (int index = 1; index < argc; ++index)
    {
        const wchar_t* const argument = argv[index];
        if (wcscmp(argument, L"--log") == 0 && index + 1 < argc)
        {
            if (_wfreopen_s(&g_output, argv[++index], L"w, ccs=UTF-8", stdout) != 0)
            {
                fwprintf(stderr, L"[FAIL] Could not open the requested log file.\n");
                return 2;
            }
        }
        else if (wcscmp(argument, L"--presentation") == 0)
        {
            presentationMode = true;
        }
        else if (wcscmp(argument, L"--ui-cylinder") == 0)
        {
            cylinderUi = true;
        }
        else if (wcscmp(argument, L"--session-no-binding-probe") == 0)
        {
            presentationMode = true;
            omitGraphicsBinding = true;
        }
        else if (wcscmp(argument, L"--direct-d3d11-instance-probe") == 0)
        {
            presentationMode = true;
            directD3D11Instance = true;
        }
        else if (wcscmp(argument, L"--duration-ms") == 0 && index + 1 < argc)
        {
            const unsigned long parsed = wcstoul(argv[++index], nullptr, 10);
            if (parsed < 1000 || parsed > 60000)
            {
                fwprintf(stderr, L"[FAIL] --duration-ms must be from 1000 through 60000.\n");
                return 2;
            }
            durationMs = static_cast<DWORD>(parsed);
        }
        else
        {
            fwprintf(
                stderr,
                L"Usage: BFVROpenXRProbe [--log <path>] [--presentation] [--ui-cylinder] [--session-no-binding-probe] [--direct-d3d11-instance-probe] [--duration-ms <1000-60000>]\n");
            return 2;
        }
    }

    wchar_t payloadDirectory[MAX_PATH] = {};
    if (!GetExecutableDirectory(payloadDirectory, std::size(payloadDirectory)))
    {
        fwprintf(stderr, L"[FAIL] Could not determine the BFVR probe directory.\n");
        return 2;
    }

    if (presentationMode)
    {
        const bool completed = RunPresentationProbe(
            payloadDirectory,
            cylinderUi,
            omitGraphicsBinding,
            directD3D11Instance,
            durationMs);
        fflush(g_output);
        return completed ? 0 : 1;
    }

    const bool systemAvailable = bfvr::ProbeOpenXRRuntime(payloadDirectory, WriteConsole, nullptr);
    if (!systemAvailable)
    {
        wprintf(L"[INFO] No OpenXR HMD system is available. The probe completed without creating a session or graphics binding.\n");
    }
    fflush(g_output);
    return 0;
}
