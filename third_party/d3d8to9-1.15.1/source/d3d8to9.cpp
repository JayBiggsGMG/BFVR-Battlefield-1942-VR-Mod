/**
 * Copyright (C) 2015 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/d3d8to9#license
 */

#include "d3dx9.hpp"
#include "d3d8to9.hpp"

#include <cwchar>

namespace
{
using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using Direct3DCreate9ExFn = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);

struct SystemD3D9Api
{
	HMODULE module = nullptr;
	Direct3DCreate9Fn create9 = nullptr;
	Direct3DCreate9ExFn create9Ex = nullptr;
};

SystemD3D9Api LoadSystemD3D9()
{
	SystemD3D9Api api = {};
	wchar_t path[MAX_PATH] = {};
	const UINT directoryLength = GetSystemDirectoryW(path, MAX_PATH);
	constexpr wchar_t suffix[] = L"\\d3d9.dll";
	if (directoryLength == 0 || directoryLength >= MAX_PATH ||
		directoryLength + ARRAYSIZE(suffix) > ARRAYSIZE(path))
	{
		return api;
	}

	std::wmemcpy(path + directoryLength, suffix, ARRAYSIZE(suffix));
	api.module = LoadLibraryExW(
		path,
		nullptr,
		LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (api.module == nullptr)
	{
		// The path is already absolute and was obtained from Windows. This
		// fallback keeps the translator usable on older Windows installations
		// that do not implement the LOAD_LIBRARY_SEARCH_* flags.
		api.module = LoadLibraryW(path);
	}
	if (api.module == nullptr)
	{
		return api;
	}

	api.create9 = reinterpret_cast<Direct3DCreate9Fn>(
		GetProcAddress(api.module, "Direct3DCreate9"));
	api.create9Ex = reinterpret_cast<Direct3DCreate9ExFn>(
		GetProcAddress(api.module, "Direct3DCreate9Ex"));
	if (api.create9 == nullptr && api.create9Ex == nullptr)
	{
		FreeLibrary(api.module);
		api = {};
	}
	return api;
}

const SystemD3D9Api& GetSystemD3D9()
{
	// Direct3DCreate8 is invoked after loader initialization, so ordinary C++
	// one-time initialization is safe here. Keep the module loaded for the
	// lifetime of every translated interface returned from it.
	static const SystemD3D9Api api = LoadSystemD3D9();
	return api;
}
} // namespace

PFN_D3DXAssembleShader D3DXAssembleShader = nullptr;
PFN_D3DXDisassembleShader D3DXDisassembleShader = nullptr;
PFN_D3DXGetShaderSize D3DXGetShaderSize = nullptr;
PFN_D3DXLoadSurfaceFromSurface D3DXLoadSurfaceFromSurface = nullptr;

#ifndef D3D8TO9NOLOG
 // Very simple logging for the purpose of debugging only.
std::ofstream LOG;
#endif

extern "C" HRESULT WINAPI ValidatePixelShader(const DWORD* pPixelShader, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** pErrorsString)
{
#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "ValidatePixelShader " << "(" << pPixelShader << ", " << pCaps << ", " << ReturnErrors << ", " << pErrorsString << ")' ..." << std::endl;
#endif

	HRESULT hr = E_FAIL;
	const char* errorMessage = "";

	if (!pPixelShader)
	{
		errorMessage = "Invalid code pointer.\n";
	}
	else
	{
		switch (*pPixelShader)
		{
		case D3DPS_VERSION(1, 0):
		case D3DPS_VERSION(1, 1):
		case D3DPS_VERSION(1, 2):
		case D3DPS_VERSION(1, 3):
		case D3DPS_VERSION(1, 4):
			if (pCaps && *pPixelShader > pCaps->PixelShaderVersion)
			{
				errorMessage = "Shader version not supported by caps.\n";
				break;
			}
			hr = S_OK;
			break;

		default:
			errorMessage = "Unsupported shader version.\n";
		}
	}

	if (!ReturnErrors)
	{
		errorMessage = "";
	}

	if (pErrorsString)
	{
		const size_t size = strlen(errorMessage) + 1;

		*pErrorsString = (char*) HeapAlloc(GetProcessHeap(), 0, size);
		if (*pErrorsString)
		{
			memcpy(*pErrorsString, errorMessage, size);
		}
	}

	return hr;
}

