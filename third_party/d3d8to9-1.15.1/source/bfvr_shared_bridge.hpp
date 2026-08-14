/**
 * BFVR extension to pinned d3d8to9 v1.15.1.
 *
 * This ABI is intentionally C-shaped and uses opaque D3D8 pointers so the
 * BFVR client can resolve it dynamically without importing D3D9.
 */
#pragma once

#include <windows.h>

constexpr UINT BFVR_D3D8TO9_SHARED_BRIDGE_VERSION = 8;
constexpr DWORD BFVR_D3D8TO9_DEPTH_EXPORT_TIMING_VERSION = 1;
constexpr DWORD BFVR_D3DFMT_INTZ =
	static_cast<DWORD>('I') |
	(static_cast<DWORD>('N') << 8) |
	(static_cast<DWORD>('T') << 16) |
	(static_cast<DWORD>('Z') << 24);

enum class BFVRD3D8To9SharedHelperStage : DWORD
{
	NotAttempted = 0,
	GetCreationParameters = 1,
	GetDirect3D = 2,
	QueryDirect3D9Ex = 3,
	CreateHelperDevice = 4,
	CreateHelperTexture = 5,
	OpenOnGameDevice = 6,
	Complete = 7,
};

// Implemented only by this BFVR-patched d3d8to9 device wrapper. The exported
// bridge functions use it to reject native or foreign IDirect3DDevice8 objects
// before accessing the translator implementation.
inline constexpr GUID IID_BFVRD3D8To9Device =
{ 0x9a901225, 0xaf89, 0x472a, { 0x88, 0xa1, 0xcd, 0xe4, 0x09, 0x44, 0x7f, 0xa2 } };

using BFVRD3D8To9GetSharedBridgeVersionFn =
	UINT(WINAPI*)();

using BFVRD3D8To9CreateSharedRenderTargetFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		UINT width,
		UINT height,
		DWORD d3dFormat,
		HANDLE* sharedHandle,
		void** d3d8Surface);

using BFVRD3D8To9WaitForGpuFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		DWORD timeoutMilliseconds);

enum class BFVRD3D8To9DepthExportEncoding : DWORD
{
	PackedRgba8 = 1,
	FloatRgba16 = 2,
};

struct BFVRD3D8To9DepthExportTiming
{
	DWORD size;
	DWORD version;
	BOOL gpuTimestampsValid;
	BOOL gpuTimestampDisjoint;
	ULONGLONG timestampFrequency;
	ULONGLONG elapsedTicks;
	double elapsedMilliseconds;
};

using BFVRD3D8To9CreateTextureBackedDepthStencilFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		UINT width,
		UINT height,
		DWORD renderTargetFormat,
		void** d3d8DepthSurface);

using BFVRD3D8To9ResolveDepthToSharedTargetFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		void* d3d8DepthSurface,
		void* d3d8TargetSurface,
		DWORD encoding,
		BFVRD3D8To9DepthExportTiming* timing);

struct BFVRD3D8To9SharedDeviceDiagnostics
{
	DWORD size;
	DWORD version;
	BOOL extendedDevice;
	HRESULT cooperativeLevel;
	LONG helperDeviceCreations;
	LONG helperAttempts;
	DWORD lastHelperStage;
	HRESULT lastHelperResult;
	HRESULT lastHelperCreateDeviceResult;
	HRESULT lastHelperCreateTextureResult;
	HRESULT lastGameOpenResult;
	UINT adapterOrdinal;
	LONG adapterLuidHigh;
	DWORD adapterLuidLow;
	HRESULT getCreationParametersResult;
	HRESULT getDirect3DResult;
	HRESULT queryDirect3D9ExResult;
	HRESULT getAdapterLuidResult;
};

using BFVRD3D8To9GetSharedDeviceDiagnosticsFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		BFVRD3D8To9SharedDeviceDiagnostics* diagnostics);

struct BFVRD3D8To9VertexShaderIdentity
{
	DWORD size;
	DWORD version;
	DWORD d3d8Handle;
	BOOL programmable;
	DWORD originalFunctionByteCount;
	DWORD creationOrdinal;
	ULONGLONG originalFunctionHash;
};

using BFVRD3D8To9GetVertexShaderIdentityFn =
	HRESULT(WINAPI*)(
		void* d3d8Device,
		DWORD d3d8Handle,
		BFVRD3D8To9VertexShaderIdentity* identity);
