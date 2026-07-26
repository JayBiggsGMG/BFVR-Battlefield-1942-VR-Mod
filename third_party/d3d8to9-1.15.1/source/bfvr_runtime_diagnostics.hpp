#pragma once

#include <windows.h>

#define BFVR_D3D8TO9_RUNTIME_DIAGNOSTICS_VERSION 2u

struct BFVRD3D8To9RuntimeDiagnostics
{
	DWORD size;
	DWORD version;
	LONG managedTextureTranslations;
	LONG managedVolumeTextureTranslations;
	LONG managedCubeTextureTranslations;
	LONG managedVertexBufferTranslations;
	LONG managedIndexBufferTranslations;
	LONG managedTranslationFailures;
	LONG resetCalls;
	HRESULT lastResetResult;
	LONG forcedWindowedConversions;
};

using BFVRD3D8To9GetRuntimeDiagnosticsFn =
	HRESULT(WINAPI*)(BFVRD3D8To9RuntimeDiagnostics* diagnostics);

extern "C" __declspec(dllexport)
HRESULT WINAPI BFVRD3D8To9GetRuntimeDiagnostics(
	BFVRD3D8To9RuntimeDiagnostics* diagnostics);

enum class BFVRD3D8To9ManagedResourceKind
{
	Texture,
	VolumeTexture,
	CubeTexture,
	VertexBuffer,
	IndexBuffer
};

void BFVRD3D8To9RecordManagedTranslation(
	BFVRD3D8To9ManagedResourceKind kind,
	HRESULT result) noexcept;
void BFVRD3D8To9RecordReset(HRESULT result) noexcept;
void BFVRD3D8To9AdjustPresentParameters(
	void* presentParameters) noexcept;
