#include "client/MenuPointerOverlay.h"

#include "client/ControllerInputCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/MainMenuOverlayLayout.h"
#include "stereo/MainMenuScroll.h"
#include "stereo/UiPointerMath.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kBfMenuSetGameInputRva = 0x0005DE60;
constexpr std::size_t kBfMenuActiveIndexOffset = 0xFC;
constexpr std::size_t kBfMenuRef2SystemOffset = 0x144;
constexpr std::size_t kBfMenuMouseEnabledOffset = 0x148;
constexpr std::size_t kRef2PointerXOffset = 0x34;
constexpr std::size_t kRef2PointerYOffset = 0x38;
constexpr std::size_t kGameInputEnableMaskLowOffset = 0xC0;
constexpr DWORD kGameInputOk = 20;
constexpr DWORD kGameInputMouseLookX = 26;
constexpr DWORD kGameInputMouseLookY = 27;
constexpr int kBfMenuBattlefieldState = 0;
constexpr DWORD kControllerHandRight = 1;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr float kControllerTriggerPressThreshold = 0.60F;
constexpr float kControllerTriggerReleaseThreshold = 0.45F;
constexpr float kNativeMouseScale = 40.0F;
constexpr float kUiDistanceMeters = 1.5F;
constexpr float kUiWidthMeters = 1.6F;
constexpr UINT kLogicalCanvasWidth = 800;
constexpr UINT kLogicalCanvasHeight = 600;
constexpr BYTE kBfMenuSetGameInputPrefix[] = {
    0x83, 0xEC, 0x20, 0x53, 0x8B, 0xD9, 0x83, 0xBB,
    0xFC, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x84, 0xDC};
volatile LONG g_nativeMenuActiveState = 0;
volatile LONG g_mainMenuOverlayAvailable = 0;
volatile LONG g_mainMenuOverlayVisible = 0;
volatile LONG g_mainMenuOverlayHovered = 0;
SRWLOCK g_menuAnchorLock = SRWLOCK_INIT;
bfvr::stereo::Pose g_menuWorldAnchor = {};
bool g_menuWorldAnchorValid = false;

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t expectedLength) noexcept
{
    if (target == nullptr || expected == nullptr || expectedLength == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, expectedLength) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SendEscapeKeyPress() noexcept
{
    const HWND foregroundWindow = GetForegroundWindow();
    DWORD foregroundProcessId = 0;
    if (foregroundWindow == nullptr ||
        GetWindowThreadProcessId(
            foregroundWindow,
            &foregroundProcessId) == 0 ||
        foregroundProcessId != GetCurrentProcessId())
    {
        return false;
    }
    const UINT scanCode = MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC);
    std::array<INPUT, 2> inputs = {};
    for (INPUT& input : inputs)
    {
        input.type = INPUT_KEYBOARD;
        if (scanCode != 0)
        {
            input.ki.wScan = static_cast<WORD>(scanCode);
            input.ki.dwFlags = KEYEVENTF_SCANCODE;
        }
        else
        {
            input.ki.wVk = VK_ESCAPE;
        }
    }
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    return SendInput(
               static_cast<UINT>(inputs.size()),
               inputs.data(),
               sizeof(INPUT)) == static_cast<UINT>(inputs.size());
}

