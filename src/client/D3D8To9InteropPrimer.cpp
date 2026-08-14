#include "client/D3D8To9InteropPrimer.h"

#include "presenter/D3DSystemRuntime.h"

#include <d3d11.h>
#include <dxgi1_2.h>

namespace bfvr
{
namespace
{
bool LuidsEqual(LUID left, LUID right) noexcept
{
    return left.HighPart == right.HighPart &&
        left.LowPart == right.LowPart;
}
} // namespace

bool PrimeD3D8To9D3D11SharedTextureInterop(
    const BFVRD3D8To9SharedDeviceDiagnostics& producerDiagnostics,
    HANDLE legacySharedHandle) noexcept
{
    const D3DSystemRuntime& runtime = GetD3DSystemRuntime();
    if (FAILED(producerDiagnostics.getAdapterLuidResult) ||
        legacySharedHandle == nullptr ||
        !runtime.IsAvailable())
    {
        return false;
    }

    LUID producerAdapterLuid = {};
    producerAdapterLuid.HighPart = producerDiagnostics.adapterLuidHigh;
    producerAdapterLuid.LowPart = producerDiagnostics.adapterLuidLow;

    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryResult = runtime.createDXGIFactory1(
        __uuidof(IDXGIFactory1),
        reinterpret_cast<void**>(&factory));
    if (FAILED(factoryResult) || factory == nullptr)
    {
        return false;
    }

    IDXGIAdapter1* matchingAdapter = nullptr;
    for (UINT index = 0; ; ++index)
    {
        IDXGIAdapter1* candidate = nullptr;
        const HRESULT enumerateResult =
            factory->EnumAdapters1(index, &candidate);
        if (enumerateResult == DXGI_ERROR_NOT_FOUND ||
            FAILED(enumerateResult) ||
            candidate == nullptr)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 description = {};
        const HRESULT descriptionResult =
            candidate->GetDesc1(&description);
        if (SUCCEEDED(descriptionResult) &&
            LuidsEqual(description.AdapterLuid, producerAdapterLuid))
        {
            matchingAdapter = candidate;
            break;
        }
        candidate->Release();
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel =
        static_cast<D3D_FEATURE_LEVEL>(0);
    HRESULT createDeviceResult = E_FAIL;
    if (matchingAdapter != nullptr)
    {
        createDeviceResult = runtime.createD3D11Device(
            matchingAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            &context);
    }

    ID3D11Texture2D* openedTexture = nullptr;
    const HRESULT openResult =
        SUCCEEDED(createDeviceResult) && device != nullptr
        ? device->OpenSharedResource(
            legacySharedHandle,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&openedTexture))
        : createDeviceResult;
    const bool primed = SUCCEEDED(openResult) && openedTexture != nullptr;

    if (openedTexture != nullptr)
        openedTexture->Release();
    if (context != nullptr)
        context->Release();
    if (device != nullptr)
        device->Release();
    if (matchingAdapter != nullptr)
        matchingAdapter->Release();
    factory->Release();
    return primed;
}

} // namespace bfvr
