bool ReadKeepOriginalFlatBackbuffer()
{
    wchar_t value[2] = {};
    const bool explicitlyRequested =
        GetEnvironmentVariableW(
            L"BFVR_KEEP_FLAT_BACKBUFFER",
            value,
            static_cast<DWORD>(std::size(value))) == 1 &&
        value[0] == L'1';
    if (explicitlyRequested)
    {
        return true;
    }
    value[0] = L'\0';
    return GetEnvironmentVariableW(
        L"BFVR_DESKTOP_MIRROR",
        value,
        static_cast<DWORD>(std::size(value))) == 1 &&
        value[0] == L'0';
}

void PrepareRuntimeRenderRequestPose()
{
    const bool nativeMenuActive = bfvr::IsMenuPointerOverlayActive();
    bfvr::stereo::Pose rawMenuWorldAnchor = {};
    bool rawMenuWorldAnchorValid = false;
    g_frameUiPlacement = {};
    g_frameUiPlacement.headLocked = true;
    const bfvr::MainMenuOverlayInteractionState overlayState =
        bfvr::GetMainMenuOverlayInteractionState();
    g_frameUiPlacement.backToGameVisible = overlayState.visible;
    g_frameUiPlacement.backToGameHovered = overlayState.hovered;
    if (nativeMenuActive)
    {
        const bfvr::D3D8RuntimeView currentHead =
            bfvr::MakeD3D8RuntimeHeadReference(g_runtimeRenderRequest);
        const bfvr::stereo::Pose currentHeadPose = {
            {currentHead.positionX, currentHead.positionY, currentHead.positionZ},
            {
                currentHead.orientationX,
                currentHead.orientationY,
                currentHead.orientationZ,
                currentHead.orientationW}};
        constexpr float kMenuFollowStartRadians = 0.610865238F;
        constexpr float kMenuFollowRadiansPerSecond = 1.570796327F;
        if (bfvr::stereo::UpdateUiMenuAnchor(
                g_menuAnchorTracker,
                currentHeadPose,
                g_runtimeRenderRequest.predictedDisplayTime,
                kMenuFollowStartRadians,
                kMenuFollowRadiansPerSecond))
        {
            const bfvr::stereo::Pose& anchor = g_menuAnchorTracker.anchor;
            g_frameUiPlacement.headLocked = false;
            g_frameUiPlacement.worldAnchorValid = true;
            g_frameUiPlacement.worldAnchor.orientationX = anchor.orientation.x;
            g_frameUiPlacement.worldAnchor.orientationY = anchor.orientation.y;
            g_frameUiPlacement.worldAnchor.orientationZ = anchor.orientation.z;
            g_frameUiPlacement.worldAnchor.orientationW = anchor.orientation.w;
            g_frameUiPlacement.worldAnchor.positionX = anchor.position.x;
            g_frameUiPlacement.worldAnchor.positionY = anchor.position.y;
            g_frameUiPlacement.worldAnchor.positionZ = anchor.position.z;
            rawMenuWorldAnchor = anchor;
            rawMenuWorldAnchorValid = true;
        }
        else
        {
            // Do not leave the controller mapper on an older LOCAL anchor
            // when presentation has already failed closed to VIEW-space UI.
            bfvr::ClearActiveMenuWorldAnchor();
        }
        if (!g_nativeMenuActive)
        {
            AppendLog(
                L"Native menu opened: placed its LOCAL panel at the yaw-only opening pose; it will world-lock, then yaw-follow only beyond 35 degrees at 90 degrees/sec.");
        }
    }
    else
    {
        if (g_nativeMenuActive)
        {
            AppendLog(
                L"Native menu closed: cleared its LOCAL panel and controller-ray anchor.");
        }
        bfvr::stereo::ResetUiMenuAnchor(g_menuAnchorTracker);
        bfvr::ClearActiveMenuWorldAnchor();
    }
    g_nativeMenuActive = nativeMenuActive;

    const bfvr::D3D8RuntimeView currentHead =
        bfvr::MakeD3D8RuntimeHeadReference(g_runtimeRenderRequest);
    const bool hasTrackedHead =
        g_runtimeRenderRequest.headPoseValid &&
        g_runtimeRenderRequest.headPoseTracked;
    const ULONGLONG now = GetTickCount64();
    bool trackingSettingsChanged = false;
    bool playModeChanged = false;
    if (!g_trackingSettingsInitialized ||
        now >= g_nextTrackingSettingsPollAt)
    {
        g_nextTrackingSettingsPollAt = now + 250;
        auto& runtime = bfvr::settings::ProcessUserSettingsRuntime();
        const bool settingsReloaded = runtime.ReloadIfChanged();
        const auto updatedSettings = bfvr::settings::DecodeUserSettings(
            runtime.Current());
        playModeChanged = !g_trackingSettingsInitialized ||
            updatedSettings.playMode != g_trackingUserSettings.playMode;
        trackingSettingsChanged = settingsReloaded || playModeChanged;
        g_trackingUserSettings = updatedSettings;
        g_trackingSettingsInitialized = true;
    }
    bfvr::D3D8TrackingContext trackingContext = {};
    bfvr::LocalPlayerControlContext playerContext = {};
    if (bfvr::ReadLocalPlayerControlContext(playerContext))
    {
        trackingContext.kind =
            playerContext.currentControlObject ==
                playerContext.defaultControlObject
            ? bfvr::D3D8TrackingContextKind::Infantry
            : bfvr::D3D8TrackingContextKind::Seat;
        trackingContext.token = reinterpret_cast<std::uintptr_t>(
            playerContext.currentControlObject);
    }
    const bool standingMode = g_trackingUserSettings.playMode ==
        bfvr::settings::PlayMode::Standing;
    const float manualHeightAdjustmentMeters =
        bfvr::settings::ComputeManualHeightAdjustmentMeters(
            g_trackingUserSettings);
    const bool seatedPostureTransitionWasActive =
        g_trackingAnchor.IsSeatedPostureTransitionActive();
    g_trackingAnchor.Update(
        currentHead,
        hasTrackedHead,
        trackingContext,
        g_runtimeRenderRequest.predictedDisplayTime,
        g_runtimeRenderRequest.recenterForwardSequence,
        standingMode,
        g_runtimeRenderRequest.standingHeightValid,
        g_runtimeRenderRequest.standingHeightMeters,
        static_cast<float>(
            g_trackingUserSettings.standingEyeHeightCentimeters) / 100.0F,
        manualHeightAdjustmentMeters);
    const bool seatedPostureTransitionIsActive =
        g_trackingAnchor.IsSeatedPostureTransitionActive();
    if (seatedPostureTransitionWasActive !=
        seatedPostureTransitionIsActive)
    {
        AppendLog(
            seatedPostureTransitionIsActive
                ? L"Seated mode was selected while the headset was still near calibrated standing height; its neutral Y will follow the ensuing sit-down and lock after 0.75 seconds of vertical stability. No additional mode toggle is required."
                : L"Seated posture transition settled; the final seated neutral Y is now locked and ordinary tracked vertical head motion is restored.");
    }
    const bfvr::D3D8RuntimeView trackingReference =
        g_trackingAnchor.ReferenceHead(currentHead);
    if (trackingSettingsChanged && hasTrackedHead &&
        trackingContext.kind == bfvr::D3D8TrackingContextKind::Infantry)
    {
        AppendLog(
            L"Infantry vertical placement updated: mode=%ls localHeadY=%.4f stageFloorToHead=%.4f stageValid=%d referenceY=%.4f resultingHeadDeltaY=%.4f manualTrim=%.4f. Standing derives the LOCAL floor and subtracts BF1942's existing 1.70-m eye camera; Seated captures the current posture as neutral.",
            standingMode ? L"standing" : L"seated",
            currentHead.positionY,
            g_runtimeRenderRequest.standingHeightMeters,
            g_runtimeRenderRequest.standingHeightValid ? 1 : 0,
            trackingReference.positionY,
            currentHead.positionY - trackingReference.positionY,
            manualHeightAdjustmentMeters);
    }
    const bfvr::D3D8RuntimeView rebasedHead =
        g_trackingAnchor.RebaseView(currentHead);
    if (rawMenuWorldAnchorValid)
    {
        // The x64 compositor consumes the raw LOCAL anchor above. The x86
        // native-menu pointer consumes controller poses after the tracking
        // anchor has rebased them for gameplay, so publish a matching rebased
        // copy only to that mapper. Mixing the raw panel anchor with rebased
        // controller poses produces the intermittent height/recenter offset
        // introduced with the locomotion and play-mode settings.
        bfvr::D3D8RuntimeView menuAnchorView = {};
        menuAnchorView.positionX = rawMenuWorldAnchor.position.x;
        menuAnchorView.positionY = rawMenuWorldAnchor.position.y;
        menuAnchorView.positionZ = rawMenuWorldAnchor.position.z;
        menuAnchorView.orientationX = rawMenuWorldAnchor.orientation.x;
        menuAnchorView.orientationY = rawMenuWorldAnchor.orientation.y;
        menuAnchorView.orientationZ = rawMenuWorldAnchor.orientation.z;
        menuAnchorView.orientationW = rawMenuWorldAnchor.orientation.w;
        const bfvr::D3D8RuntimeView rebasedMenuAnchor =
            g_trackingAnchor.RebaseView(menuAnchorView);
        bfvr::PublishActiveMenuWorldAnchor({
            {
                rebasedMenuAnchor.positionX,
                rebasedMenuAnchor.positionY,
                rebasedMenuAnchor.positionZ},
            {
                rebasedMenuAnchor.orientationX,
                rebasedMenuAnchor.orientationY,
                rebasedMenuAnchor.orientationZ,
                rebasedMenuAnchor.orientationW}});
    }
    if (g_runtimeRenderRequest.controllerInput.valid)
    {
        bfvr::PublishAcceptedControllerInput(
            g_trackingAnchor.RebaseControllerSample(
                g_runtimeRenderRequest.controllerInput),
            rebasedHead,
            hasTrackedHead);
    }
    else
    {
        bfvr::ClearAcceptedControllerInput();
    }
    g_runtimeFramePosePolicy = bfvr::MakeD3D8RuntimeFramePosePolicy(
        currentHead,
        hasTrackedHead);
    g_runtimeFramePosePolicy.renderViewReference = trackingReference;
    if (hasTrackedHead && !g_loggedImmutableLocalTrackingOrigin)
    {
        g_loggedImmutableLocalTrackingOrigin = true;
        AppendLog(
            L"Using immutable OpenXR LOCAL tracking with x86 context anchors: Seated captures the current posture as neutral; Standing derives the live STAGE floor in LOCAL coordinates so only floor-to-head height minus BF1942's existing 1.70-m eye camera is added. Manual trim is independent, every occupied vehicle/station owns a separate neutral pose, and recentering pivots at the headset. No automatic body rotation or networked position transform is added.");
    }
    g_renderViewPoseHook.UpdatePose(
        trackingReference,
        g_runtimeRenderRequest,
        g_trackingAnchor.ContextGeneration(),
        g_trackingAnchor.Context().kind ==
            bfvr::D3D8TrackingContextKind::Infantry);
}
