/**
 * BFVR extension to pinned d3d8to9 v1.15.1.
 *
 * License: same BSD-2-Clause terms as the upstream d3d8to9 source.
 */
#include "bfvr_shared_bridge.hpp"
#include "bfvr_depth_export_shaders.hpp"
#include "d3d8to9.hpp"

namespace
{
constexpr DWORD kQueryTimeoutMilliseconds = 5000;

struct FullscreenVertex
{
	float x;
	float y;
	float z;
	float rhw;
	float u;
	float v;
};

template <typename T>
void ReleaseInterface(T*& interfacePointer)
{
	if (interfacePointer != nullptr)
	{
		interfacePointer->Release();
		interfacePointer = nullptr;
	}
}

HRESULT ValidateTranslatedDevice(
	void* opaqueDevice,
	Direct3DDevice8** translatedDevice)
{
	if (opaqueDevice == nullptr || translatedDevice == nullptr)
		return D3DERR_INVALIDCALL;

	*translatedDevice = nullptr;
	auto* const device8 = static_cast<IDirect3DDevice8*>(opaqueDevice);
	void* verifiedDevice = nullptr;
	const HRESULT result = device8->QueryInterface(
		IID_BFVRD3D8To9Device,
		&verifiedDevice);
	if (FAILED(result) || verifiedDevice == nullptr)
		return D3DERR_INVALIDCALL;

	*translatedDevice = static_cast<Direct3DDevice8*>(
		static_cast<IDirect3DDevice8*>(verifiedDevice));
	return D3D_OK;
}

HRESULT ValidateTranslatedSurface(
	Direct3DDevice8* translatedDevice,
	void* opaqueSurface,
	Direct3DSurface8** translatedSurface)
{
	if (translatedDevice == nullptr ||
		opaqueSurface == nullptr ||
		translatedSurface == nullptr)
	{
		return D3DERR_INVALIDCALL;
	}

	*translatedSurface = nullptr;
	auto* const surface8 = static_cast<IDirect3DSurface8*>(opaqueSurface);
	IDirect3DDevice8* owningDevice = nullptr;
	const HRESULT result = surface8->GetDevice(&owningDevice);
	const bool matches = SUCCEEDED(result) &&
		owningDevice == static_cast<IDirect3DDevice8*>(translatedDevice);
	ReleaseInterface(owningDevice);
	if (!matches)
		return D3DERR_INVALIDCALL;

	*translatedSurface = static_cast<Direct3DSurface8*>(surface8);
	return D3D_OK;
}

HRESULT WaitForQuery(
	IDirect3DQuery9* query,
	void* data,
	DWORD dataSize)
{
	if (query == nullptr)
		return D3DERR_INVALIDCALL;

	const DWORD startedAt = GetTickCount();
	HRESULT result = S_FALSE;
	while (result == S_FALSE)
	{
		result = query->GetData(data, dataSize, D3DGETDATA_FLUSH);
		if (result != S_FALSE)
			break;
		if (GetTickCount() - startedAt >= kQueryTimeoutMilliseconds)
			return D3DERR_WASSTILLDRAWING;
		SwitchToThread();
	}
	return result;
}

HRESULT ConfigureDepthExportState(
	IDirect3DDevice9* device,
	IDirect3DSurface9* target,
	IDirect3DTexture9* depthTexture,
	IDirect3DPixelShader9* pixelShader,
	UINT width,
	UINT height)
{
	if (device == nullptr || target == nullptr ||
		depthTexture == nullptr || pixelShader == nullptr)
	{
		return D3DERR_INVALIDCALL;
	}

	HRESULT result = device->SetDepthStencilSurface(nullptr);
	result = SUCCEEDED(result)
		? device->SetRenderTarget(0, target)
		: result;

	D3DVIEWPORT9 viewport = {};
	viewport.Width = width;
	viewport.Height = height;
	viewport.MaxZ = 1.0f;
	result = SUCCEEDED(result)
		? device->SetViewport(&viewport)
		: result;
	result = SUCCEEDED(result)
		? device->SetVertexShader(nullptr)
		: result;
	result = SUCCEEDED(result)
		? device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1)
		: result;
	result = SUCCEEDED(result)
		? device->SetPixelShader(pixelShader)
		: result;
	result = SUCCEEDED(result)
		? device->SetTexture(0, depthTexture)
		: result;

