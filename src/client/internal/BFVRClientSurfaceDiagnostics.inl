bool IsSystemD3D8Target(const void* target)
{
    HMODULE module = nullptr;
    if (target == nullptr ||
        !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target),
            &module))
    {
        return false;
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath))) == 0)
    {
        return false;
    }

    const wchar_t* fileName = wcsrchr(modulePath, L'\\');
    fileName = fileName == nullptr ? modulePath : fileName + 1;
    return _wcsicmp(fileName, L"d3d8.dll") == 0;
}

BOOL TryGetD3D8ObserverLifecycle(bfvr::D3D8ObserverLifecycle* lifecycle)
{
    if (lifecycle == nullptr ||
        g_createDeviceBreakpoint.device == nullptr ||
        g_createDeviceBreakpoint.presentTarget == nullptr ||
        !g_createDeviceBreakpoint.resetObserved ||
        !g_createDeviceBreakpoint.postResetPresentObserved ||
        InterlockedCompareExchange(&g_createDeviceBreakpoint.stage, 0, 0) != 6)
    {
        return FALSE;
    }

    lifecycle->device = g_createDeviceBreakpoint.device;
    lifecycle->deviceThreadId = g_createDeviceBreakpoint.threadId;
    lifecycle->presentationReadable = g_createDeviceBreakpoint.presentationReadable;
    lifecycle->backBufferWidth = g_createDeviceBreakpoint.presentation.backBufferWidth;
    lifecycle->backBufferHeight = g_createDeviceBreakpoint.presentation.backBufferHeight;
    return TRUE;
}

BOOL IsD3D8ObserverCaptureEligible()
{
    return InterlockedCompareExchange(&g_sustainedLocalPlayerAliveObserved, 0, 0) != 0;
}

void AppendD3D8ObserverLog(const wchar_t* message)
{
    AppendLog(L"%s", message == nullptr ? L"" : message);
}

void SignalD3D8ObserverProbeCompletion()
{
    wchar_t eventName[96] = {};
    if (swprintf_s(eventName, std::size(eventName), L"Local\\BFVRD3D8ProbeComplete-%lu", GetCurrentProcessId()) < 0)
    {
        return;
    }

    HANDLE completionEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (completionEvent != nullptr)
    {
        SetEvent(completionEvent);
        CloseHandle(completionEvent);
    }
}

void StartD3D8CallInventoryProbe()
{
    if (InterlockedCompareExchange(&g_d3d8CallInventoryProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_d3d8CallInventoryProbeStarted, 1, 0) != 0)
    {
        return;
    }

    const bfvr::D3D8ObserverCallbacks callbacks = {
        TryGetD3D8ObserverLifecycle,
        IsD3D8ObserverCaptureEligible,
        AppendD3D8ObserverLog,
        SignalD3D8ObserverProbeCompletion};
    bfvr::StartD3D8CallInventoryProbe(callbacks);
}

void StartD3D8StateCensusProbe()
{
    if (InterlockedCompareExchange(&g_d3d8StateCensusProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_d3d8StateCensusProbeStarted, 1, 0) != 0)
    {
        return;
    }

    const bfvr::D3D8ObserverCallbacks callbacks = {
        TryGetD3D8ObserverLifecycle,
        IsD3D8ObserverCaptureEligible,
        AppendD3D8ObserverLog,
        SignalD3D8ObserverProbeCompletion};
    bfvr::StartD3D8StateCensusProbe(callbacks);
}

// This is intentionally a one-shot ownership check, not an OpenXR graphics
// binding. The D3D11 device, default texture, staging texture, and immediate
// context are all created by and released by BFVR in this call. No game-owned
// D3D8 object crosses the API boundary and no resource survives a D3D8 Reset.
void UploadLockedReadbackToOwnedD3D11Texture(
    ProjectionTargetDescriptorProbeRecord& record,
    const D3DSurfaceDescription& sourceDescription,
    const D3DLockedRect& lockedRect)
{
    if (!record.d3d11UploadRequested ||
        sourceDescription.format != 22 ||
        lockedRect.bits == nullptr ||
        lockedRect.pitch < static_cast<LONG>(sourceDescription.width * sizeof(DWORD)))
    {
        return;
    }

    record.d3d11FormatMappingAccepted = TRUE;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* texture = nullptr;
    ID3D11Texture2D* stagingTexture = nullptr;
    __try
    {
        do
        {
        const bfvr::D3DSystemRuntime& runtime =
            bfvr::GetD3DSystemRuntime();
        if (!runtime.IsAvailable())
        {
            record.d3d11CreateDeviceResult =
                HRESULT_FROM_WIN32(
                    runtime.error == ERROR_SUCCESS
                    ? ERROR_PROC_NOT_FOUND
                    : runtime.error);
            break;
        }
        record.d3d11CreateDeviceResult = runtime.createD3D11Device(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &record.d3d11FeatureLevel,
            &context);
        if (FAILED(record.d3d11CreateDeviceResult) || device == nullptr || context == nullptr)
        {
            break;
        }

        D3D11_TEXTURE2D_DESC textureDescription = {};
        textureDescription.Width = sourceDescription.width;
        textureDescription.Height = sourceDescription.height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        record.d3d11CreateTextureResult = device->CreateTexture2D(&textureDescription, nullptr, &texture);
        if (FAILED(record.d3d11CreateTextureResult) || texture == nullptr)
        {
            break;
        }

        textureDescription.Usage = D3D11_USAGE_STAGING;
        textureDescription.BindFlags = 0;
        textureDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        record.d3d11CreateStagingTextureResult = device->CreateTexture2D(&textureDescription, nullptr, &stagingTexture);
        if (FAILED(record.d3d11CreateStagingTextureResult) || stagingTexture == nullptr)
        {
            break;
        }

        record.d3d11UploadAttempted = TRUE;
        context->UpdateSubresource(
            texture,
            0,
            nullptr,
            lockedRect.bits,
            static_cast<UINT>(lockedRect.pitch),
            0);
        context->CopyResource(stagingTexture, texture);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        record.d3d11MapResult = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
        record.d3d11ReadbackPitch = static_cast<LONG>(mapped.RowPitch);
        if (SUCCEEDED(record.d3d11MapResult) && mapped.pData != nullptr &&
            mapped.RowPitch >= sourceDescription.width * sizeof(DWORD))
        {
            const UINT sampleCoordinates[5][2] = {
                {0, 0},
                {sourceDescription.width / 4, sourceDescription.height / 4},
                {sourceDescription.width / 2, sourceDescription.height / 2},
                {(sourceDescription.width * 3) / 4, (sourceDescription.height * 3) / 4},
                {sourceDescription.width - 1, sourceDescription.height - 1}};
            const auto* const pixels = reinterpret_cast<const BYTE*>(mapped.pData);
            BOOL allSamplesMatch = TRUE;
            for (UINT sampleIndex = 0; sampleIndex < std::size(record.d3d11VerificationPixels); ++sampleIndex)
            {
                const UINT x = sampleCoordinates[sampleIndex][0];
                const UINT y = sampleCoordinates[sampleIndex][1];
                const DWORD pixel = *reinterpret_cast<const DWORD*>(pixels +
                    static_cast<std::size_t>(y) * mapped.RowPitch +
                    static_cast<std::size_t>(x) * sizeof(DWORD));
                record.d3d11VerificationPixels[sampleIndex] = pixel;
                if (pixel != record.readbackPixels[sampleIndex])
                {
                    allSamplesMatch = FALSE;
                }
            }
            record.d3d11PixelsMatchSource = allSamplesMatch;
            context->Unmap(stagingTexture, 0);
        }
        } while (false);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        record.structuredException = TRUE;
    }

    if (stagingTexture != nullptr)
    {
        stagingTexture->Release();
    }
    if (texture != nullptr)
    {
        texture->Release();
    }
    if (context != nullptr)
    {
        context->Release();
    }
    if (device != nullptr)
    {
        device->Release();
    }
    record.d3d11ResourcesReleased = TRUE;
}

