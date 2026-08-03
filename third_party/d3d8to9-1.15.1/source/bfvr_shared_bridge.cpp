/**
 * BFVR extension to pinned d3d8to9 v1.15.1.
 *
 * License: same BSD-2-Clause terms as the upstream d3d8to9 source.
 */
#include "bfvr_shared_bridge.hpp"
#include "d3d8to9.hpp"

namespace
{
volatile LONG g_helperDeviceCreations = 0;
volatile LONG g_helperAttempts = 0;
volatile LONG g_lastHelperStage = static_cast<LONG>(
	BFVRD3D8To9SharedHelperStage::NotAttempted);
volatile LONG g_lastHelperResult = E_PENDING;
volatile LONG g_lastHelperCreateDeviceResult = E_PENDING;
volatile LONG g_lastHelperCreateTextureResult = E_PENDING;
volatile LONG g_lastGameOpenResult = E_PENDING;

void PublishHelperDiagnostics(
	BFVRD3D8To9SharedHelperStage stage,
	HRESULT result)
{
	InterlockedExchange(
		&g_lastHelperStage,
		static_cast<LONG>(stage));
	InterlockedExchange(&g_lastHelperResult, result);
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
	const HRESULT result =
		device8->QueryInterface(IID_BFVRD3D8To9Device, &verifiedDevice);
	if (FAILED(result) || verifiedDevice == nullptr)
		return D3DERR_INVALIDCALL;

	*translatedDevice =
		static_cast<Direct3DDevice8*>(
			static_cast<IDirect3DDevice8*>(verifiedDevice));
	return D3D_OK;
}

bool IsSupportedSharedFormat(D3DFORMAT format)
{
	return format == D3DFMT_A2B10G10R10 ||
		format == D3DFMT_A16B16G16R16F ||
		format == D3DFMT_A8R8G8B8 ||
		format == D3DFMT_A8B8G8R8;
}

HRESULT CreateSharedTextureThroughHelper(
	Direct3DDevice8* translatedDevice,
	UINT width,
	UINT height,
	D3DFORMAT format,
	IDirect3DTexture9** openedTexture,
	HANDLE* sharedHandle)
{
	if (translatedDevice == nullptr ||
		openedTexture == nullptr ||
		sharedHandle == nullptr)
	{
		return D3DERR_INVALIDCALL;
	}
	*openedTexture = nullptr;
	*sharedHandle = nullptr;
	InterlockedIncrement(&g_helperAttempts);
	InterlockedExchange(
		&g_lastHelperCreateDeviceResult,
		E_PENDING);
	InterlockedExchange(
		&g_lastHelperCreateTextureResult,
		E_PENDING);
	InterlockedExchange(&g_lastGameOpenResult, E_PENDING);
	PublishHelperDiagnostics(
		BFVRD3D8To9SharedHelperStage::GetCreationParameters,
		E_PENDING);

	IDirect3DDevice9* const gameDevice =
		translatedDevice->GetProxyInterface();
	D3DDEVICE_CREATION_PARAMETERS creation = {};
	IDirect3D9* direct3D9 = nullptr;
	IDirect3D9Ex* direct3D9Ex = nullptr;
	IDirect3DDevice9Ex* helperDevice = nullptr;
	IDirect3DTexture9* helperTexture = nullptr;
	IDirect3DTexture9* gameTexture = nullptr;
	HRESULT result = gameDevice->GetCreationParameters(&creation);
	PublishHelperDiagnostics(
		BFVRD3D8To9SharedHelperStage::GetCreationParameters,
		result);
	if (SUCCEEDED(result))
	{
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::GetDirect3D,
			E_PENDING);
		result = gameDevice->GetDirect3D(&direct3D9);
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::GetDirect3D,
			result);
	}
	if (SUCCEEDED(result) && direct3D9 != nullptr)
	{
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::QueryDirect3D9Ex,
			E_PENDING);
		result = direct3D9->QueryInterface(
			__uuidof(IDirect3D9Ex),
			reinterpret_cast<void**>(&direct3D9Ex));
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::QueryDirect3D9Ex,
			result);
	}
	if (SUCCEEDED(result) && direct3D9Ex != nullptr)
	{
		D3DPRESENT_PARAMETERS presentation = {};
		presentation.BackBufferWidth = 1;
		presentation.BackBufferHeight = 1;
		presentation.BackBufferFormat = D3DFMT_UNKNOWN;
		presentation.BackBufferCount = 1;
		presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
		presentation.hDeviceWindow = creation.hFocusWindow == nullptr
			? GetDesktopWindow()
			: creation.hFocusWindow;
		presentation.Windowed = TRUE;
		presentation.PresentationInterval =
			D3DPRESENT_INTERVAL_IMMEDIATE;
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::CreateHelperDevice,
			E_PENDING);
		result = direct3D9Ex->CreateDeviceEx(
			creation.AdapterOrdinal,
			creation.DeviceType,
			presentation.hDeviceWindow,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING |
				D3DCREATE_FPU_PRESERVE,
			&presentation,
			nullptr,
			&helperDevice);
		InterlockedExchange(
			&g_lastHelperCreateDeviceResult,
			result);
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::CreateHelperDevice,
			result);
	}

	HANDLE createdHandle = nullptr;
	if (SUCCEEDED(result) && helperDevice != nullptr)
	{
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::CreateHelperTexture,
			E_PENDING);
		result = helperDevice->CreateTexture(
			width,
			height,
			1,
			D3DUSAGE_RENDERTARGET,
			format,
			D3DPOOL_DEFAULT,
			&helperTexture,
			&createdHandle);
		InterlockedExchange(
			&g_lastHelperCreateTextureResult,
			result);
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::CreateHelperTexture,
			result);
	}
	HANDLE openHandle = createdHandle;
	if (SUCCEEDED(result) &&
		helperTexture != nullptr &&
		createdHandle != nullptr)
	{
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::OpenOnGameDevice,
			E_PENDING);
		result = gameDevice->CreateTexture(
			width,
			height,
			1,
			D3DUSAGE_RENDERTARGET,
			format,
			D3DPOOL_DEFAULT,
			&gameTexture,
			&openHandle);
		InterlockedExchange(
			&g_lastGameOpenResult,
			result);
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::OpenOnGameDevice,
			result);
	}
	if (SUCCEEDED(result) && gameTexture != nullptr)
	{
		*openedTexture = gameTexture;
		*sharedHandle = createdHandle;
		gameTexture = nullptr;
		InterlockedIncrement(&g_helperDeviceCreations);
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::Complete,
			D3D_OK);
	}
	else if (SUCCEEDED(result))
	{
		result = E_FAIL;
		PublishHelperDiagnostics(
			BFVRD3D8To9SharedHelperStage::OpenOnGameDevice,
			result);
	}

	if (gameTexture != nullptr)
		gameTexture->Release();
	if (helperTexture != nullptr)
		helperTexture->Release();
	if (helperDevice != nullptr)
		helperDevice->Release();
	if (direct3D9Ex != nullptr)
		direct3D9Ex->Release();
	if (direct3D9 != nullptr)
		direct3D9->Release();
	return result;
}
} // namespace

