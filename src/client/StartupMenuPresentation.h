#pragma once

#include "client/D3D8SharedPresentationBridge.h"
#include "stereo/UiPointerMath.h"

#include <windows.h>

#include <vector>

namespace bfvr
{

// Starts OpenXR before BF1942 creates its gameplay D3D8 device and presents a
// world-locked capture of the native startup menu. The normal GPU-resident
// stereo bridge replaces this bounded CPU menu bridge once the device exists.
class StartupMenuPresentation
{
public:
    bool Start(
        void* gameImage,
        D3D8SharedPresentationLogCallback appendLog);
    void Pump();
    void Stop();

    [[nodiscard]] bool IsActive() const noexcept;

private:
    static BOOL CALLBACK FindProcessWindow(HWND window, LPARAM context);
    bool EnsureCaptureResources();
    bool CaptureGameWindow();
    void ReleaseCaptureResources();

    D3D8SharedPresentationBridge bridge_;
    D3D8SharedPresentationLogCallback appendLog_ = nullptr;
    D3D8RuntimeRenderRequest request_ = {};
    stereo::UiMenuAnchorTracker menuAnchorTracker_ = {};
    std::vector<DWORD> leftPixels_;
    std::vector<DWORD> rightPixels_;
    std::vector<DWORD> uiPixels_;
    HWND gameWindow_ = nullptr;
    HDC captureDc_ = nullptr;
    HBITMAP captureBitmap_ = nullptr;
    HGDIOBJ previousCaptureBitmap_ = nullptr;
    void* captureBits_ = nullptr;
    UINT sourceWidth_ = 0;
    UINT sourceHeight_ = 0;
    DWORD nextFrameAt_ = 0;
    bool active_ = false;
    bool captureLogged_ = false;
};

} // namespace bfvr