bool SendMouseWheelStep(int direction) noexcept
{
    if (direction == 0)
    {
        return false;
    }
    const HWND foregroundWindow = GetForegroundWindow();
    DWORD foregroundProcessId = 0;
    if (foregroundWindow == nullptr ||
        GetWindowThreadProcessId(
            foregroundWindow,
            &foregroundProcessId) == 0 ||
        foregroundProcessId != GetCurrentProcessId())
    {
        return false;
    }
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = static_cast<DWORD>(
        direction > 0 ? WHEEL_DELTA : -WHEEL_DELTA);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bfvr::stereo::Pose ToPose(
    const bfvr::D3D8RuntimeControllerPose& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

bfvr::stereo::Pose ToPose(
    const bfvr::D3D8RuntimeView& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

class MenuPointerOverlay
{
public:
    using SetGameInputFn =
        void(__thiscall*)(void* menu, float deltaTime, void* gameInput);

    void Start(
        void* image,
        UINT runtimeWidth,
        UINT runtimeHeight,
        UINT sourceWidth,
        UINT sourceHeight,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            return;
        }
        appendLog = log;
        triggerPressed = false;
        okPressedLastFrame = false;
        overlayVisibleLastFrame = false;
        scrollRepeat = {};
        if (runtimeWidth == 0 || runtimeHeight == 0 ||
            sourceWidth == 0 || sourceHeight == 0)
        {
            WriteLog(
                L"Controller menu pointer rejected invalid UI dimensions: runtime=%ux%u source=%ux%u.",
                runtimeWidth,
                runtimeHeight,
                sourceWidth,
                sourceHeight);
            InterlockedExchange(&started, 0);
            return;
        }

        runtimeUiWidth = runtimeWidth;
        runtimeUiHeight = runtimeHeight;
        sourceUiWidth = sourceWidth;
        sourceUiHeight = sourceHeight;
        gameImage = static_cast<std::byte*>(image);
        setGameInputTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kBfMenuSetGameInputRva;
        if (!HasExpectedPrefix(
                setGameInputTarget,
                kBfMenuSetGameInputPrefix,
                sizeof(kBfMenuSetGameInputPrefix)))
        {
            WriteLog(
                L"Controller menu pointer rejected profiled target %p: the WinPC BfMenu::setGameInput prefix differs.",
                setGameInputTarget);
            InterlockedExchange(&started, 0);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Controller menu pointer could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started, 0);
            return;
        }
        const MH_STATUS createStatus = MH_CreateHook(
            setGameInputTarget,
            reinterpret_cast<LPVOID>(&MenuPointerOverlay::SetGameInputHook),
            reinterpret_cast<LPVOID*>(&originalSetGameInput));
        if (createStatus != MH_OK || originalSetGameInput == nullptr)
        {
            WriteLog(
                L"Controller menu pointer could not create its BfMenu::setGameInput hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookCreated = true;
        InterlockedExchangePointer(&active, this);
        const MH_STATUS enableStatus = MH_EnableHook(setGameInputTarget);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Controller menu pointer could not enable its BfMenu::setGameInput hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookEnabled = true;
        WriteLog(
            L"Controller menu pointer armed at 0x0045DE60 for world-locked native menus: runtime=%ux%u source=%ux%u logical=800x600. The presentation path supplies the yaw-only LOCAL anchor shared by this mapper; a fresh tracked right aim ray supplies native c_GIMouseLookX/Y, and right trigger supplies native c_GIOk with hysteresis. In BfMenu Battlefield frontend state 0, right-stick up/down emits focus-checked mouse-wheel detents with bounded repeat while the Quick Menu is not held. The BFVR back-to-game button emits one focus-checked Escape scan-code down/up pair on a new click edge.",
            runtimeUiWidth,
            runtimeUiHeight,
            sourceUiWidth,
            sourceUiHeight);
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled)
        {
            MH_DisableHook(setGameInputTarget);
            hookEnabled = false;
        }
        while (InterlockedCompareExchange(&callbackEntrants, 0, 0) != 0)
        {
            Sleep(0);
        }
        InterlockedCompareExchangePointer(&active, nullptr, this);
        InterlockedExchange(&g_nativeMenuActiveState, 0);
        InterlockedExchange(&g_mainMenuOverlayVisible, 0);
        InterlockedExchange(&g_mainMenuOverlayHovered, 0);
        InterlockedExchange(&g_mainMenuOverlayAvailable, 0);
        WriteLog(
            L"Controller menu pointer report: calls=%ld nativeMenuFrames=%ld mainMenuFrames=%ld hoverFrames=%ld freshTracking=%ld rayHits=%ld applied=%ld wheelUp=%ld wheelDown=%ld wheelFailures=%ld escapePresses=%ld escapeFailures=%ld readFaults=%ld restoreFaults=%ld.",
            observedCalls,
            nativeMenuFrames,
            mainMenuFrames,
            hoverFrames,
            freshTrackingFrames,
            rayHits,
            appliedFrames,
            wheelUpSteps,
            wheelDownSteps,
            wheelFailures,
            escapePresses,
            escapeFailures,
            readFaults,
            restoreFaults);
        RemoveHook();
        InterlockedExchange(&started, 0);
    }

private:
    static void __fastcall SetGameInputHook(
        void* menu,
        void*,
        float deltaTime,
        void* gameInput)
    {
        InterlockedIncrement(&callbackEntrants);
        MenuPointerOverlay* const overlay =
            static_cast<MenuPointerOverlay*>(
                InterlockedCompareExchangePointer(
                    &active,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->originalSetGameInput != nullptr)
        {
            overlay->Dispatch(menu, deltaTime, gameInput);
        }
        InterlockedDecrement(&callbackEntrants);
    }

    void Dispatch(
        void* menu,
        float deltaTime,
        void* gameInput) noexcept
    {
        InterlockedIncrement(&observedCalls);
        bool nativeMenuActive = false;
        bool battlefieldFrontend = false;
        bool battlefieldMainMenu = false;
        int activeIndex = -1;
        float currentPointerX = 0.0F;
        float currentPointerY = 0.0F;
        __try
        {
            if (menu != nullptr && gameInput != nullptr)
            {
                void* const ref2System =
                    *reinterpret_cast<void**>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuRef2SystemOffset);
                activeIndex =
                    *reinterpret_cast<int*>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuActiveIndexOffset);
                battlefieldFrontend =
                    activeIndex == kBfMenuBattlefieldState &&
                    ref2System != nullptr;
                battlefieldMainMenu =
                    battlefieldFrontend &&
                    InterlockedCompareExchange(
                        &g_mainMenuOverlayAvailable,
                        0,
                        0) != 0;
                nativeMenuActive =
                    activeIndex != -1 &&
                    *reinterpret_cast<int*>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuMouseEnabledOffset) != 0 &&
                    ref2System != nullptr;
                if (nativeMenuActive)
                {
                    currentPointerX =
                        *reinterpret_cast<float*>(
                            static_cast<std::byte*>(ref2System) +
                            kRef2PointerXOffset);
                    currentPointerY =
                        *reinterpret_cast<float*>(
                            static_cast<std::byte*>(ref2System) +
                            kRef2PointerYOffset);
                    nativeMenuActive =
                        std::isfinite(currentPointerX) &&
                        std::isfinite(currentPointerY);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            nativeMenuActive = false;
            battlefieldFrontend = false;
            battlefieldMainMenu = false;
            InterlockedIncrement(&readFaults);
        }

        bfvr::D3D8RuntimeControllerSample sample = {};
        bfvr::D3D8RuntimeView matchingHead = {};
        LONG generation = 0;
        const bool fresh =
            nativeMenuActive &&
            bfvr::ReadFreshAcceptedWeaponTracking(
                sample,
                matchingHead,
                generation,
                kControllerSampleMaximumAgeMs);
        const bfvr::D3D8RuntimeControllerHand* right = fresh
            ? &sample.hands[kControllerHandRight]
            : nullptr;
        constexpr DWORD requiredAimFlags =
            bfvr::shared::kControllerHandFlagAimActive |
            bfvr::shared::kControllerHandFlagAimPositionValid |
            bfvr::shared::kControllerHandFlagAimOrientationValid |
            bfvr::shared::kControllerHandFlagAimPositionTracked |
            bfvr::shared::kControllerHandFlagAimOrientationTracked;
        const bool trackedAim =
            right != nullptr &&
            (right->flags & requiredAimFlags) == requiredAimFlags;
        if (trackedAim)
        {
            InterlockedIncrement(&freshTrackingFrames);
        }
        const bool thumbstickActive =
            right != nullptr &&
            (right->flags &
             bfvr::shared::kControllerHandFlagThumbstickActive) != 0;
        const bool quickMenuHeld =
            right != nullptr &&
            (right->buttons &
             bfvr::shared::kControllerHandButtonPrimary) != 0;
        const int requestedWheelDirection =
            bfvr::stereo::UpdateMainMenuScrollRepeat(
                scrollRepeat,
                battlefieldFrontend && nativeMenuActive && fresh &&
                    sample.valid && sample.sessionFocused &&
                    thumbstickActive && !quickMenuHeld,
                thumbstickActive ? right->thumbstickY : 0.0F,
                GetTickCount());

        bfvr::stereo::UiCanvasPoint canvasPoint = {};
        bool rayHit = false;
        if (trackedAim)
        {
            bfvr::stereo::Pose publishedAnchor = {};
            if (bfvr::TryGetActiveMenuWorldAnchor(publishedAnchor))
            {
                menuAnchorHead = publishedAnchor;
                menuAnchorValid = true;
            }
            else if (!menuAnchorValid)
            {
                const auto yawOnlyAnchor =
                    bfvr::stereo::MakeYawOnlyUiAnchor(
                        ToPose(matchingHead));
                if (yawOnlyAnchor.has_value())
                {
                    menuAnchorHead = *yawOnlyAnchor;
                    menuAnchorValid = true;
                }
            }
            const auto relativeAim = menuAnchorValid
                ? bfvr::stereo::MakePoseRelativeToReference(
                    menuAnchorHead,
                    ToPose(right->aimPose))
                : std::nullopt;
            const float quadHeight =
                kUiWidthMeters *
                static_cast<float>(runtimeUiHeight) /
                static_cast<float>(runtimeUiWidth);
            const bfvr::stereo::Pose quadPose = {
                {0.0F, 0.0F, -kUiDistanceMeters},
                {0.0F, 0.0F, 0.0F, 1.0F}};
            const auto mapped = relativeAim.has_value()
                ? bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
                    *relativeAim,
                    quadPose,
                    kUiWidthMeters,
                    quadHeight,
                    runtimeUiWidth,
                    runtimeUiHeight,
                    sourceUiWidth,
                    sourceUiHeight,
                    kLogicalCanvasWidth,
                    kLogicalCanvasHeight)
                : std::nullopt;
            if (mapped.has_value())
            {
                canvasPoint = *mapped;
                rayHit = true;
                InterlockedIncrement(&rayHits);
            }
        }

        float backupOk = 0.0F;
        float backupMouseX = 0.0F;
        float backupMouseY = 0.0F;
        DWORD backupMask = 0;
        bool frameBackedUp = false;
        bool modified = false;
        bool requestEscape = false;
        bool hovered = false;
        if (nativeMenuActive)
        {
            __try
            {
                float* const values =
                    reinterpret_cast<float*>(gameInput);
                DWORD* const enableMask =
                    reinterpret_cast<DWORD*>(
                        static_cast<std::byte*>(gameInput) +
                        kGameInputEnableMaskLowOffset);
                backupOk = values[kGameInputOk];
                backupMouseX = values[kGameInputMouseLookX];
                backupMouseY = values[kGameInputMouseLookY];
                backupMask = *enableMask;
                frameBackedUp = true;

                float nextPointerX = currentPointerX;
                float nextPointerY = currentPointerY;
                if (rayHit)
                {
                    const float desiredX =
                        canvasPoint.pixelX -
                        static_cast<float>(kLogicalCanvasWidth) * 0.5F;
                    const float desiredY =
                        canvasPoint.pixelY -
                        static_cast<float>(kLogicalCanvasHeight) * 0.5F;
                    values[kGameInputMouseLookX] =
                        (desiredX - currentPointerX) / kNativeMouseScale;
                    values[kGameInputMouseLookY] =
                        (desiredY - currentPointerY) / kNativeMouseScale;
                    *enableMask |=
                        (1UL << kGameInputMouseLookX) |
                        (1UL << kGameInputMouseLookY);
                    nextPointerX = desiredX;
                    nextPointerY = desiredY;

                    if ((right->flags &
                         bfvr::shared::kControllerHandFlagTriggerActive) != 0)
                    {
                        if (triggerPressed)
                        {
                            triggerPressed =
                                right->triggerValue >
                                kControllerTriggerReleaseThreshold;
                        }
                        else
                        {
                            triggerPressed =
                                right->triggerValue >=
                                kControllerTriggerPressThreshold;
                        }
                        const bool nativeOkActive =
                            (backupMask & (1UL << kGameInputOk)) != 0 &&
                            std::isfinite(backupOk) &&
                            backupOk > 0.0F;
                        values[kGameInputOk] =
                            triggerPressed || nativeOkActive ? 1.0F : 0.0F;
                        *enableMask |= 1UL << kGameInputOk;
                    }
                    modified = true;
                    InterlockedIncrement(&appliedFrames);
                }
                else
                {
                    if ((backupMask &
                         (1UL << kGameInputMouseLookX)) != 0 &&
                        std::isfinite(backupMouseX))
                    {
                        nextPointerX = std::clamp(
                            currentPointerX +
                                backupMouseX * kNativeMouseScale,
                            -400.0F,
                            400.0F);
                    }
                    if ((backupMask &
                         (1UL << kGameInputMouseLookY)) != 0 &&
                        std::isfinite(backupMouseY))
                    {
                        nextPointerY = std::clamp(
                            currentPointerY +
                                backupMouseY * kNativeMouseScale,
                            -300.0F,
                            300.0F);
                    }
                }

                const float pointerCanvasX = nextPointerX + 400.0F;
                const float pointerCanvasY = nextPointerY + 300.0F;
                hovered = battlefieldMainMenu &&
                    bfvr::stereo::IsInsideUiCanvasRect(
                        bfvr::stereo::BackToGameButtonRect(),
                        pointerCanvasX,
                        pointerCanvasY);
                const bool okDown =
                    (*enableMask & (1UL << kGameInputOk)) != 0 &&
                    std::isfinite(values[kGameInputOk]) &&
                    values[kGameInputOk] > 0.0F;
                requestEscape =
                    bfvr::stereo::ConsumeUiButtonPressEdge(
                        battlefieldMainMenu,
                        hovered,
                        okDown,
                        okPressedLastFrame);
                if (requestEscape)
                {
                    // Ref2 must not also activate any native element under the
                    // BFVR-owned button. Restore the caller's frame afterward.
                    values[kGameInputOk] = 0.0F;
                    *enableMask |= 1UL << kGameInputOk;
                    modified = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Fail closed before the game sees a partially modified frame.
                if (frameBackedUp)
                {
                    __try
                    {
                        float* const values =
                            reinterpret_cast<float*>(gameInput);
                        values[kGameInputOk] = backupOk;
                        values[kGameInputMouseLookX] = backupMouseX;
                        values[kGameInputMouseLookY] = backupMouseY;
                        *reinterpret_cast<DWORD*>(
                            static_cast<std::byte*>(gameInput) +
                            kGameInputEnableMaskLowOffset) = backupMask;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        InterlockedIncrement(&restoreFaults);
                    }
                }
                modified = false;
                requestEscape = false;
                hovered = false;
                InterlockedIncrement(&readFaults);
            }
        }
        else
        {
            okPressedLastFrame = false;
            triggerPressed = false;
        }
        if (nativeMenuActive)
        {
            InterlockedIncrement(&nativeMenuFrames);
        }
        else
        {
            menuAnchorValid = false;
            bfvr::ClearActiveMenuWorldAnchor();
        }
        InterlockedExchange(
            &g_nativeMenuActiveState,
            nativeMenuActive ? 1 : 0);
        InterlockedExchange(
            &g_mainMenuOverlayVisible,
            battlefieldMainMenu ? 1 : 0);
        InterlockedExchange(
            &g_mainMenuOverlayHovered,
            hovered ? 1 : 0);
        if (battlefieldMainMenu != overlayVisibleLastFrame)
        {
            overlayVisibleLastFrame = battlefieldMainMenu;
            WriteLog(
                battlefieldMainMenu
                    ? L"Back-to-game overlay became visible on BfMenu state %d; the next published Ref2 frame will composite it in the x64 presenter."
                    : L"Back-to-game overlay became hidden after BfMenu changed to state %d.",
                activeIndex);
        }
        if (battlefieldMainMenu)
        {
            InterlockedIncrement(&mainMenuFrames);
        }
        if (hovered)
        {
            InterlockedIncrement(&hoverFrames);
        }

        originalSetGameInput(menu, deltaTime, gameInput);

        if (modified)
        {
            __try
            {
                float* const values =
                    reinterpret_cast<float*>(gameInput);
                values[kGameInputOk] = backupOk;
                values[kGameInputMouseLookX] = backupMouseX;
                values[kGameInputMouseLookY] = backupMouseY;
                *reinterpret_cast<DWORD*>(
                    static_cast<std::byte*>(gameInput) +
                    kGameInputEnableMaskLowOffset) = backupMask;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                InterlockedIncrement(&restoreFaults);
            }
        }
        if (requestEscape)
        {
            if (SendEscapeKeyPress())
            {
                InterlockedIncrement(&escapePresses);
            }
            else
            {
                InterlockedIncrement(&escapeFailures);
            }
        }
        if (requestedWheelDirection != 0)
        {
            if (SendMouseWheelStep(requestedWheelDirection))
            {
                InterlockedIncrement(
                    requestedWheelDirection > 0
                        ? &wheelUpSteps
                        : &wheelDownSteps);
            }
            else
            {
                InterlockedIncrement(&wheelFailures);
            }
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active, nullptr, this);
        if (hookEnabled)
        {
            MH_DisableHook(setGameInputTarget);
            hookEnabled = false;
        }
        if (hookCreated)
        {
            MH_RemoveHook(setGameInputTarget);
            hookCreated = false;
        }
        originalSetGameInput = nullptr;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
        {
            return;
        }
        wchar_t message[1200] = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message,
            std::size(message),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog(message);
    }

    static PVOID volatile active;
    static volatile LONG callbackEntrants;
    volatile LONG started = 0;
    volatile LONG observedCalls = 0;
    volatile LONG nativeMenuFrames = 0;
    volatile LONG mainMenuFrames = 0;
    volatile LONG hoverFrames = 0;
    volatile LONG freshTrackingFrames = 0;
    volatile LONG rayHits = 0;
    volatile LONG appliedFrames = 0;
    volatile LONG wheelUpSteps = 0;
    volatile LONG wheelDownSteps = 0;
    volatile LONG wheelFailures = 0;
    volatile LONG escapePresses = 0;
    volatile LONG escapeFailures = 0;
    volatile LONG readFaults = 0;
    volatile LONG restoreFaults = 0;
    std::byte* gameImage = nullptr;
    void* setGameInputTarget = nullptr;
    SetGameInputFn originalSetGameInput = nullptr;
    UINT runtimeUiWidth = 0;
    UINT runtimeUiHeight = 0;
    UINT sourceUiWidth = 0;
    UINT sourceUiHeight = 0;
    void (*appendLog)(const wchar_t* message) = nullptr;
    bool triggerPressed = false;
    bool okPressedLastFrame = false;
    bool overlayVisibleLastFrame = false;
    bool menuAnchorValid = false;
    bfvr::stereo::MainMenuScrollRepeatState scrollRepeat = {};
    bfvr::stereo::Pose menuAnchorHead = {};
    bool ownsMinHook = false;
    bool hookCreated = false;
    bool hookEnabled = false;
};

PVOID volatile MenuPointerOverlay::active = nullptr;
volatile LONG MenuPointerOverlay::callbackEntrants = 0;
MenuPointerOverlay g_menuPointerOverlay = {};

} // namespace

namespace bfvr
{

void StartMenuPointerOverlay(
    void* gameImage,
    UINT runtimeUiWidth,
    UINT runtimeUiHeight,
    UINT sourceUiWidth,
    UINT sourceUiHeight,
    void (*appendLog)(const wchar_t* message))
{
    g_menuPointerOverlay.Start(
        gameImage,
        runtimeUiWidth,
        runtimeUiHeight,
        sourceUiWidth,
        sourceUiHeight,
        appendLog);
}

void StopMenuPointerOverlay()
{
    g_menuPointerOverlay.Stop();
}

bool IsMenuPointerOverlayActive() noexcept
{
    return InterlockedCompareExchange(
        &g_nativeMenuActiveState,
        0,
        0) != 0;
}

MainMenuOverlayInteractionState
GetMainMenuOverlayInteractionState() noexcept
{
    MainMenuOverlayInteractionState state = {};
    state.visible = InterlockedCompareExchange(
        &g_mainMenuOverlayVisible,
        0,
        0) != 0;
    state.hovered = state.visible &&
        InterlockedCompareExchange(
            &g_mainMenuOverlayHovered,
            0,
            0) != 0;
    return state;
}

void SetMainMenuOverlayAvailable(bool available) noexcept
{
    InterlockedExchange(
        &g_mainMenuOverlayAvailable,
        available ? 1 : 0);
    if (!available)
    {
        InterlockedExchange(&g_mainMenuOverlayVisible, 0);
        InterlockedExchange(&g_mainMenuOverlayHovered, 0);
    }
}

void PublishActiveMenuWorldAnchor(const stereo::Pose& anchor) noexcept
{
    AcquireSRWLockExclusive(&g_menuAnchorLock);
    g_menuWorldAnchor = anchor;
    g_menuWorldAnchorValid = true;
    ReleaseSRWLockExclusive(&g_menuAnchorLock);
}

void ClearActiveMenuWorldAnchor() noexcept
{
    AcquireSRWLockExclusive(&g_menuAnchorLock);
    g_menuWorldAnchor = {};
    g_menuWorldAnchorValid = false;
    ReleaseSRWLockExclusive(&g_menuAnchorLock);
}

bool TryGetActiveMenuWorldAnchor(stereo::Pose& anchor) noexcept
{
    AcquireSRWLockShared(&g_menuAnchorLock);
    const bool valid = g_menuWorldAnchorValid;
    if (valid)
    {
        anchor = g_menuWorldAnchor;
    }
    ReleaseSRWLockShared(&g_menuAnchorLock);
    return valid;
}

} // namespace bfvr