// Called only after the original SetTransform trampoline accepts the exact
// ordinary-world Projection submission already classified by the passive
// hardware trace. GetRenderTarget supplies one temporary game-owned reference,
// which is always balanced before returning to the renderer coordinator. The
// separately explicit copy mode also creates and releases one BFVR-owned
// render target entirely inside this callback, so no default-pool resource can
// survive into a later Reset.
void TryCaptureProjectionTargetDescriptor(
    void* device,
    DWORD transformState,
    void* callerReturnAddress,
    DWORD externalCallerReturnAddress,
    BOOL externalStackReadable,
    HRESULT setTransformResult)
{
    if (InterlockedCompareExchange(&g_surfaceDescriptorProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_projectionTargetDescriptorProbe.state, 0, 0) != 0 ||
        FAILED(setTransformResult) ||
        device == nullptr ||
        GetCurrentThreadId() != g_createDeviceBreakpoint.threadId)
    {
        return;
    }

    if (transformState != kD3DTransformProjection ||
        callerReturnAddress != reinterpret_cast<void*>(kProjectionWrapperReturnAddress) ||
        !externalStackReadable ||
        externalCallerReturnAddress != kOrdinaryWorldProjectionCallerReturnAddress ||
        InterlockedCompareExchange(&g_profiledCameraInterfaceObserved, 0, 0) == 0)
    {
        return;
    }
    if (InterlockedCompareExchange(&g_projectionTargetDescriptorProbe.state, 1, 0) != 0)
    {
        return;
    }

    ProjectionTargetDescriptorProbeRecord& record = g_projectionTargetDescriptorProbe;
    record.executionThreadId = GetCurrentThreadId();
    record.transformState = transformState;
    record.callerReturnAddress = callerReturnAddress;
    record.externalCallerReturnAddress = externalCallerReturnAddress;
    record.externalStackReadable = externalStackReadable;
    record.setTransformResult = setTransformResult;
    record.copyRequested = InterlockedCompareExchange(&g_surfaceCopyProbeRequested, 0, 0) != 0;
    record.readbackRequested =
        InterlockedCompareExchange(&g_surfaceReadbackProbeRequested, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0;
    record.readbackAfterEndScene =
        InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0;
    record.d3d11UploadRequested = InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0;
    void* acquiredSurface = nullptr;
    void* ownedSurface = nullptr;
    void* readbackSurface = nullptr;
    __try
    {
        auto** const deviceVtable = *reinterpret_cast<void***>(device);
        const auto getRenderTarget = deviceVtable == nullptr
            ? nullptr
            : reinterpret_cast<GetRenderTargetFn>(deviceVtable[kDirect3DDevice8GetRenderTargetSlot]);
        if (getRenderTarget != nullptr)
        {
            record.getRenderTargetResult = getRenderTarget(device, &acquiredSurface);
        }
        if (SUCCEEDED(record.getRenderTargetResult) && acquiredSurface != nullptr)
        {
            record.acquiredSurface = acquiredSurface;
            record.sourceReferenceOutstanding = TRUE;
            auto** const surfaceVtable = *reinterpret_cast<void***>(acquiredSurface);
            const auto getDesc = surfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<GetSurfaceDescriptionFn>(surfaceVtable[kDirect3DSurface8GetDescSlot]);
            record.getDescTarget = reinterpret_cast<void*>(getDesc);
            if (getDesc != nullptr)
            {
                record.getDescResult = getDesc(acquiredSurface, &record.description);
                record.descriptionReadable = SUCCEEDED(record.getDescResult);
            }
        }

        if ((record.copyRequested || record.readbackRequested) && record.descriptionReadable && g_createDeviceBreakpoint.presentationReadable)
        {
            const D3DSurfaceDescription& sourceDescription = record.description;
            const D3DPresentParameters& presentation = g_createDeviceBreakpoint.presentation;
            record.copySafetyGatePassed =
                sourceDescription.type == 1 &&
                (sourceDescription.usage & 0x1) != 0 &&
                sourceDescription.pool == 0 &&
                sourceDescription.multiSampleType == 0 &&
                sourceDescription.width == presentation.backBufferWidth &&
                sourceDescription.height == presentation.backBufferHeight &&
                sourceDescription.format == presentation.backBufferFormat;

            if (record.copySafetyGatePassed)
            {
                if (record.copyRequested)
                {
                    record.createRenderTargetTarget = deviceVtable[kDirect3DDevice8CreateRenderTargetSlot];
                }
                if (record.readbackRequested)
                {
                    record.createImageSurfaceTarget = deviceVtable[kDirect3DDevice8CreateImageSurfaceSlot];
                }
                record.copyRectsTarget = deviceVtable[kDirect3DDevice8CopyRectsSlot];
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        record.structuredException = TRUE;
    }

    if (record.copyRequested && record.copySafetyGatePassed)
    {
        record.createRenderTargetInSystemD3D8 = IsSystemD3D8Target(record.createRenderTargetTarget);
        record.copyRectsInSystemD3D8 = IsSystemD3D8Target(record.copyRectsTarget);
        if (record.createRenderTargetInSystemD3D8 && record.copyRectsInSystemD3D8)
        {
            __try
            {
                const auto createRenderTarget = reinterpret_cast<CreateRenderTargetFn>(record.createRenderTargetTarget);
                const auto copyRects = reinterpret_cast<CopyRectsFn>(record.copyRectsTarget);
                const D3DSurfaceDescription& sourceDescription = record.description;
                record.createRenderTargetResult = createRenderTarget(
                    device,
                    sourceDescription.width,
                    sourceDescription.height,
                    sourceDescription.format,
                    sourceDescription.multiSampleType,
                    FALSE,
                    &ownedSurface);
                if (SUCCEEDED(record.createRenderTargetResult) && ownedSurface != nullptr)
                {
                    record.ownedSurface = ownedSurface;
                    record.ownedReferenceOutstanding = TRUE;
                    auto** const ownedSurfaceVtable = *reinterpret_cast<void***>(ownedSurface);
                    const auto ownedGetDesc = ownedSurfaceVtable == nullptr
                        ? nullptr
                        : reinterpret_cast<GetSurfaceDescriptionFn>(ownedSurfaceVtable[kDirect3DSurface8GetDescSlot]);
                    record.ownedGetDescTarget = reinterpret_cast<void*>(ownedGetDesc);
                    if (ownedGetDesc != nullptr && IsSystemD3D8Target(record.ownedGetDescTarget))
                    {
                        record.ownedGetDescResult = ownedGetDesc(ownedSurface, &record.ownedDescription);
                        record.ownedDescriptionReadable = SUCCEEDED(record.ownedGetDescResult);
                        if (record.ownedDescriptionReadable)
                        {
                            const D3DSurfaceDescription& ownedDescription = record.ownedDescription;
                            record.ownedDescriptionMatchesSource =
                                ownedDescription.format == sourceDescription.format &&
                                ownedDescription.type == sourceDescription.type &&
                                (ownedDescription.usage & 0x1) != 0 &&
                                ownedDescription.pool == 0 &&
                                ownedDescription.multiSampleType == sourceDescription.multiSampleType &&
                                ownedDescription.width == sourceDescription.width &&
                                ownedDescription.height == sourceDescription.height;
                        }
                    }

                    if (record.ownedDescriptionMatchesSource)
                    {
                        const RECT sourceRectangle = {
                            0,
                            0,
                            static_cast<LONG>(sourceDescription.width),
                            static_cast<LONG>(sourceDescription.height)};
                        const POINT destinationPoint = {0, 0};
                        record.copyAttempted = TRUE;
                        record.copyRectsResult = copyRects(
                            device,
                            acquiredSurface,
                            &sourceRectangle,
                            1,
                            ownedSurface,
                            &destinationPoint);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                record.structuredException = TRUE;
            }
        }
    }

    // This is deliberately separate from the default-pool owned-target copy.
    // CreateImageSurface yields a transient CPU-lockable image; it is used only
    // to establish that the exact world-target copy exposes readable pixels.
    if (record.readbackRequested && record.copySafetyGatePassed)
    {
        record.createImageSurfaceInSystemD3D8 = IsSystemD3D8Target(record.createImageSurfaceTarget);
        record.copyRectsInSystemD3D8 = IsSystemD3D8Target(record.copyRectsTarget);
        if (record.createImageSurfaceInSystemD3D8 && record.copyRectsInSystemD3D8)
        {
            __try
            {
                const auto createImageSurface = reinterpret_cast<CreateImageSurfaceFn>(record.createImageSurfaceTarget);
                const auto copyRects = reinterpret_cast<CopyRectsFn>(record.copyRectsTarget);
                const D3DSurfaceDescription& sourceDescription = record.description;
                record.createImageSurfaceResult = createImageSurface(
                    device,
                    sourceDescription.width,
                    sourceDescription.height,
                    sourceDescription.format,
                    &readbackSurface);
                if (SUCCEEDED(record.createImageSurfaceResult) && readbackSurface != nullptr)
                {
                    record.readbackSurface = readbackSurface;
                    record.readbackReferenceOutstanding = TRUE;
                    auto** const readbackVtable = *reinterpret_cast<void***>(readbackSurface);
                    const auto getDesc = readbackVtable == nullptr
                        ? nullptr
                        : reinterpret_cast<GetSurfaceDescriptionFn>(readbackVtable[kDirect3DSurface8GetDescSlot]);
                    record.readbackGetDescTarget = reinterpret_cast<void*>(getDesc);
                    if (getDesc != nullptr && IsSystemD3D8Target(record.readbackGetDescTarget))
                    {
                        record.readbackGetDescResult = getDesc(readbackSurface, &record.readbackDescription);
                        record.readbackDescriptionReadable = SUCCEEDED(record.readbackGetDescResult);
                        if (record.readbackDescriptionReadable)
                        {
                            const D3DSurfaceDescription& readbackDescription = record.readbackDescription;
                            record.readbackDescriptionMatchesSource =
                                readbackDescription.format == sourceDescription.format &&
                                readbackDescription.width == sourceDescription.width &&
                                readbackDescription.height == sourceDescription.height;
                        }
                    }

                    if (record.readbackDescriptionMatchesSource)
                    {
                        const RECT sourceRectangle = {
                            0,
                            0,
                            static_cast<LONG>(sourceDescription.width),
                            static_cast<LONG>(sourceDescription.height)};
                        const POINT destinationPoint = {0, 0};
                        record.readbackCopyAttempted = TRUE;
                        record.readbackCopyRectsResult = copyRects(
                            device,
                            acquiredSurface,
                            &sourceRectangle,
                            1,
                            readbackSurface,
                            &destinationPoint);
                    }

                    if (SUCCEEDED(record.readbackCopyRectsResult))
                    {
                        const auto lockRect = readbackVtable == nullptr
                            ? nullptr
                            : reinterpret_cast<LockSurfaceRectFn>(readbackVtable[kDirect3DSurface8LockRectSlot]);
                        const auto unlockRect = readbackVtable == nullptr
                            ? nullptr
                            : reinterpret_cast<UnlockSurfaceRectFn>(readbackVtable[kDirect3DSurface8UnlockRectSlot]);
                        record.readbackLockRectTarget = reinterpret_cast<void*>(lockRect);
                        record.readbackUnlockRectTarget = reinterpret_cast<void*>(unlockRect);
                        record.readbackLockRectInSystemD3D8 = IsSystemD3D8Target(record.readbackLockRectTarget);
                        record.readbackUnlockRectInSystemD3D8 = IsSystemD3D8Target(record.readbackUnlockRectTarget);
                        if (lockRect != nullptr && unlockRect != nullptr &&
                            record.readbackLockRectInSystemD3D8 && record.readbackUnlockRectInSystemD3D8)
                        {
                            D3DLockedRect lockedRect = {};
                            record.readbackLockRectResult = lockRect(readbackSurface, &lockedRect, nullptr, 0);
                            record.readbackPitch = lockedRect.pitch;
                            const UINT minimumPitch = sourceDescription.width * sizeof(DWORD);
                            if (SUCCEEDED(record.readbackLockRectResult) && lockedRect.bits != nullptr &&
                                sourceDescription.format == 22 &&
                                lockedRect.pitch >= static_cast<LONG>(minimumPitch))
                            {
                                const UINT sampleCoordinates[5][2] = {
                                    {0, 0},
                                    {sourceDescription.width / 4, sourceDescription.height / 4},
                                    {sourceDescription.width / 2, sourceDescription.height / 2},
                                    {(sourceDescription.width * 3) / 4, (sourceDescription.height * 3) / 4},
                                    {sourceDescription.width - 1, sourceDescription.height - 1}};
                                const auto* const bits = reinterpret_cast<const BYTE*>(lockedRect.bits);
                                for (UINT sampleIndex = 0; sampleIndex < std::size(record.readbackPixels); ++sampleIndex)
                                {
                                    const UINT x = sampleCoordinates[sampleIndex][0];
                                    const UINT y = sampleCoordinates[sampleIndex][1];
                                    const DWORD pixel = *reinterpret_cast<const DWORD*>(bits +
                                        static_cast<std::size_t>(y) * static_cast<std::size_t>(lockedRect.pitch) +
                                        static_cast<std::size_t>(x) * sizeof(DWORD));
                                    record.readbackPixels[sampleIndex] = pixel;
                                    if (pixel != 0)
                                    {
                                        ++record.readbackNonZeroPixelCount;
                                    }
                                }
                                record.readbackPixelsReadable = TRUE;
                                UploadLockedReadbackToOwnedD3D11Texture(record, sourceDescription, lockedRect);
                            }
                            if (SUCCEEDED(record.readbackLockRectResult))
                            {
                                record.readbackUnlockRectResult = unlockRect(readbackSurface);
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                record.structuredException = TRUE;
            }
        }
    }

    if (ownedSurface != nullptr)
    {
        __try
        {
            auto** const ownedSurfaceVtable = *reinterpret_cast<void***>(ownedSurface);
            const auto release = ownedSurfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<ReleaseUnknownFn>(ownedSurfaceVtable[2]);
            if (release != nullptr)
            {
                record.ownedReleaseResult = release(ownedSurface);
                record.ownedReleaseCalled = TRUE;
                record.ownedReferenceOutstanding = FALSE;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            record.structuredException = TRUE;
        }
    }

    if (readbackSurface != nullptr)
    {
        __try
        {
            auto** const readbackVtable = *reinterpret_cast<void***>(readbackSurface);
            const auto release = readbackVtable == nullptr
                ? nullptr
                : reinterpret_cast<ReleaseUnknownFn>(readbackVtable[2]);
            if (release != nullptr)
            {
                record.readbackReleaseResult = release(readbackSurface);
                record.readbackReleaseCalled = TRUE;
                record.readbackReferenceOutstanding = FALSE;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            record.structuredException = TRUE;
        }
    }

    if (acquiredSurface != nullptr)
    {
        __try
        {
            auto** const surfaceVtable = *reinterpret_cast<void***>(acquiredSurface);
            const auto release = surfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<ReleaseUnknownFn>(surfaceVtable[2]);
            if (release != nullptr)
            {
                record.releaseResult = release(acquiredSurface);
                record.releaseCalled = TRUE;
                record.sourceReferenceOutstanding = FALSE;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            record.structuredException = TRUE;
        }
    }
    record.resetSafeAtReturn =
        !record.sourceReferenceOutstanding &&
        !record.ownedReferenceOutstanding &&
        !record.readbackReferenceOutstanding;
    InterlockedExchange(&record.state, 2);
}

bool IsProjectionTargetStreamSourceSafe(const D3DSurfaceDescription& description)
{
    if (!g_createDeviceBreakpoint.presentationReadable)
    {
        return false;
    }

    const D3DPresentParameters& presentation = g_createDeviceBreakpoint.presentation;
    return description.type == 1 &&
        (description.usage & 0x1) != 0 &&
        description.pool == 0 &&
        description.multiSampleType == 0 &&
        description.width == presentation.backBufferWidth &&
        description.height == presentation.backBufferHeight &&
        description.format == presentation.backBufferFormat;
}

bool DoesProjectionTargetStreamOwnedSurfaceMatch(
    const D3DSurfaceDescription& ownedDescription,
    const D3DSurfaceDescription& sourceDescription)
{
    return ownedDescription.format == sourceDescription.format &&
        ownedDescription.type == sourceDescription.type &&
        (ownedDescription.usage & 0x1) != 0 &&
        ownedDescription.pool == 0 &&
        ownedDescription.multiSampleType == sourceDescription.multiSampleType &&
        ownedDescription.width == sourceDescription.width &&
        ownedDescription.height == sourceDescription.height;
}

// This helper is called only from a D3D8 callback on the independently proven
// device thread. The resource belongs to BFVR alone, is never bound as the
// game's render target, and is released before Reset or bounded completion.
BOOL ReleaseProjectionTargetStreamOwnedSurface(BOOL releaseForReset)
{
    ProjectionTargetStreamProbeRecord& record = g_projectionTargetStreamProbe;
    void* const ownedSurface = record.ownedSurface;
    if (ownedSurface == nullptr)
    {
        record.ownedReferenceOutstanding = FALSE;
        return TRUE;
    }

    BOOL released = FALSE;
    __try
    {
        auto** const surfaceVtable = *reinterpret_cast<void***>(ownedSurface);
        const auto release = surfaceVtable == nullptr
            ? nullptr
            : reinterpret_cast<ReleaseUnknownFn>(surfaceVtable[2]);
        if (release != nullptr)
        {
            record.lastOwnedReleaseResult = release(ownedSurface);
            record.ownedSurface = nullptr;
            record.ownedReferenceOutstanding = FALSE;
            InterlockedIncrement(&record.ownedReleaseCount);
            if (releaseForReset)
            {
                InterlockedIncrement(&record.resetReleaseCount);
            }
            released = TRUE;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        record.structuredException = TRUE;
    }
    return released;
}

void CompleteProjectionTargetStream(DWORD reason)
{
    ProjectionTargetStreamProbeRecord& record = g_projectionTargetStreamProbe;
    if (InterlockedCompareExchange(&record.state, 0, 0) == 2)
    {
        return;
    }

    if (record.completionReason == kProjectionTargetStreamCompletionNone)
    {
        record.completionReason = reason;
    }
    ReleaseProjectionTargetStreamOwnedSurface(FALSE);
    record.resetSafeAtReturn =
        !record.ownedReferenceOutstanding &&
        !record.sourceReferenceOutstanding;
    InterlockedExchange(&record.state, 2);
}

// This is a bounded stream rather than a render hook. It can retain exactly
// one BFVR-owned target for at most kProjectionStreamCopyLimit exact world
// Projection submissions. The game-owned current target is acquired and
// released on every individual copy.
void TryRunProjectionTargetStream(
    void* device,
    DWORD transformState,
    void* callerReturnAddress,
    DWORD externalCallerReturnAddress,
    BOOL externalStackReadable,
    HRESULT setTransformResult)
{
    ProjectionTargetStreamProbeRecord& record = g_projectionTargetStreamProbe;
    if (InterlockedCompareExchange(&g_surfaceStreamProbeRequested, 0, 0) == 0 ||
        FAILED(setTransformResult) ||
        device == nullptr ||
        GetCurrentThreadId() != g_createDeviceBreakpoint.threadId)
    {
        return;
    }

    if (transformState != kD3DTransformProjection ||
        callerReturnAddress != reinterpret_cast<void*>(kProjectionWrapperReturnAddress) ||
        !externalStackReadable ||
        externalCallerReturnAddress != kOrdinaryWorldProjectionCallerReturnAddress ||
        InterlockedCompareExchange(&g_profiledCameraInterfaceObserved, 0, 0) == 0)
    {
        return;
    }

    LONG streamState = InterlockedCompareExchange(&record.state, 0, 0);
    if (streamState == 2)
    {
        return;
    }
    if (streamState == 0)
    {
        if (InterlockedCompareExchange(&record.state, 1, 0) != 0)
        {
            return;
        }
        record.firstExecutionThreadId = GetCurrentThreadId();
        record.transformState = transformState;
        record.callerReturnAddress = callerReturnAddress;
        record.externalCallerReturnAddress = externalCallerReturnAddress;
        record.externalStackReadable = externalStackReadable;
        record.resetProbeRequested = InterlockedCompareExchange(&g_surfaceResetProbeRequested, 0, 0) != 0;
    }

    record.lastExecutionThreadId = GetCurrentThreadId();
    if (record.firstExecutionThreadId != record.lastExecutionThreadId)
    {
        CompleteProjectionTargetStream(kProjectionTargetStreamCompletionUnexpectedThread);
        return;
    }

    if (record.resetProbeRequested &&
        record.ownedSurface != nullptr &&
        !record.awaitingPostResetRecreation)
    {
        if (InterlockedCompareExchange(&record.stopRequested, 0, 0) != 0)
        {
            CompleteProjectionTargetStream(kProjectionTargetStreamCompletionWorkerTimeout);
        }
        return;
    }

    void* acquiredSurface = nullptr;
    BOOL complete = FALSE;
    DWORD completionReason = kProjectionTargetStreamCompletionNone;
    __try
    {
        auto** const deviceVtable = *reinterpret_cast<void***>(device);
        const auto getRenderTarget = deviceVtable == nullptr
            ? nullptr
            : reinterpret_cast<GetRenderTargetFn>(deviceVtable[kDirect3DDevice8GetRenderTargetSlot]);
        if (getRenderTarget == nullptr)
        {
            complete = TRUE;
            completionReason = kProjectionTargetStreamCompletionSourceUnavailable;
        }
        else
        {
            record.lastGetRenderTargetResult = getRenderTarget(device, &acquiredSurface);
            if (FAILED(record.lastGetRenderTargetResult) || acquiredSurface == nullptr)
            {
                complete = TRUE;
                completionReason = kProjectionTargetStreamCompletionSourceUnavailable;
            }
            else
            {
                record.sourceReferenceOutstanding = TRUE;
                InterlockedIncrement(&record.sourceAcquireCount);
                auto** const sourceSurfaceVtable = *reinterpret_cast<void***>(acquiredSurface);
                const auto getDesc = sourceSurfaceVtable == nullptr
                    ? nullptr
                    : reinterpret_cast<GetSurfaceDescriptionFn>(sourceSurfaceVtable[kDirect3DSurface8GetDescSlot]);
                if (getDesc == nullptr || !IsSystemD3D8Target(reinterpret_cast<void*>(getDesc)))
                {
                    complete = TRUE;
                    completionReason = kProjectionTargetStreamCompletionSourceUnavailable;
                }
                else
                {
                    record.lastGetDescResult = getDesc(acquiredSurface, &record.lastSourceDescription);
                    record.lastSourceDescriptionReadable = SUCCEEDED(record.lastGetDescResult);
                    record.lastSourceSafetyGatePassed = record.lastSourceDescriptionReadable &&
                        IsProjectionTargetStreamSourceSafe(record.lastSourceDescription);
                    if (!record.lastSourceSafetyGatePassed)
                    {
                        complete = TRUE;
                        completionReason = kProjectionTargetStreamCompletionSourceRejected;
                    }
                }
            }
        }

        if (!complete && record.ownedSurface == nullptr)
        {
            record.createRenderTargetTarget = deviceVtable[kDirect3DDevice8CreateRenderTargetSlot];
            record.copyRectsTarget = deviceVtable[kDirect3DDevice8CopyRectsSlot];
            record.createRenderTargetInSystemD3D8 = IsSystemD3D8Target(record.createRenderTargetTarget);
            record.copyRectsInSystemD3D8 = IsSystemD3D8Target(record.copyRectsTarget);
            if (!record.createRenderTargetInSystemD3D8 || !record.copyRectsInSystemD3D8)
            {
                complete = TRUE;
                completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
            }
            else
            {
                const auto createRenderTarget = reinterpret_cast<CreateRenderTargetFn>(record.createRenderTargetTarget);
                void* ownedSurface = nullptr;
                const D3DSurfaceDescription& sourceDescription = record.lastSourceDescription;
                record.lastCreateRenderTargetResult = createRenderTarget(
                    device,
                    sourceDescription.width,
                    sourceDescription.height,
                    sourceDescription.format,
                    sourceDescription.multiSampleType,
                    FALSE,
                    &ownedSurface);
                if (FAILED(record.lastCreateRenderTargetResult) || ownedSurface == nullptr)
                {
                    complete = TRUE;
                    completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
                }
                else
                {
                    record.ownedSurface = ownedSurface;
                    record.ownedReferenceOutstanding = TRUE;
                    InterlockedIncrement(&record.ownedCreateCount);
                    auto** const ownedSurfaceVtable = *reinterpret_cast<void***>(ownedSurface);
                    const auto ownedGetDesc = ownedSurfaceVtable == nullptr
                        ? nullptr
                        : reinterpret_cast<GetSurfaceDescriptionFn>(ownedSurfaceVtable[kDirect3DSurface8GetDescSlot]);
                    if (ownedGetDesc == nullptr || !IsSystemD3D8Target(reinterpret_cast<void*>(ownedGetDesc)))
                    {
                        complete = TRUE;
                        completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
                    }
                    else
                    {
                        record.lastOwnedGetDescResult = ownedGetDesc(ownedSurface, &record.ownedDescription);
                        record.ownedDescriptionReadable = SUCCEEDED(record.lastOwnedGetDescResult) &&
                            DoesProjectionTargetStreamOwnedSurfaceMatch(record.ownedDescription, sourceDescription);
                        if (!record.ownedDescriptionReadable)
                        {
                            complete = TRUE;
                            completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
                        }
                    }
                }
            }
        }

        if (!complete &&
            (!record.ownedDescriptionReadable ||
             !DoesProjectionTargetStreamOwnedSurfaceMatch(record.ownedDescription, record.lastSourceDescription)))
        {
            complete = TRUE;
            completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
        }

        if (!complete)
        {
            const auto copyRects = reinterpret_cast<CopyRectsFn>(record.copyRectsTarget);
            const D3DSurfaceDescription& sourceDescription = record.lastSourceDescription;
            const RECT sourceRectangle = {
                0,
                0,
                static_cast<LONG>(sourceDescription.width),
                static_cast<LONG>(sourceDescription.height)};
            const POINT destinationPoint = {0, 0};
            record.lastCopyRectsResult = copyRects(
                device,
                acquiredSurface,
                &sourceRectangle,
                1,
                record.ownedSurface,
                &destinationPoint);
            InterlockedIncrement(&record.copyAttemptCount);
            if (FAILED(record.lastCopyRectsResult))
            {
                complete = TRUE;
                completionReason = kProjectionTargetStreamCompletionCopyFailure;
            }
            else
            {
                const LONG copySuccessCount = InterlockedIncrement(&record.copySuccessCount);
                if (record.resetProbeRequested && record.awaitingPostResetRecreation)
                {
                    record.postResetRecreationVerified = TRUE;
                    complete = TRUE;
                    completionReason = kProjectionTargetStreamCompletionResetRecreated;
                }
                else if (!record.resetProbeRequested && copySuccessCount >= kProjectionStreamCopyLimit)
                {
                    complete = TRUE;
                    completionReason = kProjectionTargetStreamCompletionCopyLimitReached;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        record.structuredException = TRUE;
        complete = TRUE;
        completionReason = kProjectionTargetStreamCompletionOwnedTargetFailure;
    }

    if (acquiredSurface != nullptr)
    {
        __try
        {
            auto** const sourceSurfaceVtable = *reinterpret_cast<void***>(acquiredSurface);
            const auto release = sourceSurfaceVtable == nullptr
                ? nullptr
                : reinterpret_cast<ReleaseUnknownFn>(sourceSurfaceVtable[2]);
            if (release != nullptr)
            {
                record.lastSourceReleaseResult = release(acquiredSurface);
                record.sourceReferenceOutstanding = FALSE;
                InterlockedIncrement(&record.sourceReleaseCount);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            record.structuredException = TRUE;
            complete = TRUE;
            completionReason = kProjectionTargetStreamCompletionSourceUnavailable;
        }
    }

    if (record.sourceReferenceOutstanding)
    {
        complete = TRUE;
        completionReason = kProjectionTargetStreamCompletionSourceUnavailable;
    }
    if (InterlockedCompareExchange(&record.stopRequested, 0, 0) != 0 && !complete)
    {
        complete = TRUE;
        completionReason = kProjectionTargetStreamCompletionWorkerTimeout;
    }
    if (complete)
    {
        CompleteProjectionTargetStream(completionReason);
    }
}

HRESULT WINAPI HookSetTransformProjectionTargetStream(void* device, DWORD transformState, const void* matrix)
{
    void* const callerReturnAddress = _ReturnAddress();
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    __try
    {
        const auto* const stack = reinterpret_cast<const DWORD*>(_AddressOfReturnAddress());
        externalCallerReturnAddress = stack[6];
        externalStackReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        externalStackReadable = FALSE;
    }

    const SetTransformFn originalSetTransform = g_originalSetTransformForStreamProbe;
    const HRESULT result = originalSetTransform == nullptr
        ? E_FAIL
        : originalSetTransform(device, transformState, matrix);
    if (SUCCEEDED(result))
    {
        TryRunProjectionTargetStream(
            device,
            transformState,
            callerReturnAddress,
            externalCallerReturnAddress,
            externalStackReadable,
            result);
    }
    return result;
}

HRESULT WINAPI HookResetProjectionTargetStream(void* device, D3DPresentParameters* presentationParameters)
{
    D3DPresentParameters presentationSnapshot = {};
    BOOL presentationSnapshotReadable = FALSE;
    const BOOL streamRequested = InterlockedCompareExchange(&g_surfaceStreamProbeRequested, 0, 0) != 0;
    const BOOL onDeviceThread = device != nullptr && GetCurrentThreadId() == g_createDeviceBreakpoint.threadId;
    if (streamRequested && onDeviceThread)
    {
        ProjectionTargetStreamProbeRecord& record = g_projectionTargetStreamProbe;
        InterlockedIncrement(&record.resetHookCallCount);
        if (record.resetProbeRequested &&
            InterlockedCompareExchange(&record.state, 0, 0) == 1 &&
            record.ownedSurface != nullptr)
        {
            record.resetObservedWithOwnedTarget = TRUE;
            record.copySuccessCountAtReset = InterlockedCompareExchange(&record.copySuccessCount, 0, 0);
        }
        __try
        {
            if (presentationParameters != nullptr)
            {
                presentationSnapshot = *presentationParameters;
                presentationSnapshotReadable = TRUE;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_projectionTargetStreamProbe.structuredException = TRUE;
            presentationSnapshotReadable = FALSE;
        }
        ReleaseProjectionTargetStreamOwnedSurface(TRUE);
    }

    const ResetFn originalReset = g_originalResetForStreamProbe;
    const HRESULT result = originalReset == nullptr
        ? E_FAIL
        : originalReset(device, presentationParameters);
    if (streamRequested && onDeviceThread)
    {
        ProjectionTargetStreamProbeRecord& record = g_projectionTargetStreamProbe;
        record.lastResetResult = result;
        if (SUCCEEDED(result))
        {
            InterlockedIncrement(&record.resetSuccessCount);
            if (record.resetProbeRequested &&
                InterlockedCompareExchange(&record.state, 0, 0) == 1 &&
                record.resetObservedWithOwnedTarget)
            {
                record.awaitingPostResetRecreation = TRUE;
            }
            if (presentationSnapshotReadable)
            {
                g_createDeviceBreakpoint.presentation = presentationSnapshot;
                g_createDeviceBreakpoint.presentationReadable = TRUE;
                record.resetParametersCaptured = TRUE;
            }
        }
        else if (InterlockedCompareExchange(&record.state, 0, 0) == 1)
        {
            CompleteProjectionTargetStream(kProjectionTargetStreamCompletionResetFailure);
        }
    }
    return result;
}

void ArmProjectionTargetSceneReadback(
    void* device,
    DWORD transformState,
    void* callerReturnAddress,
    DWORD externalCallerReturnAddress,
    BOOL externalStackReadable,
    HRESULT setTransformResult)
{
    if ((InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) == 0 &&
         InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) == 0) ||
        InterlockedCompareExchange(&g_projectionTargetDescriptorProbe.state, 0, 0) != 0 ||
        FAILED(setTransformResult) ||
        device == nullptr ||
        GetCurrentThreadId() != g_createDeviceBreakpoint.threadId ||
        transformState != kD3DTransformProjection ||
        callerReturnAddress != reinterpret_cast<void*>(kProjectionWrapperReturnAddress) ||
        !externalStackReadable ||
        externalCallerReturnAddress != kOrdinaryWorldProjectionCallerReturnAddress ||
        InterlockedCompareExchange(&g_profiledCameraInterfaceObserved, 0, 0) == 0)
    {
        return;
    }

    if (InterlockedCompareExchange(&g_projectionTargetSceneReadbackArm.pending, 1, 0) != 0)
    {
        return;
    }

    ProjectionTargetSceneReadbackArm& arm = g_projectionTargetSceneReadbackArm;
    arm.device = device;
    arm.transformState = transformState;
    arm.callerReturnAddress = callerReturnAddress;
    arm.externalCallerReturnAddress = externalCallerReturnAddress;
    arm.externalStackReadable = externalStackReadable;
    arm.setTransformResult = setTransformResult;
    arm.threadId = GetCurrentThreadId();
}

HRESULT WINAPI HookEndSceneProjectionTargetReadback(void* device)
{
    const EndSceneFn originalEndScene = g_originalEndSceneForSceneReadbackProbe;
    const HRESULT result = originalEndScene == nullptr ? E_FAIL : originalEndScene(device);
    ProjectionTargetSceneReadbackArm& arm = g_projectionTargetSceneReadbackArm;
    if (SUCCEEDED(result) &&
        (InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0 ||
         InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0) &&
        InterlockedCompareExchange(&arm.pending, 0, 0) == 1 &&
        device == arm.device &&
        GetCurrentThreadId() == arm.threadId &&
        InterlockedCompareExchange(&arm.pending, 0, 1) == 1)
    {
        TryCaptureProjectionTargetDescriptor(
            device,
            arm.transformState,
            arm.callerReturnAddress,
            arm.externalCallerReturnAddress,
            arm.externalStackReadable,
            arm.setTransformResult);
    }
    return result;
}

HRESULT WINAPI HookSetTransformProjectionTargetProbe(void* device, DWORD transformState, const void* matrix)
{
    void* const callerReturnAddress = _ReturnAddress();
    DWORD externalCallerReturnAddress = 0;
    BOOL externalStackReadable = FALSE;
    __try
    {
        const auto* const stack = reinterpret_cast<const DWORD*>(_AddressOfReturnAddress());
        externalCallerReturnAddress = stack[6];
        externalStackReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        externalStackReadable = FALSE;
    }

    const SetTransformFn originalSetTransform = g_originalSetTransformForDescriptorProbe;
    const HRESULT result = originalSetTransform == nullptr
        ? E_FAIL
        : originalSetTransform(device, transformState, matrix);
    if (SUCCEEDED(result))
    {
        if (InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0 ||
            InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0)
        {
            ArmProjectionTargetSceneReadback(
                device,
                transformState,
                callerReturnAddress,
                externalCallerReturnAddress,
                externalStackReadable,
                result);
        }
        else
        {
            TryCaptureProjectionTargetDescriptor(
                device,
                transformState,
                callerReturnAddress,
                externalCallerReturnAddress,
                externalStackReadable,
                result);
        }
    }
    return result;
}

// The default bridge probe is bookkeeping only. The separate target probes use
// their own SetTransform detour and perform a bounded descriptor, owned-copy,
// or reset-aware stream transaction at the ordinary-world Projection boundary.
HRESULT WINAPI HookPresentBridgeProbe(
    void* device,
    const RECT* sourceRectangle,
    const RECT* destinationRectangle,
    HWND destinationWindowOverride,
    const RGNDATA* dirtyRegion)
{
    const LONG callCount = InterlockedIncrement(&g_presentBridgeProbeCallCount);
    const LONG threadId = static_cast<LONG>(GetCurrentThreadId());
    InterlockedExchange(&g_presentBridgeProbeLastThreadId, threadId);
    if (callCount == 1)
    {
        InterlockedCompareExchange(&g_presentBridgeProbeFirstThreadId, threadId, 0);
        LARGE_INTEGER counter = {};
        if (QueryPerformanceCounter(&counter))
        {
            g_presentBridgeProbeFirstCounter = counter.QuadPart;
        }
    }

    LARGE_INTEGER counter = {};
    if (QueryPerformanceCounter(&counter))
    {
        g_presentBridgeProbeLastCounter = counter.QuadPart;
    }

    const PresentFn originalPresent = g_originalPresentForBridgeProbe;
    const HRESULT result = originalPresent == nullptr
        ? E_FAIL
        : originalPresent(device, sourceRectangle, destinationRectangle, destinationWindowOverride, dirtyRegion);
    if (InterlockedCompareExchange(&g_surfaceStreamProbeRequested, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_projectionTargetStreamProbe.stopRequested, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_projectionTargetStreamProbe.state, 0, 0) == 1 &&
        device != nullptr &&
        GetCurrentThreadId() == g_createDeviceBreakpoint.threadId)
    {
        CompleteProjectionTargetStream(kProjectionTargetStreamCompletionWorkerTimeout);
    }
    return result;
}

DWORD WINAPI RunPresentBridgeProbe(void*)
{
    constexpr DWORD kLifecycleReadyTimeoutMs = 60000;
    constexpr DWORD kProbeObservationWindowMs = 5000;
    constexpr DWORD kSurfaceDescriptorProbeWindowMs = 70000;
    constexpr DWORD kSurfaceStreamProbeWindowMs = 70000;
    constexpr DWORD kSurfaceStreamCleanupWindowMs = 5000;
    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kLifecycleReadyTimeoutMs)
    {
        if (g_createDeviceBreakpoint.presentTarget != nullptr &&
            g_createDeviceBreakpoint.resetObserved &&
            g_createDeviceBreakpoint.postResetPresentObserved &&
            InterlockedCompareExchange(&g_createDeviceBreakpoint.stage, 0, 0) == 6)
        {
            break;
        }
        Sleep(10);
    }

    void* const presentTarget = g_createDeviceBreakpoint.presentTarget;
    if (presentTarget == nullptr ||
        !g_createDeviceBreakpoint.resetObserved ||
        !g_createDeviceBreakpoint.postResetPresentObserved ||
        InterlockedCompareExchange(&g_createDeviceBreakpoint.stage, 0, 0) != 6)
    {
        AppendLog(L"No-op Present bridge probe skipped: the verified CreateDevice/Reset/Present lifecycle boundary did not complete within %lu ms.", kLifecycleReadyTimeoutMs);
        return 0;
    }
    if (!IsSystemD3D8Target(presentTarget))
    {
        AppendLog(L"No-op Present bridge probe skipped: target=%p did not resolve to the expected d3d8.dll module.", presentTarget);
        return 0;
    }

    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK)
    {
        AppendLog(L"No-op Present bridge probe skipped: MH_Initialize failed (%d).", static_cast<int>(initializeStatus));
        return 0;
    }

    g_presentBridgeProbeTarget = presentTarget;
    const MH_STATUS createStatus = MH_CreateHook(
        presentTarget,
        reinterpret_cast<LPVOID>(&HookPresentBridgeProbe),
        reinterpret_cast<LPVOID*>(&g_originalPresentForBridgeProbe));
    if (createStatus != MH_OK || g_originalPresentForBridgeProbe == nullptr)
    {
        AppendLog(L"No-op Present bridge probe skipped: MH_CreateHook target=%p status=%d trampoline=%p.",
            presentTarget,
            static_cast<int>(createStatus),
            reinterpret_cast<void*>(g_originalPresentForBridgeProbe));
        MH_Uninitialize();
        return 0;
    }

    const MH_STATUS enableStatus = MH_EnableHook(presentTarget);
    if (enableStatus != MH_OK)
    {
        AppendLog(L"No-op Present bridge probe skipped: MH_EnableHook target=%p failed (%d).", presentTarget, static_cast<int>(enableStatus));
        MH_RemoveHook(presentTarget);
        MH_Uninitialize();
        return 0;
    }

    InterlockedExchange(&g_presentBridgeProbeEnabled, 1);
    const BOOL descriptorRequested = InterlockedCompareExchange(&g_surfaceDescriptorProbeRequested, 0, 0) != 0;
    const BOOL copyRequested = InterlockedCompareExchange(&g_surfaceCopyProbeRequested, 0, 0) != 0;
    const BOOL streamRequested = InterlockedCompareExchange(&g_surfaceStreamProbeRequested, 0, 0) != 0;
    const BOOL resetProbeRequested = InterlockedCompareExchange(&g_surfaceResetProbeRequested, 0, 0) != 0;
    const BOOL sceneReadbackRequested = InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0;
    const BOOL d3d11UploadRequested = InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0;
    const BOOL postSceneCaptureRequested = sceneReadbackRequested || d3d11UploadRequested;
    const BOOL readbackRequested =
        InterlockedCompareExchange(&g_surfaceReadbackProbeRequested, 0, 0) != 0 ||
        postSceneCaptureRequested;
    AppendLog(
        descriptorRequested
            ? (d3d11UploadRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in post-EndScene D3D11 upload probe will transiently read back the exact ordinary-world target, upload it into a BFVR-owned texture, verify five pixels, and release every D3D8/D3D11 resource. It creates no OpenXR session."
                : sceneReadbackRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in post-EndScene readback probe will arm the exact Projection setup, then copy once only after the following EndScene returns on the same device thread."
                : readbackRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in readback probe will arm a forwarding SetTransform detour and copy the exact ordinary-world target once into a transient system-memory image."
                : resetProbeRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in reset-lifecycle probe will arm forwarding SetTransform and Reset detours, retain one BFVR-owned target until a real Reset, then require recreation at the next exact Projection boundary."
                : streamRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in reset-aware owned-surface stream will arm forwarding SetTransform and Reset detours, then wait for the evidence-backed ordinary-world Projection return chain in an active map."
                : copyRequested
                ? L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in owned-surface copy probe will arm a forwarding SetTransform detour and wait for the evidence-backed ordinary-world Projection return chain in an active map."
                : L"Enabled the no-op D3D8 Present bridge at target=%p after the verified lifecycle trace; the separately opt-in target descriptor probe will arm a forwarding SetTransform detour and wait for the evidence-backed ordinary-world Projection return chain in an active map.")
            : L"Enabled the explicit no-op D3D8 Present bridge probe at target=%p after the verified CreateDevice/Reset/Present lifecycle trace released the device thread; it records only atomic counters/QPC and immediately calls the original trampoline.",
        presentTarget);

    if (descriptorRequested)
    {
        void* const setTransformTarget = g_createDeviceBreakpoint.setTransformTarget;
        if (setTransformTarget == nullptr || !IsSystemD3D8Target(setTransformTarget))
        {
            AppendLog(L"Projection-boundary target probe skipped: SetTransform target=%p did not resolve to the expected d3d8.dll module.", setTransformTarget);
            return 0;
        }

        if (streamRequested)
        {
            void* const resetTarget = g_createDeviceBreakpoint.resetTarget;
            if (resetTarget == nullptr || !IsSystemD3D8Target(resetTarget))
            {
                AppendLog(L"Projection-boundary stream probe skipped: Reset target=%p did not resolve to the expected d3d8.dll module.", resetTarget);
                return 0;
            }
            g_projectionTargetStreamResetTarget = resetTarget;
            const MH_STATUS createResetStatus = MH_CreateHook(
                resetTarget,
                reinterpret_cast<LPVOID>(&HookResetProjectionTargetStream),
                reinterpret_cast<LPVOID*>(&g_originalResetForStreamProbe));
            if (createResetStatus != MH_OK || g_originalResetForStreamProbe == nullptr)
            {
                AppendLog(L"Projection-boundary stream probe skipped: MH_CreateHook Reset target=%p status=%d trampoline=%p.",
                    resetTarget,
                    static_cast<int>(createResetStatus),
                    reinterpret_cast<void*>(g_originalResetForStreamProbe));
                return 0;
            }
            const MH_STATUS enableResetStatus = MH_EnableHook(resetTarget);
            if (enableResetStatus != MH_OK)
            {
                AppendLog(L"Projection-boundary stream probe skipped: MH_EnableHook Reset target=%p failed (%d).",
                    resetTarget,
                    static_cast<int>(enableResetStatus));
                MH_RemoveHook(resetTarget);
                return 0;
            }
        }

        if (postSceneCaptureRequested)
        {
            void* const endSceneTarget = g_createDeviceBreakpoint.endSceneTarget;
            if (endSceneTarget == nullptr || !IsSystemD3D8Target(endSceneTarget))
            {
                AppendLog(L"Post-EndScene capture probe skipped: EndScene target=%p did not resolve to the expected d3d8.dll module.", endSceneTarget);
                return 0;
            }
            g_projectionTargetSceneReadbackEndSceneTarget = endSceneTarget;
            const MH_STATUS createEndSceneStatus = MH_CreateHook(
                endSceneTarget,
                reinterpret_cast<LPVOID>(&HookEndSceneProjectionTargetReadback),
                reinterpret_cast<LPVOID*>(&g_originalEndSceneForSceneReadbackProbe));
            if (createEndSceneStatus != MH_OK || g_originalEndSceneForSceneReadbackProbe == nullptr)
            {
                AppendLog(L"Post-EndScene capture probe skipped: MH_CreateHook EndScene target=%p status=%d trampoline=%p.",
                    endSceneTarget,
                    static_cast<int>(createEndSceneStatus),
                    reinterpret_cast<void*>(g_originalEndSceneForSceneReadbackProbe));
                return 0;
            }
            const MH_STATUS enableEndSceneStatus = MH_EnableHook(endSceneTarget);
            if (enableEndSceneStatus != MH_OK)
            {
                AppendLog(L"Post-EndScene capture probe skipped: MH_EnableHook EndScene target=%p failed (%d).",
                    endSceneTarget,
                    static_cast<int>(enableEndSceneStatus));
                MH_RemoveHook(endSceneTarget);
                return 0;
            }
        }

        g_projectionTargetDescriptorProbeTarget = setTransformTarget;
        g_projectionTargetStreamProbeTarget = setTransformTarget;
        LPVOID const setTransformDetour = streamRequested
            ? reinterpret_cast<LPVOID>(&HookSetTransformProjectionTargetStream)
            : reinterpret_cast<LPVOID>(&HookSetTransformProjectionTargetProbe);
        LPVOID* const originalSetTransformStorage = streamRequested
            ? reinterpret_cast<LPVOID*>(&g_originalSetTransformForStreamProbe)
            : reinterpret_cast<LPVOID*>(&g_originalSetTransformForDescriptorProbe);
        const MH_STATUS createSetTransformStatus = MH_CreateHook(
            setTransformTarget,
            setTransformDetour,
            originalSetTransformStorage);
        const BOOL setTransformTrampolineReady = streamRequested
            ? g_originalSetTransformForStreamProbe != nullptr
            : g_originalSetTransformForDescriptorProbe != nullptr;
        if (createSetTransformStatus != MH_OK || !setTransformTrampolineReady)
        {
            AppendLog(L"Projection-boundary target probe skipped: MH_CreateHook SetTransform target=%p status=%d trampoline=%p.",
                setTransformTarget,
                static_cast<int>(createSetTransformStatus),
                streamRequested
                    ? reinterpret_cast<void*>(g_originalSetTransformForStreamProbe)
                    : reinterpret_cast<void*>(g_originalSetTransformForDescriptorProbe));
            if (streamRequested)
            {
                MH_DisableHook(g_projectionTargetStreamResetTarget);
                MH_RemoveHook(g_projectionTargetStreamResetTarget);
            }
            if (postSceneCaptureRequested)
            {
                MH_DisableHook(g_projectionTargetSceneReadbackEndSceneTarget);
                MH_RemoveHook(g_projectionTargetSceneReadbackEndSceneTarget);
            }
            return 0;
        }
        const MH_STATUS enableSetTransformStatus = MH_EnableHook(setTransformTarget);
        if (enableSetTransformStatus != MH_OK)
        {
            AppendLog(L"Projection-boundary target probe skipped: MH_EnableHook SetTransform target=%p failed (%d).",
                setTransformTarget,
                static_cast<int>(enableSetTransformStatus));
            MH_RemoveHook(setTransformTarget);
            if (streamRequested)
            {
                MH_DisableHook(g_projectionTargetStreamResetTarget);
                MH_RemoveHook(g_projectionTargetStreamResetTarget);
            }
            if (postSceneCaptureRequested)
            {
                MH_DisableHook(g_projectionTargetSceneReadbackEndSceneTarget);
                MH_RemoveHook(g_projectionTargetSceneReadbackEndSceneTarget);
            }
            return 0;
        }
        AppendLog(
            d3d11UploadRequested
                ? L"Enabled one-shot post-EndScene D3D11 upload probe at SetTransform=%p; the exact profiled Projection chain arms a borrowed same-thread handoff, and only the following original EndScene return may copy into a transient image, upload into a BFVR-owned D3D11 texture, verify five pixels, and release every D3D8/D3D11 resource without creating an OpenXR session."
                : sceneReadbackRequested
                ? L"Enabled one-shot post-EndScene system-memory readback probe at SetTransform=%p; the exact profiled Projection chain arms a borrowed same-thread handoff, and only the following original EndScene return may copy into a transient image surface, lock five pixels, and release every temporary reference."
                : readbackRequested
                ? L"Enabled one-shot Projection-boundary system-memory readback probe at SetTransform target=%p; every call forwards unchanged, and only the exact profiled state-3 return chain may copy the current target into a transient image surface, lock five pixels, and release both temporary surfaces before returning."
                : resetProbeRequested
                ? L"Enabled Projection-boundary reset-lifecycle probe at SetTransform=%p and Reset=%p; it will hold one BFVR-owned target after its first successful copy until a real Reset, release before the original Reset, then require a successful recreation/copy on the next exact Projection boundary."
                : streamRequested
                ? L"Enabled bounded Projection-boundary owned-surface stream probe at SetTransform=%p and Reset=%p; only the exact profiled state-3 chain may retain one BFVR-owned target, copy at most 60 frames, release before original Reset, and recreate only after a later verified Projection boundary."
                : copyRequested
                ? L"Enabled one-shot Projection-boundary owned-surface copy probe at SetTransform target=%p; every call forwards unchanged, and only the exact profiled state-3 return chain may perform one GetRenderTarget/GetDesc/CreateRenderTarget/CopyRects transaction with balanced releases."
                : L"Enabled one-shot Projection-boundary current-target descriptor probe at SetTransform target=%p; every call forwards unchanged, and only state=3 with callerReturn=0045FE21, externalCaller=00466F56, the profiled local-camera interface present, and the verified device thread may call GetRenderTarget/GetDesc/Release.",
            setTransformTarget,
            g_projectionTargetStreamResetTarget);

        if (streamRequested)
        {
            const DWORD streamStartedAt = GetTickCount();
            while (GetTickCount() - streamStartedAt < kSurfaceStreamProbeWindowMs &&
                   InterlockedCompareExchange(&g_projectionTargetStreamProbe.state, 0, 0) != 2)
            {
                Sleep(10);
            }
            if (InterlockedCompareExchange(&g_projectionTargetStreamProbe.state, 0, 0) != 2)
            {
                InterlockedExchange(&g_projectionTargetStreamProbe.stopRequested, 1);
                const DWORD cleanupStartedAt = GetTickCount();
                while (GetTickCount() - cleanupStartedAt < kSurfaceStreamCleanupWindowMs &&
                       InterlockedCompareExchange(&g_projectionTargetStreamProbe.state, 0, 0) != 2)
                {
                    Sleep(10);
                }
            }

            ProjectionTargetStreamProbeRecord& streamRecord = g_projectionTargetStreamProbe;
            const LONG streamState = InterlockedCompareExchange(&streamRecord.state, 0, 0);
            if (streamState != 2)
            {
                AppendLog(L"Projection-boundary owned-surface stream probe did not reach a device-thread cleanup point within %lu ms; it requested cleanup on the next Present and performed no cross-thread D3D release. state=%ld copies=%ld ownedRefOutstanding=%d sourceRefOutstanding=%d.",
                    kSurfaceStreamProbeWindowMs + kSurfaceStreamCleanupWindowMs,
                    streamState,
                    InterlockedCompareExchange(&streamRecord.copySuccessCount, 0, 0),
                    streamRecord.ownedReferenceOutstanding,
                    streamRecord.sourceReferenceOutstanding);
                return 0;
            }

            const D3DSurfaceDescription& sourceDescription = streamRecord.lastSourceDescription;
            const D3DSurfaceDescription& ownedDescription = streamRecord.ownedDescription;
            AppendLog(L"Projection-boundary owned-surface stream result: completionReason=%lu resetProbe=%d firstThread=%lu lastThread=%lu state=%lu callerReturn=%p externalCaller=%08lX copies=%ld/%ld copyAttempts=%ld creates=%ld releases=%ld sourceAcquires=%ld sourceReleases=%ld resetCalls=%ld resetReleases=%ld resetSuccesses=%ld resetObservedWithOwnedTarget=%d copiesAtReset=%ld awaitingPostResetRecreation=%d postResetRecreationVerified=%d lastReset=0x%08lX resetParametersCaptured=%d.",
                static_cast<unsigned long>(streamRecord.completionReason),
                streamRecord.resetProbeRequested,
                streamRecord.firstExecutionThreadId,
                streamRecord.lastExecutionThreadId,
                streamRecord.transformState,
                streamRecord.callerReturnAddress,
                static_cast<unsigned long>(streamRecord.externalCallerReturnAddress),
                InterlockedCompareExchange(&streamRecord.copySuccessCount, 0, 0),
                kProjectionStreamCopyLimit,
                InterlockedCompareExchange(&streamRecord.copyAttemptCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.ownedCreateCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.ownedReleaseCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.sourceAcquireCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.sourceReleaseCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.resetHookCallCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.resetReleaseCount, 0, 0),
                InterlockedCompareExchange(&streamRecord.resetSuccessCount, 0, 0),
                streamRecord.resetObservedWithOwnedTarget,
                streamRecord.copySuccessCountAtReset,
                streamRecord.awaitingPostResetRecreation,
                streamRecord.postResetRecreationVerified,
                static_cast<unsigned long>(streamRecord.lastResetResult),
                streamRecord.resetParametersCaptured);
            AppendLog(L"Projection-boundary owned-surface stream accounting: lastGetRenderTarget=0x%08lX lastGetDesc=0x%08lX lastCreate=0x%08lX lastOwnedGetDesc=0x%08lX lastCopy=0x%08lX sourceFormat=%u sourceType=%u sourceUsage=0x%08lX sourcePool=%u sourceMultisample=%u sourceSize=%ux%u sourceGate=%d ownedFormat=%u ownedType=%u ownedUsage=0x%08lX ownedPool=%u ownedMultisample=%u ownedSize=%ux%u createInSystemD3D8=%d copyInSystemD3D8=%d lastOwnedRelease=%lu lastSourceRelease=%lu ownedRefOutstanding=%d sourceRefOutstanding=%d resetSafeAtReturn=%d structuredException=%d.",
                static_cast<unsigned long>(streamRecord.lastGetRenderTargetResult),
                static_cast<unsigned long>(streamRecord.lastGetDescResult),
                static_cast<unsigned long>(streamRecord.lastCreateRenderTargetResult),
                static_cast<unsigned long>(streamRecord.lastOwnedGetDescResult),
                static_cast<unsigned long>(streamRecord.lastCopyRectsResult),
                sourceDescription.format,
                sourceDescription.type,
                static_cast<unsigned long>(sourceDescription.usage),
                sourceDescription.pool,
                sourceDescription.multiSampleType,
                sourceDescription.width,
                sourceDescription.height,
                streamRecord.lastSourceSafetyGatePassed,
                ownedDescription.format,
                ownedDescription.type,
                static_cast<unsigned long>(ownedDescription.usage),
                ownedDescription.pool,
                ownedDescription.multiSampleType,
                ownedDescription.width,
                ownedDescription.height,
                streamRecord.createRenderTargetInSystemD3D8,
                streamRecord.copyRectsInSystemD3D8,
                streamRecord.lastOwnedReleaseResult,
                streamRecord.lastSourceReleaseResult,
                streamRecord.ownedReferenceOutstanding,
                streamRecord.sourceReferenceOutstanding,
                streamRecord.resetSafeAtReturn,
                streamRecord.structuredException);
            return 0;
        }

        const DWORD descriptorStartedAt = GetTickCount();
        while (GetTickCount() - descriptorStartedAt < kSurfaceDescriptorProbeWindowMs &&
               InterlockedCompareExchange(&g_projectionTargetDescriptorProbe.state, 0, 0) != 2)
        {
            Sleep(10);
        }

        const LONG descriptorState = InterlockedCompareExchange(&g_projectionTargetDescriptorProbe.state, 0, 0);
        if (descriptorState != 2)
        {
            AppendLog(
                readbackRequested
                    ? L"Projection-boundary system-memory readback probe did not receive the evidence-backed active-map state-3 return chain within %lu ms; it made no D3D8 resource call."
                    : copyRequested
                    ? L"Projection-boundary owned-surface copy probe did not receive the evidence-backed active-map state-3 return chain within %lu ms; it made no D3D8 resource call."
                    : L"Projection-boundary current-target descriptor probe did not receive the evidence-backed active-map state-3 return chain within %lu ms; it made no D3D8 resource call.",
                kSurfaceDescriptorProbeWindowMs);
            return 0;
        }

        const ProjectionTargetDescriptorProbeRecord& record = g_projectionTargetDescriptorProbe;
        if (readbackRequested)
        {
            const D3DSurfaceDescription& sourceDescription = record.description;
            const D3DSurfaceDescription& readbackDescription = record.readbackDescription;
            AppendLog(L"Projection-boundary system-memory readback source: deviceThread=%lu state=%lu callerReturn=%p externalCaller=%08lX setTransform=0x%08lX acquiredSurface=%p getRenderTarget=0x%08lX getDesc=0x%08lX sourceFormat=%u sourceType=%u sourceUsage=0x%08lX sourcePool=%u sourceMultisample=%u sourceSize=%ux%u safetyGate=%d.",
                record.executionThreadId,
                record.transformState,
                record.callerReturnAddress,
                static_cast<unsigned long>(record.externalCallerReturnAddress),
                static_cast<unsigned long>(record.setTransformResult),
                record.acquiredSurface,
                static_cast<unsigned long>(record.getRenderTargetResult),
                static_cast<unsigned long>(record.getDescResult),
                sourceDescription.format,
                sourceDescription.type,
                static_cast<unsigned long>(sourceDescription.usage),
                sourceDescription.pool,
                sourceDescription.multiSampleType,
                sourceDescription.width,
                sourceDescription.height,
                record.copySafetyGatePassed);
            AppendLog(L"Projection-boundary system-memory readback transaction: afterEndScene=%d createImageTarget=%p createImageInSystemD3D8=%d createImage=0x%08lX readbackSurface=%p readbackGetDesc=%p readbackGetDescResult=0x%08lX readbackFormat=%u readbackType=%u readbackUsage=0x%08lX readbackPool=%u readbackSize=%ux%u matchesSource=%d copyTarget=%p copyInSystemD3D8=%d copyAttempted=%d copy=0x%08lX lockTarget=%p lockInSystemD3D8=%d lock=0x%08lX unlockTarget=%p unlockInSystemD3D8=%d unlock=0x%08lX pitch=%ld pixelsReadable=%d nonZeroPixels=%lu pixels=[%08lX,%08lX,%08lX,%08lX,%08lX] readbackReleaseCalled=%d readbackReleaseResult=%lu sourceReleaseCalled=%d sourceReleaseResult=%lu readbackRefOutstanding=%d sourceRefOutstanding=%d resetSafeAtReturn=%d structuredException=%d.",
                record.readbackAfterEndScene,
                record.createImageSurfaceTarget,
                record.createImageSurfaceInSystemD3D8,
                static_cast<unsigned long>(record.createImageSurfaceResult),
                record.readbackSurface,
                record.readbackGetDescTarget,
                static_cast<unsigned long>(record.readbackGetDescResult),
                readbackDescription.format,
                readbackDescription.type,
                static_cast<unsigned long>(readbackDescription.usage),
                readbackDescription.pool,
                readbackDescription.width,
                readbackDescription.height,
                record.readbackDescriptionMatchesSource,
                record.copyRectsTarget,
                record.copyRectsInSystemD3D8,
                record.readbackCopyAttempted,
                static_cast<unsigned long>(record.readbackCopyRectsResult),
                record.readbackLockRectTarget,
                record.readbackLockRectInSystemD3D8,
                static_cast<unsigned long>(record.readbackLockRectResult),
                record.readbackUnlockRectTarget,
                record.readbackUnlockRectInSystemD3D8,
                static_cast<unsigned long>(record.readbackUnlockRectResult),
                record.readbackPitch,
                record.readbackPixelsReadable,
                static_cast<unsigned long>(record.readbackNonZeroPixelCount),
                static_cast<unsigned long>(record.readbackPixels[0]),
                static_cast<unsigned long>(record.readbackPixels[1]),
                static_cast<unsigned long>(record.readbackPixels[2]),
                static_cast<unsigned long>(record.readbackPixels[3]),
                static_cast<unsigned long>(record.readbackPixels[4]),
                record.readbackReleaseCalled,
                record.readbackReleaseResult,
                record.releaseCalled,
                record.releaseResult,
                record.readbackReferenceOutstanding,
                record.sourceReferenceOutstanding,
                record.resetSafeAtReturn,
                record.structuredException);
            if (record.d3d11UploadRequested)
            {
                AppendLog(L"Post-EndScene D3D11 upload transaction: format22ToB8G8R8X8Accepted=%d createDevice=0x%08lX featureLevel=0x%04X createTexture=0x%08lX createStagingTexture=0x%08lX uploadAttempted=%d map=0x%08lX stagingPitch=%ld fivePixelsMatchSource=%d pixels=[%08lX,%08lX,%08lX,%08lX,%08lX] resourcesReleased=%d. This is a BFVR-owned no-session bridge only; no OpenXR graphics binding or composition layer was created.",
                    record.d3d11FormatMappingAccepted,
                    static_cast<unsigned long>(record.d3d11CreateDeviceResult),
                    static_cast<unsigned int>(record.d3d11FeatureLevel),
                    static_cast<unsigned long>(record.d3d11CreateTextureResult),
                    static_cast<unsigned long>(record.d3d11CreateStagingTextureResult),
                    record.d3d11UploadAttempted,
                    static_cast<unsigned long>(record.d3d11MapResult),
                    record.d3d11ReadbackPitch,
                    record.d3d11PixelsMatchSource,
                    static_cast<unsigned long>(record.d3d11VerificationPixels[0]),
                    static_cast<unsigned long>(record.d3d11VerificationPixels[1]),
                    static_cast<unsigned long>(record.d3d11VerificationPixels[2]),
                    static_cast<unsigned long>(record.d3d11VerificationPixels[3]),
                    static_cast<unsigned long>(record.d3d11VerificationPixels[4]),
                    record.d3d11ResourcesReleased);
            }
            return 0;
        }
        if (copyRequested)
        {
            const D3DSurfaceDescription& sourceDescription = record.description;
            const D3DSurfaceDescription& ownedDescription = record.ownedDescription;
            AppendLog(L"Projection-boundary owned-surface copy source: deviceThread=%lu state=%lu callerReturn=%p externalCaller=%08lX setTransform=0x%08lX acquiredSurface=%p getRenderTarget=0x%08lX getDesc=0x%08lX sourceFormat=%u sourceType=%u sourceUsage=0x%08lX sourcePool=%u sourceMultisample=%u sourceSize=%ux%u safetyGate=%d.",
                record.executionThreadId,
                record.transformState,
                record.callerReturnAddress,
                static_cast<unsigned long>(record.externalCallerReturnAddress),
                static_cast<unsigned long>(record.setTransformResult),
                record.acquiredSurface,
                static_cast<unsigned long>(record.getRenderTargetResult),
                static_cast<unsigned long>(record.getDescResult),
                sourceDescription.format,
                sourceDescription.type,
                static_cast<unsigned long>(sourceDescription.usage),
                sourceDescription.pool,
                sourceDescription.multiSampleType,
                sourceDescription.width,
                sourceDescription.height,
                record.copySafetyGatePassed);
            AppendLog(L"Projection-boundary owned-surface copy transaction: createTarget=%p createInSystemD3D8=%d create=0x%08lX ownedSurface=%p ownedGetDesc=%p ownedGetDescResult=0x%08lX ownedFormat=%u ownedType=%u ownedUsage=0x%08lX ownedPool=%u ownedMultisample=%u ownedSize=%ux%u ownedMatchesSource=%d copyTarget=%p copyInSystemD3D8=%d copyAttempted=%d copy=0x%08lX ownedReleaseCalled=%d ownedReleaseResult=%lu sourceReleaseCalled=%d sourceReleaseResult=%lu ownedRefOutstanding=%d sourceRefOutstanding=%d resetSafeAtReturn=%d structuredException=%d.",
                record.createRenderTargetTarget,
                record.createRenderTargetInSystemD3D8,
                static_cast<unsigned long>(record.createRenderTargetResult),
                record.ownedSurface,
                record.ownedGetDescTarget,
                static_cast<unsigned long>(record.ownedGetDescResult),
                ownedDescription.format,
                ownedDescription.type,
                static_cast<unsigned long>(ownedDescription.usage),
                ownedDescription.pool,
                ownedDescription.multiSampleType,
                ownedDescription.width,
                ownedDescription.height,
                record.ownedDescriptionMatchesSource,
                record.copyRectsTarget,
                record.copyRectsInSystemD3D8,
                record.copyAttempted,
                static_cast<unsigned long>(record.copyRectsResult),
                record.ownedReleaseCalled,
                record.ownedReleaseResult,
                record.releaseCalled,
                record.releaseResult,
                record.ownedReferenceOutstanding,
                record.sourceReferenceOutstanding,
                record.resetSafeAtReturn,
                record.structuredException);
            return 0;
        }
        if (record.descriptionReadable)
        {
            const D3DSurfaceDescription& description = record.description;
            AppendLog(L"Projection-boundary current-target descriptor result: deviceThread=%lu state=%lu callerReturn=%p externalCaller=%08lX externalStackReadable=%d setTransform=0x%08lX acquiredSurface=%p getRenderTarget=0x%08lX getDescTarget=%p getDesc=0x%08lX releaseCalled=%d releaseResult=%lu format=%u type=%u usage=0x%08lX pool=%u bytes=%u multisample=%u size=%ux%u matchesBackbufferSize=%d structuredException=%d.",
                record.executionThreadId,
                record.transformState,
                record.callerReturnAddress,
                static_cast<unsigned long>(record.externalCallerReturnAddress),
                record.externalStackReadable,
                static_cast<unsigned long>(record.setTransformResult),
                record.acquiredSurface,
                static_cast<unsigned long>(record.getRenderTargetResult),
                record.getDescTarget,
                static_cast<unsigned long>(record.getDescResult),
                record.releaseCalled,
                record.releaseResult,
                description.format,
                description.type,
                static_cast<unsigned long>(description.usage),
                description.pool,
                description.size,
                description.multiSampleType,
                description.width,
                description.height,
                g_createDeviceBreakpoint.presentationReadable &&
                    description.width == g_createDeviceBreakpoint.presentation.backBufferWidth &&
                    description.height == g_createDeviceBreakpoint.presentation.backBufferHeight,
                record.structuredException);
        }
        else
        {
            AppendLog(L"Projection-boundary current-target descriptor probe completed without a readable descriptor: deviceThread=%lu state=%lu callerReturn=%p externalCaller=%08lX externalStackReadable=%d setTransform=0x%08lX acquiredSurface=%p getRenderTarget=0x%08lX getDescTarget=%p getDesc=0x%08lX releaseCalled=%d releaseResult=%lu structuredException=%d.",
                record.executionThreadId,
                record.transformState,
                record.callerReturnAddress,
                static_cast<unsigned long>(record.externalCallerReturnAddress),
                record.externalStackReadable,
                static_cast<unsigned long>(record.setTransformResult),
                record.acquiredSurface,
                static_cast<unsigned long>(record.getRenderTargetResult),
                record.getDescTarget,
                static_cast<unsigned long>(record.getDescResult),
                record.releaseCalled,
                record.releaseResult,
                record.structuredException);
        }
        return 0;
    }

    Sleep(kProbeObservationWindowMs);
    const LONG callCount = InterlockedCompareExchange(&g_presentBridgeProbeCallCount, 0, 0);
    const LONG firstThreadId = InterlockedCompareExchange(&g_presentBridgeProbeFirstThreadId, 0, 0);
    const LONG lastThreadId = InterlockedCompareExchange(&g_presentBridgeProbeLastThreadId, 0, 0);
    const DWORD expectedThreadId = g_createDeviceBreakpoint.threadId;
    const BOOL threadMatches = firstThreadId != 0 &&
        firstThreadId == static_cast<LONG>(expectedThreadId) &&
        lastThreadId == static_cast<LONG>(expectedThreadId);
    AppendLog(
        L"No-op D3D8 Present bridge probe result: target=%p calls=%ld firstThread=%ld lastThread=%ld expectedDeviceThread=%lu threadMatches=%d firstQpc=%lld lastQpc=%lld. The hook remains installed only until this diagnostic process exits.",
        g_presentBridgeProbeTarget,
        callCount,
        firstThreadId,
        lastThreadId,
        expectedThreadId,
        threadMatches,
        g_presentBridgeProbeFirstCounter,
        g_presentBridgeProbeLastCounter);
    return 0;
}

void StartPresentBridgeProbe()
{
    if (InterlockedCompareExchange(&g_presentBridgeProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_presentBridgeProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunPresentBridgeProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"No-op Present bridge probe could not start (%lu); no D3D8 code was patched.", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(
        InterlockedCompareExchange(&g_surfaceD3D11UploadProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world post-EndScene D3D11 upload probe; it arms only at the exact Projection chain, creates a transient BFVR-owned D3D11 device/texture from the following EndScene readback, verifies five uploaded pixels, releases all resources, and does not create an OpenXR session."
            : InterlockedCompareExchange(&g_surfaceSceneReadbackProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world post-EndScene system-memory readback probe; it arms only at the exact Projection chain, performs its transient image-surface copy only after the following EndScene returns on that thread, then releases every temporary reference."
            : InterlockedCompareExchange(&g_surfaceReadbackProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world Projection-boundary system-memory readback probe; it remains inactive until the verified lifecycle trace releases the device thread, then performs one transient image-surface copy and five-pixel lock/read before releasing every temporary reference."
            : InterlockedCompareExchange(&g_surfaceResetProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world Projection-boundary reset-lifecycle probe; it will hold one BFVR-owned target after its first copy until an observed Reset, then verify recreation at the next exact Projection boundary."
            : InterlockedCompareExchange(&g_surfaceStreamProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world Projection-boundary reset-aware stream probe; it remains inactive until the verified lifecycle trace releases the device thread, then retains one BFVR-owned target for at most 60 copies and releases it before any observed Reset."
            : InterlockedCompareExchange(&g_surfaceCopyProbeRequested, 0, 0) != 0
            ? L"Requested the explicit ordinary-world Projection-boundary owned-surface copy probe; it remains inactive until the verified lifecycle trace releases the device thread, then performs at most one fully balanced copy transaction on the evidence-backed active-map return chain."
            : InterlockedCompareExchange(&g_surfaceDescriptorProbeRequested, 0, 0) != 0
                ? L"Requested the explicit ordinary-world Projection-boundary target descriptor probe; it remains inactive until the verified lifecycle trace releases the device thread, then its forwarding SetTransform hook waits for the evidence-backed active-map return chain."
            : L"Requested the explicit no-op Present bridge probe; it remains inactive until the verified CreateDevice/Reset/Present lifecycle trace has released the device thread.");
}

LONG CALLBACK HandleCreateDeviceBreakpoint(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    const LONG stage = InterlockedCompareExchange(&g_createDeviceBreakpoint.stage, 0, 0);
    DWORD_PTR expectedInstruction = 0;
    if (stage == 1)
    {
        expectedInstruction = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.target);
    }
    else if (stage == 2)
    {
        expectedInstruction = g_createDeviceBreakpoint.returnAddress;
    }
    else if (stage == 3)
    {
        expectedInstruction = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.presentTarget);
    }
    else if (stage == 4)
    {
        expectedInstruction = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.resetTarget);
    }

    if (exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        GetCurrentThreadId() != g_createDeviceBreakpoint.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (stage == 3 || stage == 4)
    {
        if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.clearTarget))
        {
            g_createDeviceBreakpoint.clearObserved = TRUE;
            g_createDeviceBreakpoint.clearSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr1 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.beginSceneTarget))
        {
            g_createDeviceBreakpoint.beginSceneObserved = TRUE;
            g_createDeviceBreakpoint.beginSceneSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr2 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
            if (stage == 5)
            {
                InterlockedExchange(&g_createDeviceBreakpoint.stage, 6);
                InterlockedExchange(&g_createDeviceBreakpoint.pending, 1);
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.endSceneTarget))
        {
            g_createDeviceBreakpoint.endSceneObserved = TRUE;
            g_createDeviceBreakpoint.endSceneSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr3 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    if (stage == 5)
    {
        if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.presentTarget))
        {
            g_createDeviceBreakpoint.postResetPresentObserved = TRUE;
            g_createDeviceBreakpoint.postResetPresentThreadId = GetCurrentThreadId();
            g_createDeviceBreakpoint.postResetPresentSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr0 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setRenderTargetTarget))
        {
            g_createDeviceBreakpoint.setRenderTargetObserved = TRUE;
            g_createDeviceBreakpoint.setRenderTargetSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr1 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        }
        else if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setTransformTarget))
        {
            g_createDeviceBreakpoint.setTransformObserved = TRUE;
            g_createDeviceBreakpoint.setTransformSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr2 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        }
        else if (context->Eip == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.beginSceneTarget))
        {
            g_createDeviceBreakpoint.beginSceneObserved = TRUE;
            g_createDeviceBreakpoint.beginSceneSequence = InterlockedIncrement(&g_createDeviceBreakpoint.passEventSequence);
            context->Dr3 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        }
        else
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        if (g_createDeviceBreakpoint.setRenderTargetObserved &&
            g_createDeviceBreakpoint.setTransformObserved &&
            g_createDeviceBreakpoint.beginSceneObserved)
        {
            context->Dr1 = 0;
            context->Dr2 = 0;
            context->Dr3 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0xFF);
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 6);
            InterlockedExchange(&g_createDeviceBreakpoint.pending, 1);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (expectedInstruction == 0 || context->Eip != expectedInstruction)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (stage == 1)
    {
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        __try
        {
            g_createDeviceBreakpoint.returnAddress = stack[0];
            g_createDeviceBreakpoint.direct3D = stack[1];
            g_createDeviceBreakpoint.adapter = stack[2];
            g_createDeviceBreakpoint.deviceType = stack[3];
            g_createDeviceBreakpoint.focusWindow = stack[4];
            g_createDeviceBreakpoint.behaviorFlags = stack[5];
            g_createDeviceBreakpoint.presentationParameters = stack[6];
            if (g_createDeviceBreakpoint.presentationParameters != 0)
            {
                g_createDeviceBreakpoint.presentation = *reinterpret_cast<const D3DPresentParameters*>(g_createDeviceBreakpoint.presentationParameters);
                g_createDeviceBreakpoint.presentationReadable = TRUE;
            }
            g_createDeviceBreakpoint.returnedDevice = stack[7];
            g_createDeviceBreakpoint.stackReadable = TRUE;
            context->Dr0 = g_createDeviceBreakpoint.returnAddress;
            context->Dr6 = 0;
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_createDeviceBreakpoint.stackReadable = FALSE;
            context->Dr0 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(1);
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 4);
            InterlockedExchange(&g_createDeviceBreakpoint.pending, 1);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (stage == 2)
    {
        __try
        {
            g_createDeviceBreakpoint.device = *reinterpret_cast<void**>(g_createDeviceBreakpoint.returnedDevice);
            auto** deviceVtable = *reinterpret_cast<void***>(g_createDeviceBreakpoint.device);
            g_createDeviceBreakpoint.resetTarget = deviceVtable[kDirect3DDevice8ResetSlot];
            g_createDeviceBreakpoint.presentTarget = deviceVtable[kDirect3DDevice8PresentSlot];
            // IDirect3DDevice8's contiguous tail is UpdateTexture=29,
            // GetFrontBuffer=30, SetRenderTarget=31, GetRenderTarget=32,
            // GetDepthStencilSurface=33, BeginScene=34, EndScene=35,
            // Clear=36, and SetTransform=37.
            g_createDeviceBreakpoint.beginSceneTarget = deviceVtable[kDirect3DDevice8BeginSceneSlot];
            g_createDeviceBreakpoint.endSceneTarget = deviceVtable[kDirect3DDevice8EndSceneSlot];
            g_createDeviceBreakpoint.clearTarget = deviceVtable[kDirect3DDevice8ClearSlot];
            g_createDeviceBreakpoint.setTransformTarget = deviceVtable[kDirect3DDevice8SetTransformSlot];
            g_createDeviceBreakpoint.setRenderTargetTarget = deviceVtable[kDirect3DDevice8SetRenderTargetSlot];
            if (g_createDeviceBreakpoint.presentTarget == nullptr ||
                g_createDeviceBreakpoint.beginSceneTarget == nullptr ||
                g_createDeviceBreakpoint.endSceneTarget == nullptr ||
                g_createDeviceBreakpoint.clearTarget == nullptr ||
                g_createDeviceBreakpoint.setTransformTarget == nullptr ||
                g_createDeviceBreakpoint.setRenderTargetTarget == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
            context->Dr0 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.presentTarget);
            context->Dr1 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.clearTarget);
            context->Dr2 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.beginSceneTarget);
            context->Dr3 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.endSceneTarget);
            context->Dr6 = 0;
            context->Dr7 = (context->Dr7 & ~static_cast<DWORD_PTR>(0xFF)) | 0x55;
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 3);
            if (InterlockedCompareExchange(
                    &g_skipExtendedFrameDiagnostics,
                    0,
                    0) == 0)
            {
                StartCombinedFrameTrace();
            }
            else
            {
                AppendLog(
                    L"Skipped the legacy extended frame-diagnostics trace chain for the continuous shared-presentation request; lifecycle discovery remains active.");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            context->Dr0 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(1);
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 4);
            InterlockedExchange(&g_createDeviceBreakpoint.pending, 1);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (stage == 3)
    {
        g_createDeviceBreakpoint.presentObserved = TRUE;
        if (g_createDeviceBreakpoint.resetTarget != nullptr)
        {
            context->Dr0 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.resetTarget);
            context->Dr6 = 0;
            InterlockedExchange(&g_createDeviceBreakpoint.stage, 4);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    else
    {
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        __try
        {
            g_createDeviceBreakpoint.resetDevice = stack[1];
            g_createDeviceBreakpoint.resetPresentationParameters = stack[2];
            if (g_createDeviceBreakpoint.resetPresentationParameters != 0)
            {
                g_createDeviceBreakpoint.resetPresentation = *reinterpret_cast<const D3DPresentParameters*>(g_createDeviceBreakpoint.resetPresentationParameters);
                g_createDeviceBreakpoint.resetPresentationReadable = TRUE;
            }
            g_createDeviceBreakpoint.resetStackReadable = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_createDeviceBreakpoint.resetStackReadable = FALSE;
        }
        g_createDeviceBreakpoint.resetObserved = TRUE;
    }

    if (stage == 4)
    {
        context->Dr0 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.presentTarget);
        context->Dr1 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setRenderTargetTarget);
        context->Dr2 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setTransformTarget);
        context->Dr3 = reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.beginSceneTarget);
        context->Dr6 = 0;
        context->Dr7 = (context->Dr7 & ~static_cast<DWORD_PTR>(0xFF)) | 0x55;
        InterlockedExchange(&g_createDeviceBreakpoint.stage, 5);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    context->Dr0 = 0;
    context->Dr1 = 0;
    context->Dr2 = 0;
    context->Dr3 = 0;
    context->Dr6 = 0;
    context->Dr7 &= ~static_cast<DWORD_PTR>(0xFF);
    InterlockedExchange(&g_createDeviceBreakpoint.stage, 6);
    InterlockedExchange(&g_createDeviceBreakpoint.pending, 1);
    return EXCEPTION_CONTINUE_EXECUTION;
}

