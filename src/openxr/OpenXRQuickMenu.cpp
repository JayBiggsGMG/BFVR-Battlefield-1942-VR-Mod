#include "openxr/OpenXRQuickMenu.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cwchar>
#include <utility>

namespace
{
template <typename T>
void ReleaseInterface(T*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

std::wstring JoinPath(
    const wchar_t* directory,
    const wchar_t* child)
{
    if (directory == nullptr || directory[0] == L'\0' || child == nullptr)
    {
        return {};
    }
    std::wstring result = directory;
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(child);
    return result;
}

bfvr::stereo::Pose ToPose(
    const bfvr::OpenXRPresentationPose& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

XrPosef ToXrPose(const bfvr::stereo::Pose& source) noexcept
{
    XrPosef result = {};
    result.orientation = {
        source.orientation.x,
        source.orientation.y,
        source.orientation.z,
        source.orientation.w};
    result.position = {
        source.position.x,
        source.position.y,
        source.position.z};
    return result;
}

bfvr::OpenXRPresentationPose ToPresentationPose(
    const bfvr::stereo::Pose& source) noexcept
{
    bfvr::OpenXRPresentationPose result = {};
    result.orientationX = source.orientation.x;
    result.orientationY = source.orientation.y;
    result.orientationZ = source.orientation.z;
    result.orientationW = source.orientation.w;
    result.positionX = source.position.x;
    result.positionY = source.position.y;
    result.positionZ = source.position.z;
    return result;
}

bool IsBgraFormat(DXGI_FORMAT format) noexcept
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

bool IsRgbaFormat(DXGI_FORMAT format) noexcept
{
    return format == DXGI_FORMAT_R8G8B8A8_UNORM ||
        format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

std::uint32_t SwapRedBlue(std::uint32_t bgra) noexcept
{
    return (bgra & 0xFF00FF00U) |
        ((bgra & 0x000000FFU) << 16U) |
        ((bgra & 0x00FF0000U) >> 16U);
}

std::size_t UtilityHoverIndex(
    bfvr::stereo::QuickMenuSelection selection) noexcept
{
    if (!bfvr::stereo::IsQuickMenuUtilitySelection(selection))
    {
        return 0;
    }
    return 1 + static_cast<std::size_t>(selection) -
        static_cast<std::size_t>(
            bfvr::stereo::QuickMenuSelection::MountedCameraDecouple);
}

std::size_t UtilityVisualIndex(
    bool mountedCameraDecoupled,
    bfvr::stereo::QuickMenuSelection hovered) noexcept
{
    return (mountedCameraDecoupled ? 4U : 0U) +
        UtilityHoverIndex(hovered);
}
} // namespace

namespace bfvr
{

OpenXRQuickMenu::~OpenXRQuickMenu()
{
    Shutdown();
}

bool OpenXRQuickMenu::Initialize(
    const wchar_t* payloadDirectory,
    XrSession session,
    DXGI_FORMAT swapchainFormat,
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const OpenXRQuickMenuApi& api,
    float nativeMenuWidthMeters,
    float nativeMenuDistanceMeters,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (payloadDirectory == nullptr || session == XR_NULL_HANDLE ||
        device == nullptr || context == nullptr ||
        (!IsBgraFormat(swapchainFormat) && !IsRgbaFormat(swapchainFormat)) ||
        api.createSwapchain == nullptr || api.destroySwapchain == nullptr ||
        api.enumerateSwapchainImages == nullptr ||
        api.acquireSwapchainImage == nullptr ||
        api.waitSwapchainImage == nullptr ||
        api.releaseSwapchainImage == nullptr)
    {
        WriteLog(
            L"Quick Menu received incomplete OpenXR/D3D11 initialization state.");
        return false;
    }
    api_ = api;
    session_ = session;
    swapchainFormat_ = swapchainFormat;
    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();

    userSettingsRuntime_ = &settings::ProcessUserSettingsRuntime();
    const settings::UserSettingsLoadStatus startupSettingsStatus =
        userSettingsRuntime_->Initialize(payloadDirectory);
    startupSettingsValues_ = settings::DecodeUserSettings(
        userSettingsRuntime_->Current());
    WriteLog(
        L"BFVR startup user configuration selected %s at %s.",
        settings::UserSettingsLoadStatusName(startupSettingsStatus),
        userSettingsRuntime_->Store().Path().empty()
            ? L"<unavailable path>"
            : userSettingsRuntime_->Store().Path().c_str());

    QuickMenuArt art;
    const std::wstring assetsDirectory = JoinPath(
        payloadDirectory,
        L"assets");
    const std::wstring settingsDirectory = JoinPath(
        assetsDirectory.c_str(),
        L"SettingsMenu");
    if (!art.InitializeFromDirectory(
            assetsDirectory,
            logCallback_,
            logContext_))
    {
        Shutdown();
        return false;
    }
    settingsInteraction_.Configure(
        nativeMenuWidthMeters * stereo::kSettingsMenuNativeWidthScale,
        std::max(
            nativeMenuDistanceMeters -
                stereo::kSettingsMenuForwardOffsetMeters,
            0.05F));

    bool resourcesReady = true;
    for (std::size_t index = 0;
         resourcesReady && index < menuSources_.size();
         ++index)
    {
        std::vector<std::uint32_t> pixels;
        UINT width = 0;
        UINT height = 0;
        resourcesReady = art.CopyMenuPixels(
                static_cast<stereo::QuickMenuSelection>(index),
                pixels,
                width,
                height) &&
            CreateSourceTexture(
                pixels,
                width,
                height,
                &menuSources_[index]);
    }
    for (std::size_t active = 0;
         resourcesReady && active < 2;
         ++active)
    {
        for (std::size_t hover = 0;
             resourcesReady && hover < 4;
             ++hover)
        {
            const stereo::QuickMenuSelection selection = hover == 0
                ? stereo::QuickMenuSelection::None
                : static_cast<stereo::QuickMenuSelection>(
                    static_cast<std::uint32_t>(
                        stereo::QuickMenuSelection::MountedCameraDecouple) +
                    static_cast<std::uint32_t>(hover - 1));
            std::vector<std::uint32_t> pixels;
            UINT width = 0;
            UINT height = 0;
            const std::size_t index = active * 4 + hover;
            resourcesReady = art.CopyUtilityStripPixels(
                    selection,
                    active != 0,
                    pixels,
                    width,
                    height) &&
                CreateSourceTexture(
                    pixels,
                    width,
                    height,
                    &utilitySources_[index]);
        }
    }
    std::vector<std::uint32_t> cursorPixels;
    UINT cursorWidth = 0;
    UINT cursorHeight = 0;
    resourcesReady = resourcesReady &&
        art.CopyCursorPixels(
            cursorPixels,
            cursorWidth,
            cursorHeight) &&
        CreateSourceTexture(
            cursorPixels,
            cursorWidth,
            cursorHeight,
            &cursorSource_) &&
        CreateSwapchain(
            menuSwapchain_,
            stereo::kQuickMenuTextureSize,
            stereo::kQuickMenuTextureSize) &&
        CreateSwapchain(
            utilitySwapchain_,
            stereo::kQuickMenuUtilityTextureWidth,
            stereo::kQuickMenuUtilityTextureHeight) &&
        CreateSwapchain(cursorSwapchain_, cursorWidth, cursorHeight);
    art.Reset();
    if (!resourcesReady || !IsReady())
    {
        WriteLog(L"Quick Menu could not create its D3D11/OpenXR resources.");
        Shutdown();
        return false;
    }

    std::vector<std::uint32_t> settingsPixels;
    UINT settingsWidth = 0;
    UINT settingsHeight = 0;
    settingsAvailable_ =
        settingsArt_.InitializeFromDirectory(
            settingsDirectory,
            logCallback_,
            logContext_) &&
        settingsArt_.Compose(
            settingsInteraction_.Snapshot(),
            settingsPixels,
            settingsWidth,
            settingsHeight) &&
        CreateMutableSourceTexture(
            settingsPixels,
            settingsWidth,
            settingsHeight,
            &settingsSource_) &&
        CreateSwapchain(
            settingsSwapchain_,
            stereo::kSettingsMenuTextureSize,
            stereo::kSettingsMenuTextureSize);
    if (!settingsAvailable_)
    {
        settingsInteraction_.Reset();
        settingsArt_.Reset();
        DestroySwapchain(settingsSwapchain_);
        ReleaseInterface(settingsSource_);
        WriteLog(
            L"VR Settings resources are unavailable; the independent Quick Menu remains active and Settings will fail closed.");
    }
    WriteLog(
        settingsAvailable_
            ? L"Quick Menu and VR Settings OpenXR resources are ready: quick=%ux%u utilityStrip=%ux%u settings=%ux%u cursor=%ux%u. Settings uses the owner-tuned %.2f m width and sits %.2f m closer than its Deploy/Spawn plane; right A remains the dedicated interaction action."
            : L"Quick Menu OpenXR resources are ready without VR Settings: quick=%ux%u utilityStrip=%ux%u settings=%ux%u cursor=%ux%u. The Quick Menu remains independent; owner-tuned Settings width %.2f m and depth offset %.2f m are reserved for a later successful Settings load.",
        menuSwapchain_.width,
        menuSwapchain_.height,
        utilitySwapchain_.width,
        utilitySwapchain_.height,
        settingsSwapchain_.width,
        settingsSwapchain_.height,
        cursorSwapchain_.width,
        cursorSwapchain_.height,
        nativeMenuWidthMeters * stereo::kSettingsMenuNativeWidthScale,
        stereo::kSettingsMenuForwardOffsetMeters);
    return true;
}

void OpenXRQuickMenu::Update(
    const OpenXRPresentationFrameState& frame) noexcept
{
    stereo::QuickMenuFrameInput input = {};
    input.predictedDisplayTime = frame.predictedDisplayTime;
    input.sessionFocused = frame.controllerInput.sessionFocused;
    input.shouldRender = frame.shouldRender;
    input.headTracked = frame.headPoseValid && frame.headPoseTracked;
    input.headPose = ToPose(frame.headPose);
    const OpenXRControllerHandState& right = frame.controllerInput.hands[1];
    input.rightPrimaryHeld = right.primaryPressed;
    input.rightGripTracked = right.gripActive &&
        right.gripPositionValid && right.gripPositionTracked;
    input.rightAimTracked = right.aimActive &&
        right.aimPositionValid && right.aimOrientationValid &&
        right.aimPositionTracked && right.aimOrientationTracked;
    input.rightGripPose = ToPose(right.gripPose);
    input.rightAimPose = ToPose(right.aimPose);
    input.standingHeightValid = frame.standingHeightValid;
    input.standingHeightMeters = frame.standingHeightMeters;
    if (settingsInteraction_.IsActive())
    {
        settingsInteraction_.Update(input);
        const stereo::SettingsMenuSelection soundActivation =
            settingsInteraction_.TakeMenuSoundActivation();
        if (soundActivation == stereo::SettingsMenuSelection::Cancel)
        {
            ++pendingNativeMenuSounds_.cancel;
        }
        else if (soundActivation != stereo::SettingsMenuSelection::None)
        {
            ++pendingNativeMenuSounds_.ok;
        }
        if (settingsInteraction_.TakeValuesChanged())
        {
            settings::EncodeUserSettings(
                settingsInteraction_.Snapshot().values,
                userSettingsSession_.Working());
        }
        const stereo::SettingsMenuCommand command =
            settingsInteraction_.TakeCommand();
        if (command == stereo::SettingsMenuCommand::ResetDefaults)
        {
            userSettingsSession_.ResetToDefaults();
            settingsInteraction_.SetValues(settings::DecodeUserSettings(
                userSettingsSession_.Working()));
            settingsInteraction_.SetStatus(
                stereo::SettingsMenuStatus::DefaultsRestored);
            WriteLog(
                L"VR Settings replaced only its working copy with the seeded defaults; the saved UserConfig.txt remains unchanged until Save.");
        }
        else if (command == stereo::SettingsMenuCommand::Save)
        {
            const settings::UserSettingsValues savedValues =
                settings::DecodeUserSettings(userSettingsSession_.Working());
            const bool saved = userSettingsRuntime_ != nullptr &&
                userSettingsRuntime_->Commit(userSettingsSession_.Working());
            const bool restartRequired = saved &&
                settings::UserSettingsRequireRestart(
                    startupSettingsValues_,
                    savedValues);
            settingsInteraction_.SetStatus(
                !saved
                    ? stereo::SettingsMenuStatus::SaveFailed
                    : restartRequired
                    ? stereo::SettingsMenuStatus::
                          SettingsSavedRestartRequired
                    : stereo::SettingsMenuStatus::SettingsSaved);
            WriteLog(
                saved
                    ? L"VR Settings atomically saved its complete working copy to UserConfig.txt; the menu remains open."
                    : L"VR Settings could not save UserConfig.txt; the prior saved file remains intact and the menu remains open.");
        }
        else if (command == stereo::SettingsMenuCommand::Cancel)
        {
            userSettingsSession_.Cancel();
            WriteLog(
                L"VR Settings cancelled and discarded its unsaved working copy; UserConfig.txt was not changed.");
        }
        else if (command == stereo::SettingsMenuCommand::RecenterForward)
        {
            trackingAction_ = OpenXRTrackingAction::RecenterForward;
            WriteLog(
                L"VR Settings requested an immediate forward recenter; no saved setting was changed.");
        }
        (void)RefreshSettingsSource(settingsInteraction_.Snapshot());
        // The persistent panel owns right A until Cancel closes it. Resetting
        // here prevents a held settings click from opening Quick Menu below.
        interaction_.Reset();
        TrackNativeMenuHover();
        return;
    }
    interaction_.Update(input);
    TrackNativeMenuHover();
}

void OpenXRQuickMenu::SetMountedCameraDecoupled(
    bool decoupled) noexcept
{
    mountedCameraDecoupled_ = decoupled;
}

void OpenXRQuickMenu::OpenSettingsMenu() noexcept
{
    if (!settingsAvailable_)
    {
        WriteLog(
            L"VR Settings open was ignored because its independent resources are unavailable; Quick Menu remains active.");
        return;
    }
    interaction_.Reset();
    const settings::UserSettingsLoadStatus loadStatus =
        userSettingsSession_.Begin(userSettingsRuntime_->Store());
    settingsInteraction_.Open();
    settingsInteraction_.SetValues(settings::DecodeUserSettings(
        userSettingsSession_.Working()));
    switch (loadStatus)
    {
    case settings::UserSettingsLoadStatus::Loaded:
        settingsInteraction_.SetStatus(
            stereo::SettingsMenuStatus::SettingsLoaded);
        break;
    case settings::UserSettingsLoadStatus::InvalidUsedDefaults:
        settingsInteraction_.SetStatus(
            stereo::SettingsMenuStatus::InvalidConfigDefaultsLoaded);
        break;
    case settings::UserSettingsLoadStatus::IoErrorUsedDefaults:
        settingsInteraction_.SetStatus(
            stereo::SettingsMenuStatus::ConfigReadFailed);
        break;
    case settings::UserSettingsLoadStatus::LoadedCompletedSeed:
    case settings::UserSettingsLoadStatus::MissingUsedDefaults:
    case settings::UserSettingsLoadStatus::MissingCreatedDefaults:
    default:
        settingsInteraction_.SetStatus(
            stereo::SettingsMenuStatus::DefaultsLoaded);
        break;
    }
    settingsVisualValid_ = false;
    WriteLog(
        L"VR Settings menu opened on its persistent Deploy-style yaw anchor using %s; Cancel is its only close action.",
        settings::UserSettingsLoadStatusName(loadStatus));
}

void OpenXRQuickMenu::OnTrackingSpaceChanged() noexcept
{
    if (settingsInteraction_.IsActive())
    {
        settingsInteraction_.ResetTrackingAnchor();
    }
    else
    {
        interaction_.Reset();
    }
}

void OpenXRQuickMenu::SetForwardRecenterResult(bool succeeded) noexcept
{
    if (settingsInteraction_.IsActive())
    {
        settingsInteraction_.SetStatus(
            succeeded
                ? stereo::SettingsMenuStatus::ForwardRecentered
                : stereo::SettingsMenuStatus::ForwardRecenterFailed);
    }
}

std::size_t OpenXRQuickMenu::AppendLayers(
    XrSpace localSpace,
    const XrCompositionLayerBaseHeader** destination,
    std::size_t capacity)
{
    const stereo::SettingsMenuSnapshot settings =
        settingsInteraction_.Snapshot();
    if (settings.active)
    {
        if (!IsReady() || !settings.visible ||
            localSpace == XR_NULL_HANDLE || destination == nullptr ||
            capacity < 1 || !RefreshSettingsSource(settings) ||
            !CopyToSwapchain(
                settingsSwapchain_,
                settingsSource_,
                L"VR Settings menu"))
        {
            return 0;
        }
        menuLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        menuLayer_.layerFlags =
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        menuLayer_.space = localSpace;
        menuLayer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        menuLayer_.subImage.swapchain = settingsSwapchain_.handle;
        menuLayer_.subImage.imageRect.extent = {
            static_cast<std::int32_t>(settingsSwapchain_.width),
            static_cast<std::int32_t>(settingsSwapchain_.height)};
        menuLayer_.pose = ToXrPose(settings.panelPose);
        menuLayer_.size = {settings.widthMeters, settings.heightMeters};
        destination[0] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                &menuLayer_);
        std::size_t appended = 1;
        if (settings.pointerVisible && capacity > appended &&
            CopyToSwapchain(
                cursorSwapchain_,
                cursorSource_,
                L"VR Settings cursor"))
        {
            const float cursorWidthMeters = settings.widthMeters *
                static_cast<float>(cursorSwapchain_.width) /
                static_cast<float>(settingsSwapchain_.width) *
                stereo::kSettingsMenuCursorScale;
            const float cursorHeightMeters = settings.heightMeters *
                static_cast<float>(cursorSwapchain_.height) /
                static_cast<float>(settingsSwapchain_.height) *
                stereo::kSettingsMenuCursorScale;
            const stereo::Pose cursorPose =
                stereo::MakeSettingsMenuCursorPose(
                    settings.panelPose,
                    settings.widthMeters,
                    settings.heightMeters,
                    settings.pointerU,
                    settings.pointerV,
                    cursorWidthMeters,
                    cursorHeightMeters);
            cursorLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            cursorLayer_.layerFlags =
                XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            cursorLayer_.space = localSpace;
            cursorLayer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            cursorLayer_.subImage.swapchain = cursorSwapchain_.handle;
            cursorLayer_.subImage.imageRect.extent = {
                static_cast<std::int32_t>(cursorSwapchain_.width),
                static_cast<std::int32_t>(cursorSwapchain_.height)};
            cursorLayer_.pose = ToXrPose(cursorPose);
            cursorLayer_.size = {cursorWidthMeters, cursorHeightMeters};
            destination[appended++] =
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                    &cursorLayer_);
        }
        if (!firstSettingsFrameLogged_)
        {
            firstSettingsFrameLogged_ = true;
            WriteLog(
                L"VR Settings submitted its first persistent frame: tab=VR Settings, page=1/1, paging arrows hidden.");
        }
        return appended;
    }

    const stereo::QuickMenuInteractionSnapshot state =
        interaction_.Snapshot();
    if (!IsReady() || !state.visible || localSpace == XR_NULL_HANDLE ||
        destination == nullptr || capacity < 2)
    {
        return 0;
    }
    const std::size_t selectionIndex =
        static_cast<std::size_t>(state.hovered);
    if (selectionIndex >= menuSources_.size() ||
        !CopyToSwapchain(
            menuSwapchain_,
            menuSources_[selectionIndex],
            L"Quick Menu"))
    {
        return 0;
    }

    menuLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    menuLayer_.layerFlags =
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    menuLayer_.space = localSpace;
    menuLayer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    menuLayer_.subImage.swapchain = menuSwapchain_.handle;
    menuLayer_.subImage.imageRect.extent = {
        static_cast<std::int32_t>(menuSwapchain_.width),
        static_cast<std::int32_t>(menuSwapchain_.height)};
    menuLayer_.pose = ToXrPose(state.panelPose);
    menuLayer_.size = {
        stereo::kQuickMenuWidthMeters,
        stereo::kQuickMenuHeightMeters};
    destination[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
        &menuLayer_);
    std::size_t appended = 1;

    const std::size_t utilityIndex = UtilityVisualIndex(
        mountedCameraDecoupled_,
        state.hovered);
    if (utilityIndex >= utilitySources_.size() ||
        !CopyToSwapchain(
            utilitySwapchain_,
            utilitySources_[utilityIndex],
            L"Quick Menu utility strip"))
    {
        return appended;
    }
    utilityLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    utilityLayer_.layerFlags =
        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    utilityLayer_.space = localSpace;
    utilityLayer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    utilityLayer_.subImage.swapchain = utilitySwapchain_.handle;
    utilityLayer_.subImage.imageRect.extent = {
        static_cast<std::int32_t>(utilitySwapchain_.width),
        static_cast<std::int32_t>(utilitySwapchain_.height)};
    utilityLayer_.pose = ToXrPose(
        stereo::MakeQuickMenuUtilityPose(state.panelPose));
    utilityLayer_.size = {
        stereo::kQuickMenuWidthMeters,
        stereo::kQuickMenuUtilityHeightMeters};
    destination[appended++] =
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(
            &utilityLayer_);

    if (state.pointerVisible && capacity > appended &&
        CopyToSwapchain(
            cursorSwapchain_,
            cursorSource_,
            L"Quick Menu cursor"))
    {
        const float cursorWidthMeters = stereo::kQuickMenuWidthMeters *
            static_cast<float>(cursorSwapchain_.width) /
            static_cast<float>(menuSwapchain_.width);
        const float cursorHeightMeters = stereo::kQuickMenuHeightMeters *
            static_cast<float>(cursorSwapchain_.height) /
            static_cast<float>(menuSwapchain_.height);
        const stereo::Pose cursorPose = state.pointerOnUtilityStrip
            ? stereo::MakeQuickMenuUtilityCursorPose(
                state.panelPose,
                state.pointerU,
                state.pointerV,
                cursorWidthMeters,
                cursorHeightMeters)
            : stereo::MakeQuickMenuCursorPose(
                state.panelPose,
                state.pointerU,
                state.pointerV,
                cursorWidthMeters,
                cursorHeightMeters);
        cursorLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        cursorLayer_.layerFlags =
            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        cursorLayer_.space = localSpace;
        cursorLayer_.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        cursorLayer_.subImage.swapchain = cursorSwapchain_.handle;
        cursorLayer_.subImage.imageRect.extent = {
            static_cast<std::int32_t>(cursorSwapchain_.width),
            static_cast<std::int32_t>(cursorSwapchain_.height)};
        cursorLayer_.pose = ToXrPose(cursorPose);
        cursorLayer_.size = {cursorWidthMeters, cursorHeightMeters};
        destination[appended++] =
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                &cursorLayer_);
    }

    if (!firstVisibleFrameLogged_)
    {
        firstVisibleFrameLogged_ = true;
        WriteLog(
            L"Quick Menu submitted its first stabilized right-hand frame with hover=%s pointer=%d.",
            stereo::QuickMenuSelectionName(state.hovered),
            state.pointerVisible ? 1 : 0);
    }
    return appended;
}

stereo::QuickMenuSelection
OpenXRQuickMenu::TakeReleasedSelection() noexcept
{
    return interaction_.TakeReleasedSelection();
}

OpenXRNativeMenuSoundRequests
OpenXRQuickMenu::TakeNativeMenuSoundRequests() noexcept
{
    const OpenXRNativeMenuSoundRequests result = pendingNativeMenuSounds_;
    pendingNativeMenuSounds_ = {};
    return result;
}

OpenXRTrackingAction OpenXRQuickMenu::TakeTrackingAction() noexcept
{
    const OpenXRTrackingAction result = trackingAction_;
    trackingAction_ = OpenXRTrackingAction::None;
    return result;
}

std::uint64_t OpenXRQuickMenu::HapticHoverTarget() const noexcept
{
    const stereo::SettingsMenuSnapshot settings =
        settingsInteraction_.Snapshot();
    if (settings.active)
    {
        return settings.visible &&
            settings.hovered != stereo::SettingsMenuSelection::None
            ? 0x1'0000'0000ULL +
                static_cast<std::uint64_t>(settings.hovered)
            : 0;
    }
    const stereo::QuickMenuInteractionSnapshot quick =
        interaction_.Snapshot();
    return quick.visible &&
        quick.hovered != stereo::QuickMenuSelection::None
        ? 1ULL + static_cast<std::uint64_t>(quick.hovered)
        : 0;
}

void OpenXRQuickMenu::TrackNativeMenuHover() noexcept
{
    const std::uint64_t target = HapticHoverTarget();
    if (target != 0 && target != lastNativeMenuSoundHoverTarget_)
    {
        ++pendingNativeMenuSounds_.highlight;
    }
    lastNativeMenuSoundHoverTarget_ = target;
}

bool OpenXRQuickMenu::ControllerHapticsEnabled() const noexcept
{
    const stereo::SettingsMenuSnapshot settings =
        settingsInteraction_.Snapshot();
    if (settings.active)
    {
        return settings.values.controllerHapticsEnabled;
    }
    return userSettingsRuntime_ == nullptr ||
        settings::DecodeUserSettings(userSettingsRuntime_->Current())
            .controllerHapticsEnabled;
}

bool OpenXRQuickMenu::GetMirrorState(
    OpenXRQuickMenuMirrorState& state) const noexcept
{
    state = {};
    const stereo::SettingsMenuSnapshot settings =
        settingsInteraction_.Snapshot();
    if (settings.active)
    {
        if (!IsReady() || !settings.visible || settingsSource_ == nullptr)
        {
            return false;
        }
        // The mutable texture is refreshed by AppendLayers earlier in the
        // same EndFrame path; mirror borrows that exact composed visual.
        state.visible = true;
        state.pointerVisible = settings.pointerVisible;
        state.panelPose = ToPresentationPose(settings.panelPose);
        state.panelWidthMeters = settings.widthMeters;
        state.panelHeightMeters = settings.heightMeters;
        state.menuTexture = settingsSource_;
        if (settings.pointerVisible && cursorSource_ != nullptr &&
            settingsSwapchain_.width != 0 && settingsSwapchain_.height != 0)
        {
            state.cursorWidthMeters = settings.widthMeters *
                static_cast<float>(cursorSwapchain_.width) /
                static_cast<float>(settingsSwapchain_.width) *
                stereo::kSettingsMenuCursorScale;
            state.cursorHeightMeters = settings.heightMeters *
                static_cast<float>(cursorSwapchain_.height) /
                static_cast<float>(settingsSwapchain_.height) *
                stereo::kSettingsMenuCursorScale;
            state.cursorPose = ToPresentationPose(
                stereo::MakeSettingsMenuCursorPose(
                    settings.panelPose,
                    settings.widthMeters,
                    settings.heightMeters,
                    settings.pointerU,
                    settings.pointerV,
                    state.cursorWidthMeters,
                    state.cursorHeightMeters));
            state.cursorTexture = cursorSource_;
        }
        return true;
    }
    const stereo::QuickMenuInteractionSnapshot snapshot =
        interaction_.Snapshot();
    const std::size_t selectionIndex =
        static_cast<std::size_t>(snapshot.hovered);
    if (!IsReady() || !snapshot.visible ||
        selectionIndex >= menuSources_.size())
    {
        return false;
    }

    state.visible = true;
    state.pointerVisible = snapshot.pointerVisible;
    state.hovered = snapshot.hovered;
    state.panelPose = ToPresentationPose(snapshot.panelPose);
    state.panelWidthMeters = stereo::kQuickMenuWidthMeters;
    state.panelHeightMeters = stereo::kQuickMenuHeightMeters;
    state.utilityPose = ToPresentationPose(
        stereo::MakeQuickMenuUtilityPose(snapshot.panelPose));
    state.utilityWidthMeters = stereo::kQuickMenuWidthMeters;
    state.utilityHeightMeters = stereo::kQuickMenuUtilityHeightMeters;
    state.menuTexture = menuSources_[selectionIndex];
    const std::size_t utilityIndex = UtilityVisualIndex(
        mountedCameraDecoupled_,
        snapshot.hovered);
    if (utilityIndex >= utilitySources_.size())
    {
        return false;
    }
    state.utilityTexture = utilitySources_[utilityIndex];
    if (snapshot.pointerVisible && cursorSource_ != nullptr &&
        menuSwapchain_.width != 0 && menuSwapchain_.height != 0)
    {
        state.cursorWidthMeters = stereo::kQuickMenuWidthMeters *
            static_cast<float>(cursorSwapchain_.width) /
            static_cast<float>(menuSwapchain_.width);
        state.cursorHeightMeters = stereo::kQuickMenuHeightMeters *
            static_cast<float>(cursorSwapchain_.height) /
            static_cast<float>(menuSwapchain_.height);
        state.cursorPose = ToPresentationPose(
            snapshot.pointerOnUtilityStrip
                ? stereo::MakeQuickMenuUtilityCursorPose(
                    snapshot.panelPose,
                    snapshot.pointerU,
                    snapshot.pointerV,
                    state.cursorWidthMeters,
                    state.cursorHeightMeters)
                : stereo::MakeQuickMenuCursorPose(
                    snapshot.panelPose,
                    snapshot.pointerU,
                    snapshot.pointerV,
                    state.cursorWidthMeters,
                    state.cursorHeightMeters));
        state.cursorTexture = cursorSource_;
    }
    return true;
}

bool OpenXRQuickMenu::IsReady() const noexcept
{
    return session_ != XR_NULL_HANDLE && device_ != nullptr &&
        context_ != nullptr && menuSwapchain_.handle != XR_NULL_HANDLE &&
        utilitySwapchain_.handle != XR_NULL_HANDLE &&
        cursorSwapchain_.handle != XR_NULL_HANDLE &&
        cursorSource_ != nullptr &&
        std::all_of(
            menuSources_.begin(),
            menuSources_.end(),
            [](ID3D11Texture2D* texture) { return texture != nullptr; }) &&
        std::all_of(
            utilitySources_.begin(),
            utilitySources_.end(),
            [](ID3D11Texture2D* texture) { return texture != nullptr; });
}

void OpenXRQuickMenu::Shutdown()
{
    interaction_.Reset();
    userSettingsSession_.Cancel();
    userSettingsRuntime_ = nullptr;
    startupSettingsValues_ = {};
    settingsInteraction_.Reset();
    settingsArt_.Reset();
    DestroySwapchain(settingsSwapchain_);
    DestroySwapchain(cursorSwapchain_);
    DestroySwapchain(utilitySwapchain_);
    DestroySwapchain(menuSwapchain_);
    ReleaseInterface(cursorSource_);
    ReleaseInterface(settingsSource_);
    for (ID3D11Texture2D*& source : menuSources_)
    {
        ReleaseInterface(source);
    }
    for (ID3D11Texture2D*& source : utilitySources_)
    {
        ReleaseInterface(source);
    }
    ReleaseInterface(context_);
    ReleaseInterface(device_);
    api_ = {};
    session_ = XR_NULL_HANDLE;
    swapchainFormat_ = DXGI_FORMAT_UNKNOWN;
    menuLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    utilityLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    cursorLayer_ = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    firstVisibleFrameLogged_ = false;
    firstSettingsFrameLogged_ = false;
    settingsAvailable_ = false;
    settingsVisualValid_ = false;
    renderedSettingsState_ = {};
    mountedCameraDecoupled_ = false;
    trackingAction_ = OpenXRTrackingAction::None;
    pendingNativeMenuSounds_ = {};
    lastNativeMenuSoundHoverTarget_ = 0;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

bool OpenXRQuickMenu::CreateSwapchain(
    Swapchain& swapchain,
    UINT width,
    UINT height)
{
    XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.format = static_cast<std::int64_t>(swapchainFormat_);
    createInfo.sampleCount = 1;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;
    XrResult result = api_.createSwapchain(
        session_,
        &createInfo,
        &swapchain.handle);
    if (XR_FAILED(result) || swapchain.handle == XR_NULL_HANDLE)
    {
        WriteLog(
            L"Quick Menu swapchain creation failed for %ux%u (result=%ld).",
            width,
            height,
            static_cast<long>(result));
        return false;
    }
    uint32_t imageCount = 0;
    result = api_.enumerateSwapchainImages(
        swapchain.handle,
        0,
        &imageCount,
        nullptr);
    if (XR_FAILED(result) || imageCount == 0)
    {
        WriteLog(
            L"Quick Menu swapchain image count query failed (result=%ld).",
            static_cast<long>(result));
        return false;
    }
    swapchain.images.resize(imageCount);
    for (XrSwapchainImageD3D11KHR& image : swapchain.images)
    {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        image.next = nullptr;
    }
    result = api_.enumerateSwapchainImages(
        swapchain.handle,
        imageCount,
        &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(
            swapchain.images.data()));
    if (XR_FAILED(result))
    {
        WriteLog(
            L"Quick Menu swapchain image enumeration failed (result=%ld).",
            static_cast<long>(result));
        return false;
    }
    swapchain.width = width;
    swapchain.height = height;
    return true;
}

bool OpenXRQuickMenu::CreateSourceTexture(
    const std::vector<std::uint32_t>& bgraPixels,
    UINT width,
    UINT height,
    ID3D11Texture2D** texture)
{
    if (texture == nullptr || width == 0 || height == 0 ||
        bgraPixels.size() != static_cast<std::size_t>(width) * height)
    {
        return false;
    }
    std::vector<std::uint32_t> rgbaPixels;
    const std::uint32_t* source = bgraPixels.data();
    if (IsRgbaFormat(swapchainFormat_))
    {
        rgbaPixels.resize(bgraPixels.size());
        std::transform(
            bgraPixels.begin(),
            bgraPixels.end(),
            rgbaPixels.begin(),
            SwapRedBlue);
        source = rgbaPixels.data();
    }
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = swapchainFormat_;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {
        source,
        width * sizeof(std::uint32_t),
        0};
    return SUCCEEDED(device_->CreateTexture2D(
        &description,
        &data,
        texture)) && *texture != nullptr;
}

bool OpenXRQuickMenu::CreateMutableSourceTexture(
    const std::vector<std::uint32_t>& bgraPixels,
    UINT width,
    UINT height,
    ID3D11Texture2D** texture)
{
    if (texture == nullptr || width == 0 || height == 0 ||
        bgraPixels.size() != static_cast<std::size_t>(width) * height)
    {
        return false;
    }
    std::vector<std::uint32_t> rgbaPixels;
    const std::uint32_t* source = bgraPixels.data();
    if (IsRgbaFormat(swapchainFormat_))
    {
        rgbaPixels.resize(bgraPixels.size());
        std::transform(
            bgraPixels.begin(),
            bgraPixels.end(),
            rgbaPixels.begin(),
            SwapRedBlue);
        source = rgbaPixels.data();
    }
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = swapchainFormat_;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {
        source,
        width * sizeof(std::uint32_t),
        0};
    return SUCCEEDED(device_->CreateTexture2D(
        &description,
        &data,
        texture)) && *texture != nullptr;
}

bool OpenXRQuickMenu::RefreshSettingsSource(
    const stereo::SettingsMenuSnapshot& state)
{
    const bool unchanged = settingsVisualValid_ &&
        renderedSettingsState_.controllerLayoutVisible ==
            state.controllerLayoutVisible &&
        renderedSettingsState_.arrowLeftVisible == state.arrowLeftVisible &&
        renderedSettingsState_.arrowRightVisible == state.arrowRightVisible &&
        renderedSettingsState_.tab == state.tab &&
        renderedSettingsState_.hovered == state.hovered &&
        renderedSettingsState_.page == state.page &&
        renderedSettingsState_.values == state.values &&
        renderedSettingsState_.status == state.status;
    if (unchanged)
    {
        return true;
    }
    if (settingsSource_ == nullptr || context_ == nullptr)
    {
        return false;
    }
    std::vector<std::uint32_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (!settingsArt_.Compose(state, pixels, width, height) ||
        width != stereo::kSettingsMenuTextureSize ||
        height != stereo::kSettingsMenuTextureSize)
    {
        return false;
    }
    if (IsRgbaFormat(swapchainFormat_))
    {
        std::transform(
            pixels.begin(),
            pixels.end(),
            pixels.begin(),
            SwapRedBlue);
    }
    context_->UpdateSubresource(
        settingsSource_,
        0,
        nullptr,
        pixels.data(),
        width * sizeof(std::uint32_t),
        0);
    // The source texture is mutable. Its pointer remains stable when the
    // composed Settings artwork changes, so explicitly invalidate the cached
    // OpenXR image and upload the new pixels on the next layer submission.
    settingsSwapchain_.copiedSource = nullptr;
    settingsSwapchain_.contentValid = false;
    renderedSettingsState_ = state;
    settingsVisualValid_ = true;
    return true;
}

bool OpenXRQuickMenu::CopyToSwapchain(
    Swapchain& target,
    ID3D11Texture2D* source,
    const wchar_t* label)
{
    if (source == nullptr || target.handle == XR_NULL_HANDLE)
    {
        return false;
    }
    // OpenXR composition layers may keep submitting the most recently
    // released swapchain image. Reusing it avoids an acquire/wait/copy/release
    // round trip every frame when only a quad pose (not its pixels) changed.
    if (target.contentValid && target.copiedSource == source)
    {
        return true;
    }
    D3D11_TEXTURE2D_DESC description = {};
    source->GetDesc(&description);
    if (description.Width != target.width ||
        description.Height != target.height ||
        description.Format != swapchainFormat_)
    {
        return false;
    }

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{
        XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = api_.acquireSwapchainImage(
        target.handle,
        &acquireInfo,
        &imageIndex);
    if (XR_FAILED(result) || imageIndex >= target.images.size())
    {
        WriteLog(
            L"Quick Menu could not acquire its %s image (result=%ld).",
            label,
            static_cast<long>(result));
        return false;
    }
    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = api_.waitSwapchainImage(target.handle, &waitInfo);
    ID3D11Texture2D* const destination =
        imageIndex < target.images.size()
        ? target.images[imageIndex].texture
        : nullptr;
    if (XR_SUCCEEDED(result) && destination != nullptr)
    {
        context_->CopyResource(destination, source);
    }
    else
    {
        WriteLog(
            L"Quick Menu could not wait for its %s image (result=%ld).",
            label,
            static_cast<long>(result));
    }
    XrSwapchainImageReleaseInfo releaseInfo{
        XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult releaseResult = api_.releaseSwapchainImage(
        target.handle,
        &releaseInfo);
    if (XR_FAILED(releaseResult))
    {
        WriteLog(
            L"Quick Menu could not release its %s image (result=%ld).",
            label,
            static_cast<long>(releaseResult));
    }
    const bool copied = XR_SUCCEEDED(result) && destination != nullptr &&
        XR_SUCCEEDED(releaseResult);
    if (copied)
    {
        target.copiedSource = source;
        target.contentValid = true;
    }
    return copied;
}

void OpenXRQuickMenu::DestroySwapchain(Swapchain& swapchain) noexcept
{
    if (swapchain.handle != XR_NULL_HANDLE && api_.destroySwapchain != nullptr)
    {
        api_.destroySwapchain(swapchain.handle);
    }
    swapchain = {};
}

void OpenXRQuickMenu::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1200> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    logCallback_(logContext_, message.data());
}

} // namespace bfvr
