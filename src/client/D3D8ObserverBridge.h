#pragma once

#include <windows.h>

namespace bfvr
{

// Shared, read-only bridge from the lifecycle observer in BFVRClient into
// isolated D3D8 diagnostics. Individual diagnostics own their hooks and POD
// records; the client exposes only already-observed lifecycle state.
struct D3D8ObserverLifecycle
{
    void* device = nullptr;
    DWORD deviceThreadId = 0;
    BOOL presentationReadable = FALSE;
    UINT backBufferWidth = 0;
    UINT backBufferHeight = 0;
};

struct D3D8ObserverCallbacks
{
    BOOL (*tryGetReadyLifecycle)(D3D8ObserverLifecycle* lifecycle) = nullptr;
    BOOL (*isCaptureEligible)() = nullptr;
    void (*appendLog)(const wchar_t* message) = nullptr;
    void (*signalCompletion)() = nullptr;
};

} // namespace bfvr