	const struct
	{
		D3DRENDERSTATETYPE state;
		DWORD value;
	} renderStates[] = {
		{D3DRS_ZENABLE, FALSE},
		{D3DRS_ZWRITEENABLE, FALSE},
		{D3DRS_STENCILENABLE, FALSE},
		{D3DRS_ALPHATESTENABLE, FALSE},
		{D3DRS_ALPHABLENDENABLE, FALSE},
		{D3DRS_FOGENABLE, FALSE},
		{D3DRS_CULLMODE, D3DCULL_NONE},
		{D3DRS_CLIPPING, FALSE},
		{D3DRS_SCISSORTESTENABLE, FALSE},
		{D3DRS_SRGBWRITEENABLE, FALSE},
		{D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE |
			D3DCOLORWRITEENABLE_ALPHA},
	};
	for (const auto& renderState : renderStates)
	{
		if (SUCCEEDED(result))
		{
			result = device->SetRenderState(
				renderState.state,
				renderState.value);
		}
	}

	const struct
	{
		D3DSAMPLERSTATETYPE state;
		DWORD value;
	} samplerStates[] = {
		{D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP},
		{D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
		{D3DSAMP_MAGFILTER, D3DTEXF_POINT},
		{D3DSAMP_MINFILTER, D3DTEXF_POINT},
		{D3DSAMP_MIPFILTER, D3DTEXF_NONE},
		{D3DSAMP_SRGBTEXTURE, FALSE},
	};
	for (const auto& samplerState : samplerStates)
	{
		if (SUCCEEDED(result))
		{
			result = device->SetSamplerState(
				0,
				samplerState.state,
				samplerState.value);
		}
	}
	return result;
}

void CollectGpuTiming(
	IDirect3DQuery9* frequencyQuery,
	IDirect3DQuery9* disjointQuery,
	IDirect3DQuery9* startQuery,
	IDirect3DQuery9* endQuery,
	BFVRD3D8To9DepthExportTiming& timing)
{
	ULONGLONG frequency = 0;
	ULONGLONG started = 0;
	ULONGLONG ended = 0;
	BOOL disjoint = TRUE;
	const bool valid =
		SUCCEEDED(WaitForQuery(
			frequencyQuery,
			&frequency,
			sizeof(frequency))) &&
		SUCCEEDED(WaitForQuery(
			disjointQuery,
			&disjoint,
			sizeof(disjoint))) &&
		SUCCEEDED(WaitForQuery(
			startQuery,
			&started,
			sizeof(started))) &&
		SUCCEEDED(WaitForQuery(
			endQuery,
			&ended,
			sizeof(ended))) &&
		frequency != 0 && ended >= started;

	timing.gpuTimestampDisjoint = disjoint;
	if (!valid || disjoint)
		return;

	timing.timestampFrequency = frequency;
	timing.elapsedTicks = ended - started;
	timing.elapsedMilliseconds =
		static_cast<double>(timing.elapsedTicks) * 1000.0 /
		static_cast<double>(frequency);
	timing.gpuTimestampsValid = TRUE;
}
} // namespace

