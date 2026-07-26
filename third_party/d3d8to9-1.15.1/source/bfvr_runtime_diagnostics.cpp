#include "bfvr_runtime_diagnostics.hpp"

#include <d3d9.h>

namespace
{
volatile LONG g_managedTextureTranslations = 0;
volatile LONG g_managedVolumeTextureTranslations = 0;
volatile LONG g_managedCubeTextureTranslations = 0;
volatile LONG g_managedVertexBufferTranslations = 0;
volatile LONG g_managedIndexBufferTranslations = 0;
volatile LONG g_managedTranslationFailures = 0;
volatile LONG g_resetCalls = 0;
volatile LONG g_lastResetResult = E_PENDING;
volatile LONG g_forcedWindowedConversions = 0;

LONG ReadCounter(volatile LONG* counter) noexcept
{
	return InterlockedCompareExchange(counter, 0, 0);
}
}

extern "C" HRESULT WINAPI BFVRD3D8To9GetRuntimeDiagnostics(
	BFVRD3D8To9RuntimeDiagnostics* diagnostics)
{
	if (diagnostics == nullptr ||
		diagnostics->size < sizeof(BFVRD3D8To9RuntimeDiagnostics))
	{
		return E_INVALIDARG;
	}

	BFVRD3D8To9RuntimeDiagnostics snapshot = {};
	snapshot.size = sizeof(snapshot);
	snapshot.version = BFVR_D3D8TO9_RUNTIME_DIAGNOSTICS_VERSION;
	snapshot.managedTextureTranslations =
		ReadCounter(&g_managedTextureTranslations);
	snapshot.managedVolumeTextureTranslations =
		ReadCounter(&g_managedVolumeTextureTranslations);
	snapshot.managedCubeTextureTranslations =
		ReadCounter(&g_managedCubeTextureTranslations);
	snapshot.managedVertexBufferTranslations =
		ReadCounter(&g_managedVertexBufferTranslations);
	snapshot.managedIndexBufferTranslations =
		ReadCounter(&g_managedIndexBufferTranslations);
	snapshot.managedTranslationFailures =
		ReadCounter(&g_managedTranslationFailures);
	snapshot.resetCalls = ReadCounter(&g_resetCalls);
	snapshot.lastResetResult =
		static_cast<HRESULT>(ReadCounter(&g_lastResetResult));
	snapshot.forcedWindowedConversions =
		ReadCounter(&g_forcedWindowedConversions);
	*diagnostics = snapshot;
	return S_OK;
}

void BFVRD3D8To9RecordManagedTranslation(
	BFVRD3D8To9ManagedResourceKind kind,
	HRESULT result) noexcept
{
	volatile LONG* counter = nullptr;
	switch (kind)
	{
	case BFVRD3D8To9ManagedResourceKind::Texture:
		counter = &g_managedTextureTranslations;
		break;
	case BFVRD3D8To9ManagedResourceKind::VolumeTexture:
		counter = &g_managedVolumeTextureTranslations;
		break;
	case BFVRD3D8To9ManagedResourceKind::CubeTexture:
		counter = &g_managedCubeTextureTranslations;
		break;
	case BFVRD3D8To9ManagedResourceKind::VertexBuffer:
		counter = &g_managedVertexBufferTranslations;
		break;
	case BFVRD3D8To9ManagedResourceKind::IndexBuffer:
		counter = &g_managedIndexBufferTranslations;
		break;
	}

	if (counter != nullptr)
	{
		InterlockedIncrement(counter);
	}
	if (FAILED(result))
	{
		InterlockedIncrement(&g_managedTranslationFailures);
	}
}

void BFVRD3D8To9RecordReset(HRESULT result) noexcept
{
	InterlockedExchange(&g_lastResetResult, result);
	InterlockedIncrement(&g_resetCalls);
}

void BFVRD3D8To9AdjustPresentParameters(
	void* presentParameters) noexcept
{
	if (presentParameters == nullptr)
		return;

	wchar_t enabled[2] = {};
	if (GetEnvironmentVariableW(
			L"BFVR_D3D8TO9_FORCE_WINDOWED",
			enabled,
			static_cast<DWORD>(_countof(enabled))) != 1 ||
		enabled[0] != L'1')
	{
		return;
	}

	auto& parameters =
		*static_cast<D3DPRESENT_PARAMETERS*>(presentParameters);
	parameters.Windowed = TRUE;
	parameters.FullScreen_RefreshRateInHz = 0;
	InterlockedIncrement(&g_forcedWindowedConversions);
}