extern "C" UINT WINAPI BFVRD3D8To9GetSharedBridgeVersion()
{
	return BFVR_D3D8TO9_SHARED_BRIDGE_VERSION;
}

extern "C" HRESULT WINAPI BFVRD3D8To9GetSharedDeviceDiagnostics(
	void* opaqueDevice,
	BFVRD3D8To9SharedDeviceDiagnostics* diagnostics)
{
	if (diagnostics == nullptr ||
		diagnostics->size < sizeof(BFVRD3D8To9SharedDeviceDiagnostics))
	{
		return E_INVALIDARG;
	}

	Direct3DDevice8* translatedDevice = nullptr;
	const HRESULT result =
		ValidateTranslatedDevice(opaqueDevice, &translatedDevice);
	if (FAILED(result))
		return result;

	BFVRD3D8To9SharedDeviceDiagnostics snapshot = {};
	snapshot.size = sizeof(snapshot);
	snapshot.version = BFVR_D3D8TO9_SHARED_BRIDGE_VERSION;
	snapshot.extendedDevice =
		translatedDevice->UsesExtendedDevice() ? TRUE : FALSE;
	snapshot.cooperativeLevel =
		translatedDevice->GetProxyInterface()->TestCooperativeLevel();
	snapshot.helperDeviceCreations =
		InterlockedCompareExchange(
			&g_helperDeviceCreations,
			0,
			0);
	snapshot.helperAttempts =
		InterlockedCompareExchange(
			&g_helperAttempts,
			0,
			0);
	snapshot.lastHelperStage = static_cast<DWORD>(
		InterlockedCompareExchange(
			&g_lastHelperStage,
			0,
			0));
	snapshot.lastHelperResult =
		InterlockedCompareExchange(
			&g_lastHelperResult,
			0,
			0);
	snapshot.lastHelperCreateDeviceResult =
		InterlockedCompareExchange(
			&g_lastHelperCreateDeviceResult,
			0,
			0);
	snapshot.lastHelperCreateTextureResult =
		InterlockedCompareExchange(
			&g_lastHelperCreateTextureResult,
			0,
			0);
	snapshot.lastGameOpenResult =
		InterlockedCompareExchange(
			&g_lastGameOpenResult,
			0,
			0);
	*diagnostics = snapshot;
	translatedDevice->Release();
	return D3D_OK;
}

