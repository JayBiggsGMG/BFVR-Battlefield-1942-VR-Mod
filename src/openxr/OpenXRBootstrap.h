#pragma once

namespace bfvr
{
using OpenXRLogCallback = void (*)(void* context, const wchar_t* message);

// This enumerates runtime graphics extensions, creates only an OpenXR instance,
// and performs a head-mounted-display system query. When an HMD and the D3D11
// extension are available, it also reads the runtime's D3D11 graphics
// requirements. It intentionally creates no session, graphics binding,
// D3D11 device, swapchain, input action, or composition layer.
bool ProbeOpenXRRuntime(const wchar_t* payloadDirectory, OpenXRLogCallback logCallback, void* logContext);
}