extern "C" HRESULT WINAPI BFVRD3D8To9CreateTextureBackedDepthStencil(
	void* opaqueDevice,
	UINT width,
	UINT height,
	DWORD renderTargetFormat,
	void** d3d8DepthSurface)
{
	if (d3d8DepthSurface == nullptr || width == 0 || height == 0)
		return D3DERR_INVALIDCALL;
	*d3d8DepthSurface = nullptr;

	Direct3DDevice8* translatedDevice = nullptr;
	HRESULT result = ValidateTranslatedDevice(
		opaqueDevice,
		&translatedDevice);
	if (FAILED(result))
		return result;

	IDirect3DDevice9* const device9 =
		translatedDevice->GetProxyInterface();
	D3DDEVICE_CREATION_PARAMETERS creation = {};
	D3DDISPLAYMODE displayMode = {};
	IDirect3D9* direct3D9 = nullptr;
	result = device9->GetCreationParameters(&creation);
	result = SUCCEEDED(result)
		? device9->GetDirect3D(&direct3D9)
		: result;
	if (SUCCEEDED(result) && direct3D9 == nullptr)
		result = E_FAIL;
	result = SUCCEEDED(result)
		? direct3D9->GetAdapterDisplayMode(
			creation.AdapterOrdinal,
			&displayMode)
		: result;
	result = SUCCEEDED(result)
		? direct3D9->CheckDeviceFormat(
			creation.AdapterOrdinal,
			creation.DeviceType,
			displayMode.Format,
			D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_TEXTURE,
			D3DFMT_BFVR_INTZ)
		: result;
	result = SUCCEEDED(result)
		? direct3D9->CheckDepthStencilMatch(
			creation.AdapterOrdinal,
			creation.DeviceType,
			displayMode.Format,
			static_cast<D3DFORMAT>(renderTargetFormat),
			D3DFMT_BFVR_INTZ)
		: result;

	IDirect3DTexture9* texture9 = nullptr;
	result = SUCCEEDED(result)
		? device9->CreateTexture(
			width,
			height,
			1,
			D3DUSAGE_DEPTHSTENCIL,
			D3DFMT_BFVR_INTZ,
			D3DPOOL_DEFAULT,
			&texture9,
			nullptr)
		: result;
	IDirect3DSurface9* surface9 = nullptr;
	result = SUCCEEDED(result) && texture9 != nullptr
		? texture9->GetSurfaceLevel(0, &surface9)
		: result;
	ReleaseInterface(texture9);
	ReleaseInterface(direct3D9);
	if (FAILED(result) || surface9 == nullptr)
	{
		ReleaseInterface(surface9);
		translatedDevice->Release();
		return FAILED(result) ? result : E_FAIL;
	}

	auto* const surface8 = translatedDevice->ProxyAddressLookupTable
		->FindAddress<Direct3DSurface8>(surface9);
	if (surface8 == nullptr)
	{
		surface9->Release();
		translatedDevice->Release();
		return E_OUTOFMEMORY;
	}

	*d3d8DepthSurface = static_cast<IDirect3DSurface8*>(surface8);
	translatedDevice->Release();
	return D3D_OK;
}

