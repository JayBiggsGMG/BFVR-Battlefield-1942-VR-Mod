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
            bfvr::PublishActiveMenuWorldAnchor(anchor);
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
    const bfvr::stereo::Pose hrtfHeadPose = {
        {currentHead.positionX, currentHead.positionY, currentHead.positionZ},
        {
            currentHead.orientationX,
            currentHead.orientationY,
            currentHead.orientationZ,
            currentHead.orientationW}};
    bfvr::audio::PublishHrtfHeadPose(
        hrtfHeadPose,
        hasTrackedHead);
    g_runtimeFramePosePolicy = bfvr::MakeD3D8RuntimeFramePosePolicy(
        currentHead,
        hasTrackedHead);
    if (hasTrackedHead && !g_loggedImmutableLocalTrackingOrigin)
    {
        // Do not sample the user's current pose here. A sampled pose creates
        // inverse(sampledHead) * currentHead and turns whichever way the HMD
        // happened to be held into the game's persistent coordinate basis.
        // The constant OpenXR LOCAL origin is independent of every BF1942
        // lifecycle event and of controller availability.
        g_loggedImmutableLocalTrackingOrigin = true;
        AppendLog(
            L"Using the OpenXR LOCAL tracking origin; BF1942 spawn, death, respawn, Ready/menu, controller state, and D3D8 Reset cannot sample or redefine the 6DOF camera basis. Runtime-level OpenXR reference-space changes are a separate lifecycle event.");
    }
    g_renderViewPoseHook.UpdatePose(
        g_runtimeFramePosePolicy.renderViewReference,
        g_runtimeRenderRequest);
}