extern "C" HRESULT WINAPI ValidateVertexShader(const DWORD* pVertexShader, const DWORD* pVertexDecl, const D3DCAPS8* pCaps, BOOL ReturnErrors, char** pErrorsString)
{
	UNREFERENCED_PARAMETER(pVertexDecl);

#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "ValidateVertexShader " << "(" << pVertexShader << ", " << pVertexDecl << ", " << pCaps << ", " << ReturnErrors << ", " << pErrorsString << ")' ..." << std::endl;
#endif

	HRESULT hr = E_FAIL;
	const char* errorMessage = "";

	if (!pVertexShader)
	{
		errorMessage = "Invalid code pointer.\n";
	}
	else
	{
		switch (*pVertexShader)
		{
		case D3DVS_VERSION(1, 0):
		case D3DVS_VERSION(1, 1):
			if (pCaps && *pVertexShader > pCaps->VertexShaderVersion)
			{
				errorMessage = "Shader version not supported by caps.\n";
				break;
			}
			hr = S_OK;
			break;

		default:
			errorMessage = "Unsupported shader version.\n";
		}
	}

	if (!ReturnErrors)
	{
		errorMessage = "";
	}

	if (pErrorsString)
	{
		const size_t size = strlen(errorMessage) + 1;

		*pErrorsString = (char*) HeapAlloc(GetProcessHeap(), 0, size);
		if (*pErrorsString)
		{
			memcpy(*pErrorsString, errorMessage, size);
		}
	}

	return hr;
}

extern "C" void WINAPI DebugSetMute()
{
#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "DebugSetMute" << "(" << ")' ..." << std::endl;
#endif
}

extern "C" IDirect3D8 *WINAPI Direct3DCreate8(UINT SDKVersion)
{
#ifndef D3D8TO9NOLOG
	static bool LogMessageFlag = true;

	if (!LOG.is_open())
	{
		LOG.open("d3d8.log", std::ios::trunc);
	}

	if (!LOG.is_open() && LogMessageFlag)
	{
		LogMessageFlag = false;
		MessageBox(nullptr, TEXT("Failed to open debug log file \"d3d8.log\"!"), nullptr, MB_ICONWARNING);
	}

	LOG << "Redirecting '" << "Direct3DCreate8" << "(" << SDKVersion << ")' ..." << std::endl;
	LOG << "> Passing on to 'Direct3DCreate9':" << std::endl;
#endif

	IDirect3D9 *d3d = nullptr;
	IDirect3D9Ex *d3dEx = nullptr;
	const SystemD3D9Api& systemD3D9 = GetSystemD3D9();
	if (systemD3D9.create9Ex != nullptr &&
		SUCCEEDED(systemD3D9.create9Ex(D3D_SDK_VERSION, &d3dEx)) &&
		d3dEx != nullptr)
	{
		d3d = d3dEx;
	}
	else if (systemD3D9.create9 != nullptr)
	{
		d3d = systemD3D9.create9(D3D_SDK_VERSION);
	}

	if (d3d == nullptr)
	{
		return nullptr;
	}

	// Load D3DX
	if (!D3DXAssembleShader || !D3DXDisassembleShader || !D3DXGetShaderSize || !D3DXLoadSurfaceFromSurface)
	{
		const HMODULE module = LoadLibrary(TEXT("d3dx9_43.dll"));

		if (module != nullptr)
		{
			D3DXAssembleShader = reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(module, "D3DXAssembleShader"));
			D3DXDisassembleShader = reinterpret_cast<PFN_D3DXDisassembleShader>(GetProcAddress(module, "D3DXDisassembleShader"));
			D3DXGetShaderSize = reinterpret_cast<PFN_D3DXGetShaderSize>(GetProcAddress(module, "D3DXGetShaderSize"));
			D3DXLoadSurfaceFromSurface = reinterpret_cast<PFN_D3DXLoadSurfaceFromSurface>(GetProcAddress(module, "D3DXLoadSurfaceFromSurface"));
		}
		else
		{
#ifndef D3D8TO9NOLOG
			LOG << "Failed to load d3dx9_43.dll! Some features will not work correctly." << std::endl;
#endif
			if (MessageBox(nullptr, TEXT(
					"Failed to load d3dx9_43.dll! Some features will not work correctly.\n\n"
					"It's required to install the \"Microsoft DirectX End-User Runtime\" in order to use d3d8to9, or alternatively get the DLLs from this NuGet package:\nhttps://www.nuget.org/packages/Microsoft.DXSDK.D3DX\n\n"
					"Please click \"OK\" to open the official download page or \"Cancel\" to continue anyway."), nullptr, MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND | MB_OKCANCEL | MB_DEFBUTTON1) == IDOK)
			{
				ShellExecute(nullptr, TEXT("open"), TEXT("https://www.microsoft.com/download/details.aspx?id=35"), nullptr, nullptr, SW_SHOW);

				return nullptr;
			}
		}
	}

	return new Direct3D8(d3d);
}