extern "C" HRESULT WINAPI BFVRD3D8To9ResolveDepthToSharedTarget(
	void* opaqueDevice,
	void* opaqueDepthSurface,
	void* opaqueTargetSurface,
	DWORD encodingValue,
	BFVRD3D8To9DepthExportTiming* timing)
{
	if (timing != nullptr &&
		timing->size < sizeof(BFVRD3D8To9DepthExportTiming))
	{
		return E_INVALIDARG;
	}
	if (timing != nullptr)
	{
		const DWORD size = timing->size;
		*timing = {};
		timing->size = size;
		timing->version = BFVR_D3D8TO9_DEPTH_EXPORT_TIMING_VERSION;
	}

	const auto encoding =
		static_cast<BFVRD3D8To9DepthExportEncoding>(encodingValue);
	if (encoding != BFVRD3D8To9DepthExportEncoding::PackedRgba8 &&
		encoding != BFVRD3D8To9DepthExportEncoding::FloatRgba16)
	{
		return D3DERR_INVALIDCALL;
	}

	Direct3DDevice8* translatedDevice = nullptr;
	HRESULT result = ValidateTranslatedDevice(
		opaqueDevice,
		&translatedDevice);
	if (FAILED(result))
		return result;

	Direct3DSurface8* depthSurface8 = nullptr;
	Direct3DSurface8* targetSurface8 = nullptr;
	result = ValidateTranslatedSurface(
		translatedDevice,
		opaqueDepthSurface,
		&depthSurface8);
	result = SUCCEEDED(result)
		? ValidateTranslatedSurface(
			translatedDevice,
			opaqueTargetSurface,
			&targetSurface8)
		: result;
	if (FAILED(result))
	{
		translatedDevice->Release();
		return result;
	}

	IDirect3DSurface9* const depthSurface9 =
		depthSurface8->GetProxyInterface();
	IDirect3DSurface9* const targetSurface9 =
		targetSurface8->GetProxyInterface();
	D3DSURFACE_DESC depthDescription = {};
	D3DSURFACE_DESC targetDescription = {};
	result = depthSurface9->GetDesc(&depthDescription);
	result = SUCCEEDED(result)
		? targetSurface9->GetDesc(&targetDescription)
		: result;
	const D3DFORMAT requiredTargetFormat =
		encoding == BFVRD3D8To9DepthExportEncoding::PackedRgba8
		? D3DFMT_A8R8G8B8
		: D3DFMT_A16B16G16R16F;
	if (FAILED(result) ||
		depthDescription.Format != D3DFMT_BFVR_INTZ ||
		(depthDescription.Usage & D3DUSAGE_DEPTHSTENCIL) == 0 ||
		targetDescription.Format != requiredTargetFormat ||
		(targetDescription.Usage & D3DUSAGE_RENDERTARGET) == 0 ||
		depthDescription.Width != targetDescription.Width ||
		depthDescription.Height != targetDescription.Height ||
		depthDescription.MultiSampleType != D3DMULTISAMPLE_NONE ||
		targetDescription.MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		translatedDevice->Release();
		return FAILED(result) ? result : D3DERR_INVALIDCALL;
	}

	IDirect3DTexture9* depthTexture9 = nullptr;
	result = depthSurface9->GetContainer(
		__uuidof(IDirect3DTexture9),
		reinterpret_cast<void**>(&depthTexture9));
	IDirect3DDevice9* const device9 =
		translatedDevice->GetProxyInterface();
	IDirect3DPixelShader9* pixelShader = nullptr;
	const BYTE* const bytecode =
		encoding == BFVRD3D8To9DepthExportEncoding::PackedRgba8
		? g_bfvrDepthPackedShader
		: g_bfvrDepthFloatShader;
	result = SUCCEEDED(result)
		? device9->CreatePixelShader(
			reinterpret_cast<const DWORD*>(bytecode),
			&pixelShader)
		: result;

	IDirect3DStateBlock9* stateBlock = nullptr;
	IDirect3DSurface9* priorTarget = nullptr;
	IDirect3DSurface9* priorDepth = nullptr;
	D3DVIEWPORT9 priorViewport = {};
	result = SUCCEEDED(result)
		? device9->CreateStateBlock(D3DSBT_ALL, &stateBlock)
		: result;
	result = SUCCEEDED(result) && stateBlock != nullptr
		? stateBlock->Capture()
		: result;
	result = SUCCEEDED(result)
		? device9->GetRenderTarget(0, &priorTarget)
		: result;
	if (SUCCEEDED(result))
	{
		const HRESULT depthResult =
			device9->GetDepthStencilSurface(&priorDepth);
		if (FAILED(depthResult) && depthResult != D3DERR_NOTFOUND)
			result = depthResult;
	}
	result = SUCCEEDED(result)
		? device9->GetViewport(&priorViewport)
		: result;

	IDirect3DQuery9* frequencyQuery = nullptr;
	IDirect3DQuery9* disjointQuery = nullptr;
	IDirect3DQuery9* startQuery = nullptr;
	IDirect3DQuery9* endQuery = nullptr;
	bool timingActive =
		SUCCEEDED(device9->CreateQuery(
			D3DQUERYTYPE_TIMESTAMPFREQ,
			&frequencyQuery)) && frequencyQuery != nullptr &&
		SUCCEEDED(device9->CreateQuery(
			D3DQUERYTYPE_TIMESTAMPDISJOINT,
			&disjointQuery)) && disjointQuery != nullptr &&
		SUCCEEDED(device9->CreateQuery(
			D3DQUERYTYPE_TIMESTAMP,
			&startQuery)) && startQuery != nullptr &&
		SUCCEEDED(device9->CreateQuery(
			D3DQUERYTYPE_TIMESTAMP,
			&endQuery)) && endQuery != nullptr;
	bool timingStarted = false;
	if (SUCCEEDED(result) && timingActive)
	{
		timingActive =
			SUCCEEDED(disjointQuery->Issue(D3DISSUE_BEGIN)) &&
			SUCCEEDED(frequencyQuery->Issue(D3DISSUE_END)) &&
			SUCCEEDED(startQuery->Issue(D3DISSUE_END));
		timingStarted = timingActive;
	}

	result = SUCCEEDED(result)
		? ConfigureDepthExportState(
			device9,
			targetSurface9,
			depthTexture9,
			pixelShader,
			targetDescription.Width,
			targetDescription.Height)
		: result;
	bool sceneBegan = false;
	if (SUCCEEDED(result))
	{
		result = device9->BeginScene();
		sceneBegan = SUCCEEDED(result);
	}
	const FullscreenVertex vertices[] = {
		{-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
		{static_cast<float>(targetDescription.Width) - 0.5f,
			-0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
		{-0.5f,
			static_cast<float>(targetDescription.Height) - 0.5f,
			0.0f, 1.0f, 0.0f, 1.0f},
		{static_cast<float>(targetDescription.Width) - 0.5f,
			static_cast<float>(targetDescription.Height) - 0.5f,
			0.0f, 1.0f, 1.0f, 1.0f},
	};
	result = SUCCEEDED(result)
		? device9->DrawPrimitiveUP(
			D3DPT_TRIANGLESTRIP,
			2,
			vertices,
			sizeof(FullscreenVertex))
		: result;
	if (timingStarted)
	{
		timingActive =
			SUCCEEDED(endQuery->Issue(D3DISSUE_END)) &&
			SUCCEEDED(disjointQuery->Issue(D3DISSUE_END));
	}
	if (sceneBegan)
	{
		const HRESULT endSceneResult = device9->EndScene();
		if (SUCCEEDED(result) && FAILED(endSceneResult))
			result = endSceneResult;
	}

	const HRESULT unbindResult = device9->SetTexture(0, nullptr);
	const HRESULT stateRestoreResult = stateBlock == nullptr
		? E_FAIL
		: stateBlock->Apply();
	const HRESULT targetRestoreResult = priorTarget == nullptr
		? E_FAIL
		: device9->SetRenderTarget(0, priorTarget);
	const HRESULT depthRestoreResult =
		device9->SetDepthStencilSurface(priorDepth);
	const HRESULT viewportRestoreResult =
		device9->SetViewport(&priorViewport);
	const HRESULT restorationResults[] = {
		unbindResult,
		stateRestoreResult,
		targetRestoreResult,
		depthRestoreResult,
		viewportRestoreResult,
	};
	for (const HRESULT restorationResult : restorationResults)
	{
		if (SUCCEEDED(result) && FAILED(restorationResult))
			result = restorationResult;
	}

	if (timing != nullptr && timingActive)
	{
		CollectGpuTiming(
			frequencyQuery,
			disjointQuery,
			startQuery,
			endQuery,
			*timing);
	}

	ReleaseInterface(endQuery);
	ReleaseInterface(startQuery);
	ReleaseInterface(disjointQuery);
	ReleaseInterface(frequencyQuery);
	ReleaseInterface(priorDepth);
	ReleaseInterface(priorTarget);
	ReleaseInterface(stateBlock);
	ReleaseInterface(pixelShader);
	ReleaseInterface(depthTexture9);
	translatedDevice->Release();
	return result;
}