extern "C" HRESULT WINAPI BFVRD3D8To9GetVertexShaderIdentity(
	void* opaqueDevice,
	DWORD d3d8Handle,
	BFVRD3D8To9VertexShaderIdentity* identity)
{
	if (identity == nullptr ||
		identity->size <
			sizeof(BFVRD3D8To9VertexShaderIdentity))
	{
		return E_INVALIDARG;
	}

	Direct3DDevice8* translatedDevice = nullptr;
	const HRESULT result =
		ValidateTranslatedDevice(opaqueDevice, &translatedDevice);
	if (FAILED(result))
		return result;

	BFVRD3D8To9VertexShaderIdentity snapshot = {};
	const HRESULT identityResult =
		translatedDevice->GetVertexShaderIdentity(
			d3d8Handle,
			snapshot);
	if (SUCCEEDED(identityResult))
		*identity = snapshot;
	translatedDevice->Release();
	return identityResult;
}

extern "C" HRESULT WINAPI BFVRD3D8To9CreateSharedRenderTarget(
	void* opaqueDevice,
	UINT width,
	UINT height,
	DWORD d3dFormat,
	HANDLE* sharedHandle,
	void** d3d8Surface)
{
	if (sharedHandle == nullptr || d3d8Surface == nullptr ||
		width == 0 || height == 0)
		return D3DERR_INVALIDCALL;

	*sharedHandle = nullptr;
	*d3d8Surface = nullptr;

	Direct3DDevice8* translatedDevice = nullptr;
	HRESULT result =
		ValidateTranslatedDevice(opaqueDevice, &translatedDevice);
	if (FAILED(result))
		return result;

	const D3DFORMAT format = static_cast<D3DFORMAT>(d3dFormat);
	if (!IsSupportedSharedFormat(format))
	{
		translatedDevice->Release();
		return D3DERR_INVALIDCALL;
	}

	IDirect3DTexture9* texture = nullptr;
	HANDLE handle = nullptr;
	wchar_t forceHelper[2] = {};
	const bool helperForced =
		GetEnvironmentVariableW(
			L"BFVR_D3D8TO9_FORCE_SHARED_HELPER",
			forceHelper,
			static_cast<DWORD>(_countof(forceHelper))) == 1 &&
		forceHelper[0] == L'1';
	result = helperForced
		? D3DERR_INVALIDCALL
		: translatedDevice->GetProxyInterface()->CreateTexture(
			width,
			height,
			1,
			D3DUSAGE_RENDERTARGET,
			format,
			D3DPOOL_DEFAULT,
			&texture,
			&handle);
	if (FAILED(result))
	{
		result = CreateSharedTextureThroughHelper(
			translatedDevice,
			width,
			height,
			format,
			&texture,
			&handle);
	}
	if (FAILED(result) || texture == nullptr || handle == nullptr)
	{
		if (texture != nullptr)
			texture->Release();
		translatedDevice->Release();
		return FAILED(result) ? result : E_FAIL;
	}

	IDirect3DSurface9* surface9 = nullptr;
	result = texture->GetSurfaceLevel(0, &surface9);
	texture->Release();
	if (FAILED(result) || surface9 == nullptr)
	{
		translatedDevice->Release();
		return FAILED(result) ? result : E_FAIL;
	}

	auto* const surface8 =
		translatedDevice->ProxyAddressLookupTable
			->FindAddress<Direct3DSurface8>(surface9);
	if (surface8 == nullptr)
	{
		surface9->Release();
		translatedDevice->Release();
		return E_OUTOFMEMORY;
	}

	*sharedHandle = handle;
	*d3d8Surface = static_cast<IDirect3DSurface8*>(surface8);
	translatedDevice->Release();
	return D3D_OK;
}

extern "C" HRESULT WINAPI BFVRD3D8To9WaitForGpu(
	void* opaqueDevice,
	DWORD timeoutMilliseconds)
{
	Direct3DDevice8* translatedDevice = nullptr;
	HRESULT result =
		ValidateTranslatedDevice(opaqueDevice, &translatedDevice);
	if (FAILED(result))
		return result;

	IDirect3DQuery9* query = nullptr;
	result = translatedDevice->GetProxyInterface()->CreateQuery(
		D3DQUERYTYPE_EVENT,
		&query);
	if (SUCCEEDED(result) && query != nullptr)
		result = query->Issue(D3DISSUE_END);

	const DWORD startedAt = GetTickCount();
	while (SUCCEEDED(result) && query != nullptr)
	{
		result = query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
		if (result != S_FALSE)
			break;
		if (GetTickCount() - startedAt >= timeoutMilliseconds)
		{
			result = D3DERR_WASSTILLDRAWING;
			break;
		}
		SwitchToThread();
	}

	if (query != nullptr)
		query->Release();
	translatedDevice->Release();
	return result;
}
