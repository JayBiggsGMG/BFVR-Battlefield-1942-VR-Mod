#include "client/StartupMenuPresentation.h"

#include "client/MenuPointerOverlay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace
{

constexpr DWORD kStartupFrameIntervalMs = 66;
constexpr DWORD kOpaqueBlack = 0xFF000000U;
constexpr DWORD kOpaqueFallback = 0xFF101418U;
constexpr float kMenuFollowStartRadians = 0.610865238F;
constexpr float kMenuFollowRadiansPerSecond = 1.570796327F;

bool MakePixelBuffer(
    UINT width,
    UINT height,
    DWORD color,
    std::vector<DWORD>& pixels)
{
    if (width == 0 || height == 0 ||
        static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max() /
                sizeof(DWORD)))
    {
        return false;
    }
    pixels.assign(
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height),
        color);
    return !pixels.empty();
}

} // namespace

namespace bfvr
{

bool StartupMenuPresentation::Start(
    void* gameImage,
    D3D8SharedPresentationLogCallback appendLog)
{
    Stop();
    appendLog_ = appendLog;
    sourceWidth_ = static_cast<UINT>(
        std::max(GetSystemMetrics(SM_CXSCREEN), 640));
    sourceHeight_ = static_cast<UINT>(
        std::max(GetSystemMetrics(SM_CYSCREEN), 480));
    // The world eyes are only an opaque black backing plane during startup.
    // Half-size sources avoid needless CPU traffic; the menu source retains
    // the native desktop/client resolution.
    if (!bridge_.Initialize(
            sourceWidth_,
            sourceHeight_,
            0.50F,
            D3D8PresentationCompanion::OpenXR,
            appendLog_,
            true) ||
        !MakePixelBuffer(
            bridge_.LeftWorldWidth(),
            bridge_.LeftWorldHeight(),
            kOpaqueBlack,
            leftPixels_) ||
        !MakePixelBuffer(
            bridge_.RightWorldWidth(),
            bridge_.RightWorldHeight(),
            kOpaqueBlack,
            rightPixels_) ||
        !MakePixelBuffer(
            bridge_.UiWidth(),
            bridge_.UiHeight(),
            kOpaqueFallback,
            uiPixels_))
    {
        Stop();
        return false;
    }

    StartMenuPointerOverlay(
        gameImage,
        bridge_.RuntimeUiWidth(),
        bridge_.RuntimeUiHeight(),
        sourceWidth_,
        sourceHeight_,
        appendLog_);
    active_ = true;
    if (appendLog_ != nullptr)
    {
        appendLog_(
            L"Launch-time OpenXR menu bridge is active before BF1942 creates its D3D8 device. It presents the native game window on a world-locked Ref2 panel and will hand off to the GPU-resident stereo bridge when CreateDevice/Present become available.");
    }
    return true;
}

void StartupMenuPresentation::Pump()
{
    if (!active_)
    {
        return;
    }
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - nextFrameAt_) < 0)
    {
        return;
    }
    nextFrameAt_ = now + kStartupFrameIntervalMs;
    if (!bridge_.RequestRender(request_, 0))
    {
        return;
    }

    CaptureGameWindow();
    const std::array<D3D8SharedFramePixels, 3> frame = {{
        {
            leftPixels_.data(),
            bridge_.LeftWorldWidth() * sizeof(DWORD),
            bridge_.LeftWorldWidth(),
            bridge_.LeftWorldHeight()},
        {
            rightPixels_.data(),
            bridge_.RightWorldWidth() * sizeof(DWORD),
            bridge_.RightWorldWidth(),
            bridge_.RightWorldHeight()},
        {
            uiPixels_.data(),
            bridge_.UiWidth() * sizeof(DWORD),
            bridge_.UiWidth(),
            bridge_.UiHeight()}}};
    D3D8RuntimeUiPlacement uiPlacement = {};
    const MainMenuOverlayInteractionState overlayState =
        GetMainMenuOverlayInteractionState();
    uiPlacement.backToGameVisible = overlayState.visible;
    uiPlacement.backToGameHovered = overlayState.hovered;
    // Preserve the historical world-static startup panel until BF1942 reports
    // a real native menu, then replace its fallback latch with the same
    // yaw-only edge anchor used after the D3D8 handoff.
    uiPlacement.headLocked = false;
    const bool nativeMenuActive = IsMenuPointerOverlayActive();
    if (nativeMenuActive)
    {
        const D3D8RuntimeView currentHead =
            MakeD3D8RuntimeHeadReference(request_);
        const stereo::Pose currentHeadPose = {
            {currentHead.positionX, currentHead.positionY, currentHead.positionZ},
            {
                currentHead.orientationX,
                currentHead.orientationY,
                currentHead.orientationZ,
                currentHead.orientationW}};
        if (stereo::UpdateUiMenuAnchor(
                menuAnchorTracker_,
                currentHeadPose,
                request_.predictedDisplayTime,
                kMenuFollowStartRadians,
                kMenuFollowRadiansPerSecond))
        {
            const stereo::Pose& anchor = menuAnchorTracker_.anchor;
            uiPlacement.worldAnchorValid = true;
            uiPlacement.worldAnchor.orientationX = anchor.orientation.x;
            uiPlacement.worldAnchor.orientationY = anchor.orientation.y;
            uiPlacement.worldAnchor.orientationZ = anchor.orientation.z;
            uiPlacement.worldAnchor.orientationW = anchor.orientation.w;
            uiPlacement.worldAnchor.positionX = anchor.position.x;
            uiPlacement.worldAnchor.positionY = anchor.position.y;
            uiPlacement.worldAnchor.positionZ = anchor.position.z;
            PublishActiveMenuWorldAnchor(anchor);
        }
        else
        {
            uiPlacement.headLocked = true;
            ClearActiveMenuWorldAnchor();
        }
    }
    else
    {
        stereo::ResetUiMenuAnchor(menuAnchorTracker_);
        ClearActiveMenuWorldAnchor();
    }
    if (bridge_.PublishFrame(
            request_,
            frame,
            uiPlacement,
            D3D8RuntimeMovementFrame{}))
    {
        bridge_.WaitForPresentation(request_.sequence, 1000);
    }
}

void StartupMenuPresentation::Stop()
{
    if (active_)
    {
        StopMenuPointerOverlay();
    }
    active_ = false;
    stereo::ResetUiMenuAnchor(menuAnchorTracker_);
    ClearActiveMenuWorldAnchor();
    bridge_.Shutdown();
    ReleaseCaptureResources();
    leftPixels_.clear();
    rightPixels_.clear();
    uiPixels_.clear();
    request_ = {};
    gameWindow_ = nullptr;
    sourceWidth_ = 0;
    sourceHeight_ = 0;
    nextFrameAt_ = 0;
    captureLogged_ = false;
    appendLog_ = nullptr;
}

bool StartupMenuPresentation::IsActive() const noexcept
{
    return active_;
}

BOOL CALLBACK StartupMenuPresentation::FindProcessWindow(
    HWND window,
    LPARAM context)
{
    auto* const presentation =
        reinterpret_cast<StartupMenuPresentation*>(context);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    RECT client = {};
    if (presentation != nullptr &&
        processId == GetCurrentProcessId() &&
        IsWindowVisible(window) &&
        GetClientRect(window, &client) &&
        client.right > client.left &&
        client.bottom > client.top)
    {
        presentation->gameWindow_ = window;
        return FALSE;
    }
    return TRUE;
}

bool StartupMenuPresentation::EnsureCaptureResources()
{
    if (captureDc_ != nullptr &&
        captureBitmap_ != nullptr &&
        captureBits_ != nullptr &&
        previousCaptureBitmap_ != nullptr &&
        previousCaptureBitmap_ != HGDI_ERROR)
    {
        return true;
    }
    ReleaseCaptureResources();
    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr)
    {
        return false;
    }
    captureDc_ = CreateCompatibleDC(screenDc);
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(sourceWidth_);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(sourceHeight_);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    captureBitmap_ = CreateDIBSection(
        screenDc,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &captureBits_,
        nullptr,
        0);
    ReleaseDC(nullptr, screenDc);
    if (captureDc_ == nullptr ||
        captureBitmap_ == nullptr ||
        captureBits_ == nullptr)
    {
        ReleaseCaptureResources();
        return false;
    }
    previousCaptureBitmap_ =
        SelectObject(captureDc_, captureBitmap_);
    return previousCaptureBitmap_ != nullptr &&
        previousCaptureBitmap_ != HGDI_ERROR;
}

bool StartupMenuPresentation::CaptureGameWindow()
{
    if (gameWindow_ == nullptr || !IsWindow(gameWindow_))
    {
        gameWindow_ = nullptr;
        EnumWindows(
            &StartupMenuPresentation::FindProcessWindow,
            reinterpret_cast<LPARAM>(this));
    }
    if (!EnsureCaptureResources())
    {
        return false;
    }

    RECT client = {};
    const bool captureClient = gameWindow_ != nullptr;
    HDC windowDc = GetDC(captureClient ? gameWindow_ : nullptr);
    const bool haveClient =
        windowDc != nullptr &&
        (!captureClient ||
         GetClientRect(gameWindow_, &client)) &&
        (!captureClient ||
        client.right > client.left &&
         client.bottom > client.top);
    bool captured = false;
    if (haveClient)
    {
        SetStretchBltMode(captureDc_, COLORONCOLOR);
        captured = StretchBlt(
            captureDc_,
            0,
            0,
            static_cast<int>(sourceWidth_),
            static_cast<int>(sourceHeight_),
            windowDc,
            0,
            0,
            captureClient
                ? client.right - client.left
                : static_cast<int>(sourceWidth_),
            captureClient
                ? client.bottom - client.top
                : static_cast<int>(sourceHeight_),
            SRCCOPY) != FALSE;
    }
    if (windowDc != nullptr)
    {
        ReleaseDC(captureClient ? gameWindow_ : nullptr, windowDc);
    }
    if (!captured)
    {
        return false;
    }

    GdiFlush();
    const auto* const capturedPixels =
        static_cast<const DWORD*>(captureBits_);
    for (std::size_t index = 0; index < uiPixels_.size(); ++index)
    {
        uiPixels_[index] = capturedPixels[index] | 0xFF000000U;
    }
    if (!captureLogged_ && appendLog_ != nullptr)
    {
        captureLogged_ = true;
        appendLog_(
            captureClient
                ? L"Launch-time OpenXR bridge captured the native BF1942 client area successfully; startup menu frames are now visible on the world-locked panel."
                : L"Launch-time OpenXR bridge captured the foreground desktop while waiting for the native BF1942 window; startup frames are visible on the world-locked panel.");
    }
    return true;
}

void StartupMenuPresentation::ReleaseCaptureResources()
{
    if (captureDc_ != nullptr &&
        previousCaptureBitmap_ != nullptr &&
        previousCaptureBitmap_ != HGDI_ERROR)
    {
        SelectObject(captureDc_, previousCaptureBitmap_);
    }
    previousCaptureBitmap_ = nullptr;
    if (captureBitmap_ != nullptr)
    {
        DeleteObject(captureBitmap_);
    }
    captureBitmap_ = nullptr;
    captureBits_ = nullptr;
    if (captureDc_ != nullptr)
    {
        DeleteDC(captureDc_);
    }
    captureDc_ = nullptr;
}

} // namespace bfvr
