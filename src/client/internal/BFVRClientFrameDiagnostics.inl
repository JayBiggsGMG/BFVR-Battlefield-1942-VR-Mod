void RestoreCombinedFrameTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_combinedFrameTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsCombinedFrameTraceStateOwned(context))
    {
        context.Dr0 = g_combinedFrameTrace.originalDr0;
        context.Dr1 = g_combinedFrameTrace.originalDr1;
        context.Dr2 = g_combinedFrameTrace.originalDr2;
        context.Dr3 = g_combinedFrameTrace.originalDr3;
        context.Dr6 = g_combinedFrameTrace.originalDr6;
        context.Dr7 = g_combinedFrameTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_combinedFrameTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

LONG CALLBACK HandleCombinedFrameTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_combinedFrameTrace.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_combinedFrameTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.presentTarget))
    {
        const LONG sampleIndex = InterlockedIncrement(&g_combinedFrameTrace.presentSamplesCaptured) - 1;
        if (sampleIndex >= 0 && sampleIndex < kCombinedPresentSampleMaximum)
        {
            CombinedPresentSample& sample = g_combinedFrameTrace.presentSamples[sampleIndex];
            sample.tick = GetTickCount();
            LARGE_INTEGER performanceCounter = {};
            if (QueryPerformanceCounter(&performanceCounter))
            {
                sample.performanceCounter = performanceCounter.QuadPart;
            }
        }
        if (sampleIndex + 1 >= kCombinedPresentSampleMaximum)
        {
            context->Dr0 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setTransformTarget))
    {
        const LONG sampleIndex = InterlockedIncrement(&g_combinedFrameTrace.transformSamplesCaptured) - 1;
        if (sampleIndex >= 0 && sampleIndex < kCombinedTransformSampleMaximum)
        {
            CombinedTransformSample& sample = g_combinedFrameTrace.transformSamples[sampleIndex];
            sample.tick = GetTickCount();
            const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
            __try
            {
                sample.ecx = static_cast<DWORD>(context->Ecx);
                sample.edx = static_cast<DWORD>(context->Edx);
                for (std::size_t wordIndex = 0; wordIndex < std::size(sample.stackWords); ++wordIndex)
                {
                    sample.stackWords[wordIndex] = stack[wordIndex];
                }
                sample.transformState = stack[2];
                sample.matrixAddress = stack[3];
                sample.stackReadable = TRUE;
                if (sample.matrixAddress != 0)
                {
                    std::memcpy(sample.matrix, reinterpret_cast<const void*>(sample.matrixAddress), sizeof(sample.matrix));
                    sample.matrixReadable = TRUE;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                sample.stackReadable = FALSE;
                sample.matrixReadable = FALSE;
            }
            if (sample.transformState == kD3DTransformProjection && !g_combinedFrameTrace.projectionBoundaryCaptured)
            {
                CaptureRendererTransformCaches(
                    g_combinedFrameTrace.rendererTransformCachesAtProjection,
                    g_combinedFrameTrace.rendererTransactionStateGlobal);
                CaptureLocalCameraTransformAtProjection(
                    g_combinedFrameTrace.localCameraAtProjection,
                    g_combinedFrameTrace.localCameraAddRefTarget);
                g_combinedFrameTrace.projectionBoundaryCaptured = TRUE;
            }
        }
        if (sampleIndex + 1 >= kCombinedTransformSampleMaximum)
        {
            context->Dr1 = 0;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    const LONG scenePhase = InterlockedCompareExchange(&g_combinedFrameTrace.scenePhase, 0, 0);
    if (scenePhase == 0 && context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.beginSceneTarget))
    {
        InterlockedExchange(&g_combinedFrameTrace.scenePhase, 1);
        context->Dr2 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.endSceneTarget);
        context->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (scenePhase == 1 && context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.endSceneTarget))
    {
        InterlockedExchange(&g_combinedFrameTrace.scenePhase, 2);
        context->Dr2 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.clearTarget);
        context->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (scenePhase == 2 && context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.clearTarget))
    {
        InterlockedExchange(&g_combinedFrameTrace.scenePhase, 3);
        context->Dr2 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setRenderTargetTarget))
    {
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        __try
        {
            g_combinedFrameTrace.setRenderTargetReturnAddress = stack[0];
            g_combinedFrameTrace.setRenderTargetColor = stack[2];
            g_combinedFrameTrace.setRenderTargetDepth = stack[3];
            g_combinedFrameTrace.setRenderTargetStackReadable = TRUE;
            if (g_combinedFrameTrace.setRenderTargetColor != 0)
            {
                auto** colorVtable = *reinterpret_cast<void***>(g_combinedFrameTrace.setRenderTargetColor);
                if (colorVtable != nullptr)
                {
                    g_combinedFrameTrace.setRenderTargetColorVtable =
                        static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(colorVtable));
                    g_combinedFrameTrace.setRenderTargetColorGetDescTarget =
                        static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(colorVtable[kDirect3DSurface8GetDescSlot]));
                    g_combinedFrameTrace.setRenderTargetColorInterfaceReadable = TRUE;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_combinedFrameTrace.setRenderTargetStackReadable = FALSE;
        }
        g_combinedFrameTrace.setRenderTargetTick = GetTickCount();
        g_combinedFrameTrace.setRenderTargetObserved = TRUE;
        context->Dr3 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmCombinedFrameTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0 || !g_createDeviceBreakpoint.resetObserved)
    {
        return false;
    }
    const auto* gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"Map-gated combined D3D8 trace skipped: game image is unavailable.");
        return false;
    }
    if (g_combinedFrameTraceHandler == nullptr)
    {
        g_combinedFrameTraceHandler = AddVectoredExceptionHandler(1, HandleCombinedFrameTrace);
        if (g_combinedFrameTraceHandler == nullptr)
        {
            AppendLog(L"Map-gated combined D3D8 trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    LARGE_INTEGER performanceCounterFrequency = {};
    if (!QueryPerformanceFrequency(&performanceCounterFrequency) || performanceCounterFrequency.QuadPart <= 0)
    {
        AppendLog(L"Map-gated combined D3D8 trace skipped: QueryPerformanceFrequency failed (%lu).", GetLastError());
        return false;
    }

    DisableCreateDeviceTraceForCombinedFrameTrace();
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Map-gated combined D3D8 trace skipped: unable to open the device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        AppendLog(L"Map-gated combined D3D8 trace skipped: unable to suspend the device thread (%lu).", GetLastError());
        CloseHandle(thread);
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated combined D3D8 trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_combinedFrameTrace = {};
    g_combinedFrameTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_combinedFrameTrace.presentTarget = g_createDeviceBreakpoint.presentTarget;
    g_combinedFrameTrace.setTransformTarget = g_createDeviceBreakpoint.setTransformTarget;
    g_combinedFrameTrace.rendererTransactionStateGlobal = gameImage + kRendererTransactionStateGlobalRva;
    g_combinedFrameTrace.localCameraAddRefTarget = gameImage + kLocalCameraAddRefRva;
    g_combinedFrameTrace.beginSceneTarget = g_createDeviceBreakpoint.beginSceneTarget;
    g_combinedFrameTrace.endSceneTarget = g_createDeviceBreakpoint.endSceneTarget;
    g_combinedFrameTrace.clearTarget = g_createDeviceBreakpoint.clearTarget;
    g_combinedFrameTrace.setRenderTargetTarget = g_createDeviceBreakpoint.setRenderTargetTarget;
    g_combinedFrameTrace.originalDr0 = context.Dr0;
    g_combinedFrameTrace.originalDr1 = context.Dr1;
    g_combinedFrameTrace.originalDr2 = context.Dr2;
    g_combinedFrameTrace.originalDr3 = context.Dr3;
    g_combinedFrameTrace.originalDr6 = context.Dr6;
    g_combinedFrameTrace.originalDr7 = context.Dr7;
    g_combinedFrameTrace.performanceCounterFrequency = performanceCounterFrequency.QuadPart;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.presentTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setTransformTarget);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.beginSceneTarget);
    context.Dr3 = reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setRenderTargetTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x55;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated combined D3D8 trace skipped: unable to arm device-thread breakpoints (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_combinedFrameTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed map-gated combined D3D8 trace on device thread %lu: 8 Presents, up to 12 SetTransform entries, an entry-aligned renderer/local-camera snapshot if state 3 (Projection) occurs, BeginScene->EndScene->Clear chain, SetRenderTarget, then a second read-only renderer cache snapshot at trace end.", g_combinedFrameTrace.threadId);
    return true;
}

void StartRendererPassTrace();

DWORD WINAPI RunCombinedFrameTrace(void*)
{
    constexpr DWORD kMapActiveWaitWindowMs = 80000;
    constexpr DWORD kCombinedTraceWindowMs = 30000;
    const DWORD mapActiveWaitStartedAt = GetTickCount();
    while (InterlockedCompareExchange(&g_profiledCameraInterfaceObserved, 0, 0) == 0 &&
           GetTickCount() - mapActiveWaitStartedAt < kMapActiveWaitWindowMs)
    {
        Sleep(50);
    }
    if (InterlockedCompareExchange(&g_profiledCameraInterfaceObserved, 0, 0) == 0)
    {
        AppendLog(L"Map-gated combined D3D8 trace skipped because the passive reader did not see the profiled local camera interface within 80 seconds.");
        return 0;
    }
    if (!ArmCombinedFrameTrace())
    {
        return 0;
    }

    const DWORD traceStartedAt = GetTickCount();
    while (GetTickCount() - traceStartedAt < kCombinedTraceWindowMs)
    {
        Sleep(10);
    }
    RestoreCombinedFrameTrace();
    InterlockedExchange(&g_combinedFrameTrace.state, 2);
    CaptureRendererTransformCaches(
        g_combinedFrameTrace.rendererTransformCachesAtTraceEnd,
        g_combinedFrameTrace.rendererTransactionStateGlobal);

    const LONG presentSamples = InterlockedCompareExchange(&g_combinedFrameTrace.presentSamplesCaptured, 0, 0);
    const LONG transformSamples = InterlockedCompareExchange(&g_combinedFrameTrace.transformSamplesCaptured, 0, 0);
    const LONG storedPresentSamples = presentSamples < kCombinedPresentSampleMaximum ? presentSamples : kCombinedPresentSampleMaximum;
    const DWORD firstPresentTick = storedPresentSamples > 0 ? g_combinedFrameTrace.presentSamples[0].tick : 0;
    const DWORD lastPresentTick = storedPresentSamples > 1 ? g_combinedFrameTrace.presentSamples[storedPresentSamples - 1].tick : 0;
    const LONGLONG firstPresentCounter = storedPresentSamples > 0 ? g_combinedFrameTrace.presentSamples[0].performanceCounter : 0;
    const LONGLONG lastPresentCounter = storedPresentSamples > 1 ? g_combinedFrameTrace.presentSamples[storedPresentSamples - 1].performanceCounter : 0;
    const double presentSpanMilliseconds =
        g_combinedFrameTrace.performanceCounterFrequency > 0 && lastPresentCounter >= firstPresentCounter
            ? (1000.0 * static_cast<double>(lastPresentCounter - firstPresentCounter) /
               static_cast<double>(g_combinedFrameTrace.performanceCounterFrequency))
            : 0.0;
    AppendLog(
        L"Map-gated combined D3D8 trace summary: PresentSamples=%ld spanMs=%lu qpcSpanMs=%.3f; SetTransformSamples=%ld; state3ProjectionBoundary=%d; scenePhase=%ld (0=Begin pending, 3=Begin/End/Clear); SetRenderTarget=%d return=%08lX color=%08lX depth=%08lX colorVtable=%08lX colorGetDesc=%08lX colorInterfaceReadable=%d; rendererCacheAtProjection=%d transaction=%08lX; rendererCacheAtEnd=%d transaction=%08lX; localCameraAtProjection=%d camera=%08lX; registersRestored=%d.",
        presentSamples,
        static_cast<unsigned long>(lastPresentTick - firstPresentTick),
        presentSpanMilliseconds,
        transformSamples,
        g_combinedFrameTrace.projectionBoundaryCaptured,
        InterlockedCompareExchange(&g_combinedFrameTrace.scenePhase, 0, 0),
        g_combinedFrameTrace.setRenderTargetObserved,
        static_cast<unsigned long>(g_combinedFrameTrace.setRenderTargetReturnAddress),
        static_cast<unsigned long>(g_combinedFrameTrace.setRenderTargetColor),
        static_cast<unsigned long>(g_combinedFrameTrace.setRenderTargetDepth),
        static_cast<unsigned long>(g_combinedFrameTrace.setRenderTargetColorVtable),
        static_cast<unsigned long>(g_combinedFrameTrace.setRenderTargetColorGetDescTarget),
        g_combinedFrameTrace.setRenderTargetColorInterfaceReadable,
        g_combinedFrameTrace.rendererTransformCachesAtProjection.readable,
        static_cast<unsigned long>(g_combinedFrameTrace.rendererTransformCachesAtProjection.rendererTransaction),
        g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.readable,
        static_cast<unsigned long>(g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.rendererTransaction),
        g_combinedFrameTrace.localCameraAtProjection.readable,
        static_cast<unsigned long>(g_combinedFrameTrace.localCameraAtProjection.cameraInterface),
        g_combinedFrameTrace.cleanupRestored);
    for (LONG index = 0; index < transformSamples && index < kCombinedTransformSampleMaximum; ++index)
    {
        const CombinedTransformSample& sample = g_combinedFrameTrace.transformSamples[index];
        AppendLog(
            L"Map-gated SetTransform sample %ld tick=%lu ecx=%08lX edx=%08lX stack=[%08lX %08lX %08lX %08lX %08lX %08lX %08lX] state=%lu matrix=%08lX stackReadable=%d matrixReadable=%d m=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f].",
            index + 1,
            static_cast<unsigned long>(sample.tick),
            static_cast<unsigned long>(sample.ecx),
            static_cast<unsigned long>(sample.edx),
            static_cast<unsigned long>(sample.stackWords[0]),
            static_cast<unsigned long>(sample.stackWords[1]),
            static_cast<unsigned long>(sample.stackWords[2]),
            static_cast<unsigned long>(sample.stackWords[3]),
            static_cast<unsigned long>(sample.stackWords[4]),
            static_cast<unsigned long>(sample.stackWords[5]),
            static_cast<unsigned long>(sample.stackWords[6]),
            static_cast<unsigned long>(sample.transformState),
            static_cast<unsigned long>(sample.matrixAddress),
            sample.stackReadable,
            sample.matrixReadable,
            static_cast<double>(sample.matrix[0]), static_cast<double>(sample.matrix[1]), static_cast<double>(sample.matrix[2]), static_cast<double>(sample.matrix[3]),
            static_cast<double>(sample.matrix[4]), static_cast<double>(sample.matrix[5]), static_cast<double>(sample.matrix[6]), static_cast<double>(sample.matrix[7]),
            static_cast<double>(sample.matrix[8]), static_cast<double>(sample.matrix[9]), static_cast<double>(sample.matrix[10]), static_cast<double>(sample.matrix[11]),
            static_cast<double>(sample.matrix[12]), static_cast<double>(sample.matrix[13]), static_cast<double>(sample.matrix[14]), static_cast<double>(sample.matrix[15]));
    }
    if (g_combinedFrameTrace.projectionBoundaryCaptured && g_combinedFrameTrace.rendererTransformCachesAtProjection.readable)
    {
        AppendRendererTransformCache(L"World at state-3 entry", g_combinedFrameTrace.rendererTransformCachesAtProjection.world);
        AppendRendererTransformCache(L"View at state-3 entry", g_combinedFrameTrace.rendererTransformCachesAtProjection.view);
        AppendRendererTransformCache(L"Projection at state-3 entry", g_combinedFrameTrace.rendererTransformCachesAtProjection.projection);
    }
    if (g_combinedFrameTrace.projectionBoundaryCaptured)
    {
        AppendLocalCameraTransformSample(g_combinedFrameTrace.localCameraAtProjection);
    }
    else
    {
        AppendLog(L"Map-gated combined D3D8 trace saw no state-3 Projection entry among its first %ld SetTransform samples; no entry-aligned renderer/local-camera correlation was recorded.", transformSamples);
    }
    if (g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.readable)
    {
        AppendRendererTransformCache(L"World at trace end", g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.world);
        AppendRendererTransformCache(L"View at trace end", g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.view);
        AppendRendererTransformCache(L"Projection at trace end", g_combinedFrameTrace.rendererTransformCachesAtTraceEnd.projection);
    }
    StartRendererPassTrace();
    return 0;
}

void StartCombinedFrameTrace()
{
    if (InterlockedCompareExchange(&g_combinedFrameTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunCombinedFrameTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Map-gated combined D3D8 trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one map-gated combined D3D8 trace worker; it will wait for the profiled local camera interface, not a non-zero candidate matrix.");
}

void CaptureRendererPassTraceEvent(RendererPassTraceEvent& event, const CONTEXT& context)
{
    event.tick = GetTickCount();
    event.instruction = static_cast<DWORD>(context.Eip);
    event.ecx = static_cast<DWORD>(context.Ecx);
    LARGE_INTEGER counter = {};
    if (QueryPerformanceCounter(&counter))
    {
        event.performanceCounter = counter.QuadPart;
    }

    __try
    {
        event.stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
        event.stackReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        event.stackReadable = FALSE;
    }
}

bool IsRendererPassTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.frameCoordinatorTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.rendererCoordinatorTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 && context.Dr2 != reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 && context.Dr3 != reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewReturnTarget))
    {
        return false;
    }
    return true;
}

void RestoreRendererPassTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_rendererPassTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsRendererPassTraceStateOwned(context))
    {
        context.Dr0 = g_rendererPassTrace.originalDr0;
        context.Dr1 = g_rendererPassTrace.originalDr1;
        context.Dr2 = g_rendererPassTrace.originalDr2;
        context.Dr3 = g_rendererPassTrace.originalDr3;
        context.Dr6 = g_rendererPassTrace.originalDr6;
        context.Dr7 = g_rendererPassTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_rendererPassTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

LONG CALLBACK HandleRendererPassTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_rendererPassTrace.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_rendererPassTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.frameCoordinatorTarget))
    {
        CaptureRendererPassTraceEvent(g_rendererPassTrace.frameCoordinator, *context);
        InterlockedExchange(&g_rendererPassTrace.frameCoordinatorObserved, 1);
        context->Dr0 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.rendererCoordinatorTarget))
    {
        CaptureRendererPassTraceEvent(g_rendererPassTrace.rendererCoordinator, *context);
        InterlockedExchange(&g_rendererPassTrace.rendererCoordinatorObserved, 1);
        context->Dr1 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewTarget))
    {
        CaptureRendererPassTraceEvent(g_rendererPassTrace.renderViewEntry, *context);
        InterlockedExchange(&g_rendererPassTrace.renderViewEntryObserved, 1);
        context->Dr2 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        if (g_rendererPassTrace.renderViewEntry.stackReadable && g_rendererPassTrace.renderViewEntry.stackReturnAddress != 0)
        {
            g_rendererPassTrace.renderViewReturnTarget = reinterpret_cast<void*>(
                static_cast<DWORD_PTR>(g_rendererPassTrace.renderViewEntry.stackReturnAddress));
            context->Dr3 = reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewReturnTarget);
            context->Dr7 |= 0x40;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (g_rendererPassTrace.renderViewReturnTarget != nullptr &&
        context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewReturnTarget))
    {
        CaptureRendererPassTraceEvent(g_rendererPassTrace.renderViewReturn, *context);
        InterlockedExchange(&g_rendererPassTrace.renderViewReturnObserved, 1);
        context->Dr3 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmRendererPassTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"Map-gated renderer-pass trace skipped: game image is unavailable.");
        return false;
    }
    if (g_rendererPassTraceHandler == nullptr)
    {
        g_rendererPassTraceHandler = AddVectoredExceptionHandler(1, HandleRendererPassTrace);
        if (g_rendererPassTraceHandler == nullptr)
        {
            AppendLog(L"Map-gated renderer-pass trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    LARGE_INTEGER frequency = {};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        AppendLog(L"Map-gated renderer-pass trace skipped: QueryPerformanceFrequency failed (%lu).", GetLastError());
        return false;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Map-gated renderer-pass trace skipped: unable to open the device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-pass trace skipped: unable to suspend the device thread (%lu).", GetLastError());
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-pass trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_rendererPassTrace = {};
    g_rendererPassTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_rendererPassTrace.frameCoordinatorTarget = const_cast<std::byte*>(gameImage) + kFrameCoordinatorRva;
    g_rendererPassTrace.rendererCoordinatorTarget = const_cast<std::byte*>(gameImage) + kRendererCoordinatorRva;
    g_rendererPassTrace.renderViewTarget = const_cast<std::byte*>(gameImage) + kRenderViewRva;
    g_rendererPassTrace.originalDr0 = context.Dr0;
    g_rendererPassTrace.originalDr1 = context.Dr1;
    g_rendererPassTrace.originalDr2 = context.Dr2;
    g_rendererPassTrace.originalDr3 = context.Dr3;
    g_rendererPassTrace.originalDr6 = context.Dr6;
    g_rendererPassTrace.originalDr7 = context.Dr7;
    g_rendererPassTrace.performanceCounterFrequency = frequency.QuadPart;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.frameCoordinatorTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.rendererCoordinatorTarget);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewTarget);
    context.Dr3 = 0;
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x15;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-pass trace skipped: unable to arm device-thread breakpoints (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_rendererPassTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one map-gated renderer-pass trace on device thread %lu: frame coordinator 0x0044ABC0, renderer coordinator 0x00466D80, one per-view entry 0x004662C0, and its dynamically captured return boundary.", g_rendererPassTrace.threadId);
    return true;
}

void AppendRendererPassTraceEvent(const wchar_t* label, const RendererPassTraceEvent& event)
{
    AppendLog(
        L"Renderer-pass %s tick=%lu qpc=%lld eip=%08lX ecx=%08lX stackReturn=%08lX stackReadable=%d.",
        label,
        static_cast<unsigned long>(event.tick),
        static_cast<long long>(event.performanceCounter),
        static_cast<unsigned long>(event.instruction),
        static_cast<unsigned long>(event.ecx),
        static_cast<unsigned long>(event.stackReturnAddress),
        event.stackReadable);
}

void StartRendererViewSubpassTrace();
void StartRendererLayerTrace();

DWORD WINAPI RunRendererPassTrace(void*)
{
    constexpr DWORD kRendererPassTraceWindowMs = 5000;
    if (!ArmRendererPassTrace())
    {
        return 0;
    }

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kRendererPassTraceWindowMs)
    {
        if (InterlockedCompareExchange(&g_rendererPassTrace.frameCoordinatorObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererPassTrace.rendererCoordinatorObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererPassTrace.renderViewEntryObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererPassTrace.renderViewReturnObserved, 0, 0) != 0)
        {
            break;
        }
        Sleep(10);
    }

    RestoreRendererPassTrace();
    InterlockedExchange(&g_rendererPassTrace.state, 2);
    const LONGLONG viewDurationCounter =
        g_rendererPassTrace.renderViewReturn.performanceCounter >= g_rendererPassTrace.renderViewEntry.performanceCounter
            ? g_rendererPassTrace.renderViewReturn.performanceCounter - g_rendererPassTrace.renderViewEntry.performanceCounter
            : 0;
    const double viewDurationMilliseconds = g_rendererPassTrace.performanceCounterFrequency > 0
        ? 1000.0 * static_cast<double>(viewDurationCounter) / static_cast<double>(g_rendererPassTrace.performanceCounterFrequency)
        : 0.0;
    AppendLog(
        L"Map-gated renderer-pass trace summary: frameCoordinator=%ld rendererCoordinator=%ld renderViewEntry=%ld renderViewReturn=%ld returnTarget=%08lX viewDurationMs=%.3f registersRestored=%d.",
        InterlockedCompareExchange(&g_rendererPassTrace.frameCoordinatorObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererPassTrace.rendererCoordinatorObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererPassTrace.renderViewEntryObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererPassTrace.renderViewReturnObserved, 0, 0),
        static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_rendererPassTrace.renderViewReturnTarget)),
        viewDurationMilliseconds,
        g_rendererPassTrace.cleanupRestored);
    if (InterlockedCompareExchange(&g_rendererPassTrace.frameCoordinatorObserved, 0, 0) != 0)
    {
        AppendRendererPassTraceEvent(L"frame-coordinator entry", g_rendererPassTrace.frameCoordinator);
    }
    if (InterlockedCompareExchange(&g_rendererPassTrace.rendererCoordinatorObserved, 0, 0) != 0)
    {
        AppendRendererPassTraceEvent(L"renderer-coordinator entry", g_rendererPassTrace.rendererCoordinator);
    }
    if (InterlockedCompareExchange(&g_rendererPassTrace.renderViewEntryObserved, 0, 0) != 0)
    {
        AppendRendererPassTraceEvent(L"render-view entry", g_rendererPassTrace.renderViewEntry);
    }
    if (InterlockedCompareExchange(&g_rendererPassTrace.renderViewReturnObserved, 0, 0) != 0)
    {
        AppendRendererPassTraceEvent(L"render-view return", g_rendererPassTrace.renderViewReturn);
    }
    StartRendererViewSubpassTrace();
    return 0;
}

void StartRendererPassTrace()
{
    if (InterlockedCompareExchange(&g_rendererPassTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunRendererPassTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Map-gated renderer-pass trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one five-second renderer-pass trace after the existing D3D8 trace; it observes four code boundaries and writes no game/D3D state.");
}

void CaptureRendererViewSubpassEvent(RendererViewSubpassEvent& event, const CONTEXT& context, std::size_t vtableOffset)
{
    event.tick = GetTickCount();
    event.instruction = static_cast<DWORD>(context.Eip);
    event.ecx = static_cast<DWORD>(context.Ecx);
    LARGE_INTEGER counter = {};
    if (QueryPerformanceCounter(&counter))
    {
        event.performanceCounter = counter.QuadPart;
    }

    __try
    {
        event.vtable = static_cast<DWORD>(context.Edx);
        event.callTarget = *reinterpret_cast<const DWORD*>(context.Edx + vtableOffset);
        event.stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
        event.readable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        event.readable = FALSE;
    }
}

bool IsRendererViewSubpassTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryObjectSubmitTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryRendererStageTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 && context.Dr2 != reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.secondaryRendererStageTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 && context.Dr3 != reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.trailingRendererStageTarget))
    {
        return false;
    }
    return true;
}

void RestoreRendererViewSubpassTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_rendererViewSubpassTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsRendererViewSubpassTraceStateOwned(context))
    {
        context.Dr0 = g_rendererViewSubpassTrace.originalDr0;
        context.Dr1 = g_rendererViewSubpassTrace.originalDr1;
        context.Dr2 = g_rendererViewSubpassTrace.originalDr2;
        context.Dr3 = g_rendererViewSubpassTrace.originalDr3;
        context.Dr6 = g_rendererViewSubpassTrace.originalDr6;
        context.Dr7 = g_rendererViewSubpassTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_rendererViewSubpassTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

LONG CALLBACK HandleRendererViewSubpassTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_rendererViewSubpassTrace.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_rendererViewSubpassTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryObjectSubmitTarget))
    {
        CaptureRendererViewSubpassEvent(g_rendererViewSubpassTrace.primaryObjectSubmit, *context, 0x20);
        InterlockedExchange(&g_rendererViewSubpassTrace.primaryObjectSubmitObserved, 1);
        context->Dr0 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryRendererStageTarget))
    {
        CaptureRendererViewSubpassEvent(g_rendererViewSubpassTrace.primaryRendererStage, *context, 0x1C);
        InterlockedExchange(&g_rendererViewSubpassTrace.primaryRendererStageObserved, 1);
        context->Dr1 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.secondaryRendererStageTarget))
    {
        CaptureRendererViewSubpassEvent(g_rendererViewSubpassTrace.secondaryRendererStage, *context, 0x20);
        InterlockedExchange(&g_rendererViewSubpassTrace.secondaryRendererStageObserved, 1);
        context->Dr2 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.trailingRendererStageTarget))
    {
        CaptureRendererViewSubpassEvent(g_rendererViewSubpassTrace.trailingRendererStage, *context, 0x18);
        InterlockedExchange(&g_rendererViewSubpassTrace.trailingRendererStageObserved, 1);
        context->Dr3 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmRendererViewSubpassTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"Map-gated renderer-view subpass trace skipped: game image is unavailable.");
        return false;
    }
    if (g_rendererViewSubpassTraceHandler == nullptr)
    {
        g_rendererViewSubpassTraceHandler = AddVectoredExceptionHandler(1, HandleRendererViewSubpassTrace);
        if (g_rendererViewSubpassTraceHandler == nullptr)
        {
            AppendLog(L"Map-gated renderer-view subpass trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    LARGE_INTEGER frequency = {};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        AppendLog(L"Map-gated renderer-view subpass trace skipped: QueryPerformanceFrequency failed (%lu).", GetLastError());
        return false;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Map-gated renderer-view subpass trace skipped: unable to open the device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-view subpass trace skipped: unable to suspend the device thread (%lu).", GetLastError());
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-view subpass trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_rendererViewSubpassTrace = {};
    g_rendererViewSubpassTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_rendererViewSubpassTrace.primaryObjectSubmitTarget = const_cast<std::byte*>(gameImage) + kRenderViewPrimaryObjectSubmitRva;
    g_rendererViewSubpassTrace.primaryRendererStageTarget = const_cast<std::byte*>(gameImage) + kRenderViewPrimaryRendererStageRva;
    g_rendererViewSubpassTrace.secondaryRendererStageTarget = const_cast<std::byte*>(gameImage) + kRenderViewSecondaryRendererStageRva;
    g_rendererViewSubpassTrace.trailingRendererStageTarget = const_cast<std::byte*>(gameImage) + kRenderViewTrailingRendererStageRva;
    g_rendererViewSubpassTrace.originalDr0 = context.Dr0;
    g_rendererViewSubpassTrace.originalDr1 = context.Dr1;
    g_rendererViewSubpassTrace.originalDr2 = context.Dr2;
    g_rendererViewSubpassTrace.originalDr3 = context.Dr3;
    g_rendererViewSubpassTrace.originalDr6 = context.Dr6;
    g_rendererViewSubpassTrace.originalDr7 = context.Dr7;
    g_rendererViewSubpassTrace.performanceCounterFrequency = frequency.QuadPart;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryObjectSubmitTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.primaryRendererStageTarget);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.secondaryRendererStageTarget);
    context.Dr3 = reinterpret_cast<DWORD_PTR>(g_rendererViewSubpassTrace.trailingRendererStageTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x55;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-view subpass trace skipped: unable to arm device-thread breakpoints (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_rendererViewSubpassTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one map-gated renderer-view subpass trace on device thread %lu: render-view reference setup 0x00466AD0, skinning bucket mode 0 at 0x00466B05, render-context bucket mode 1 at 0x00466B89, and skinning bucket mode 2 at 0x00466C5A.", g_rendererViewSubpassTrace.threadId);
    return true;
}

void AppendRendererViewSubpassEvent(const wchar_t* label, const RendererViewSubpassEvent& event)
{
    AppendLog(
        L"Renderer-view subpass %s tick=%lu qpc=%lld eip=%08lX ecx=%08lX vtable=%08lX callTarget=%08lX stackReturn=%08lX readable=%d.",
        label,
        static_cast<unsigned long>(event.tick),
        static_cast<long long>(event.performanceCounter),
        static_cast<unsigned long>(event.instruction),
        static_cast<unsigned long>(event.ecx),
        static_cast<unsigned long>(event.vtable),
        static_cast<unsigned long>(event.callTarget),
        static_cast<unsigned long>(event.stackReturnAddress),
        event.readable);
}

DWORD WINAPI RunRendererViewSubpassTrace(void*)
{
    constexpr DWORD kRendererViewSubpassTraceWindowMs = 5000;
    if (!ArmRendererViewSubpassTrace())
    {
        return 0;
    }

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kRendererViewSubpassTraceWindowMs)
    {
        if (InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryObjectSubmitObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryRendererStageObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererViewSubpassTrace.secondaryRendererStageObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererViewSubpassTrace.trailingRendererStageObserved, 0, 0) != 0)
        {
            break;
        }
        Sleep(10);
    }

    RestoreRendererViewSubpassTrace();
    InterlockedExchange(&g_rendererViewSubpassTrace.state, 2);
    AppendLog(
        L"Map-gated renderer-view subpass trace summary: primaryObject=%ld primaryRenderer=%ld secondaryRenderer=%ld trailingRenderer=%ld registersRestored=%d.",
        InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryObjectSubmitObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryRendererStageObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererViewSubpassTrace.secondaryRendererStageObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererViewSubpassTrace.trailingRendererStageObserved, 0, 0),
        g_rendererViewSubpassTrace.cleanupRestored);
    if (InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryObjectSubmitObserved, 0, 0) != 0)
    {
        AppendRendererViewSubpassEvent(L"render-view reference setup", g_rendererViewSubpassTrace.primaryObjectSubmit);
    }
    if (InterlockedCompareExchange(&g_rendererViewSubpassTrace.primaryRendererStageObserved, 0, 0) != 0)
    {
        AppendRendererViewSubpassEvent(L"skinning bucket (mode 0)", g_rendererViewSubpassTrace.primaryRendererStage);
    }
    if (InterlockedCompareExchange(&g_rendererViewSubpassTrace.secondaryRendererStageObserved, 0, 0) != 0)
    {
        AppendRendererViewSubpassEvent(L"render-context bucket (mode 1)", g_rendererViewSubpassTrace.secondaryRendererStage);
    }
    if (InterlockedCompareExchange(&g_rendererViewSubpassTrace.trailingRendererStageObserved, 0, 0) != 0)
    {
        AppendRendererViewSubpassEvent(L"skinning bucket (mode 2)", g_rendererViewSubpassTrace.trailingRendererStage);
    }
    StartRendererLayerTrace();
    return 0;
}

void StartRendererViewSubpassTrace()
{
    if (InterlockedCompareExchange(&g_rendererViewSubpassTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunRendererViewSubpassTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Map-gated renderer-view subpass trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one five-second renderer-view subpass trace after the pass-boundary trace; it reads four existing virtual-call targets and writes no game/D3D state.");
}

void CaptureRendererLayerTraceEvent(RendererLayerTraceEvent& event, const CONTEXT& context)
{
    event.tick = GetTickCount();
    event.instruction = static_cast<DWORD>(context.Eip);
    event.ecx = static_cast<DWORD>(context.Ecx);
    LARGE_INTEGER counter = {};
    if (QueryPerformanceCounter(&counter))
    {
        event.performanceCounter = counter.QuadPart;
    }
    __try
    {
        event.stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
        event.stackReadable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        event.stackReadable = FALSE;
    }
}

bool IsRendererLayerTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewLoopExitTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 && context.Dr2 != reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.postViewCallbackListTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 && context.Dr3 != reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.textOverlayQueueTarget))
    {
        return false;
    }
    return true;
}

void RestoreRendererLayerTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_rendererLayerTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsRendererLayerTraceStateOwned(context))
    {
        context.Dr0 = g_rendererLayerTrace.originalDr0;
        context.Dr1 = g_rendererLayerTrace.originalDr1;
        context.Dr2 = g_rendererLayerTrace.originalDr2;
        context.Dr3 = g_rendererLayerTrace.originalDr3;
        context.Dr6 = g_rendererLayerTrace.originalDr6;
        context.Dr7 = g_rendererLayerTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_rendererLayerTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

LONG CALLBACK HandleRendererLayerTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_rendererLayerTrace.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_rendererLayerTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewTarget))
    {
        CaptureRendererLayerTraceEvent(g_rendererLayerTrace.renderView, *context);
        InterlockedExchange(&g_rendererLayerTrace.renderViewObserved, 1);
        context->Dr0 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewLoopExitTarget))
    {
        CaptureRendererLayerTraceEvent(g_rendererLayerTrace.renderViewLoopExit, *context);
        InterlockedExchange(&g_rendererLayerTrace.renderViewLoopExitObserved, 1);
        context->Dr1 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.postViewCallbackListTarget))
    {
        CaptureRendererLayerTraceEvent(g_rendererLayerTrace.postViewCallbackList, *context);
        InterlockedExchange(&g_rendererLayerTrace.postViewCallbackListObserved, 1);
        context->Dr2 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.textOverlayQueueTarget))
    {
        CaptureRendererLayerTraceEvent(g_rendererLayerTrace.textOverlayQueue, *context);
        InterlockedExchange(&g_rendererLayerTrace.textOverlayQueueObserved, 1);
        context->Dr3 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmRendererLayerTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"Map-gated renderer-layer trace skipped: game image is unavailable.");
        return false;
    }
    if (g_rendererLayerTraceHandler == nullptr)
    {
        g_rendererLayerTraceHandler = AddVectoredExceptionHandler(1, HandleRendererLayerTrace);
        if (g_rendererLayerTraceHandler == nullptr)
        {
            AppendLog(L"Map-gated renderer-layer trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Map-gated renderer-layer trace skipped: unable to open the device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-layer trace skipped: unable to suspend the device thread (%lu).", GetLastError());
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-layer trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_rendererLayerTrace = {};
    g_rendererLayerTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_rendererLayerTrace.renderViewTarget = const_cast<std::byte*>(gameImage) + kRenderViewRva;
    g_rendererLayerTrace.renderViewLoopExitTarget = const_cast<std::byte*>(gameImage) + kRenderViewLoopExitRva;
    g_rendererLayerTrace.postViewCallbackListTarget = const_cast<std::byte*>(gameImage) + kPostViewCallbackListRva;
    g_rendererLayerTrace.textOverlayQueueTarget = const_cast<std::byte*>(gameImage) + kTextOverlayQueueRva;
    g_rendererLayerTrace.originalDr0 = context.Dr0;
    g_rendererLayerTrace.originalDr1 = context.Dr1;
    g_rendererLayerTrace.originalDr2 = context.Dr2;
    g_rendererLayerTrace.originalDr3 = context.Dr3;
    g_rendererLayerTrace.originalDr6 = context.Dr6;
    g_rendererLayerTrace.originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.renderViewLoopExitTarget);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.postViewCallbackListTarget);
    context.Dr3 = reinterpret_cast<DWORD_PTR>(g_rendererLayerTrace.textOverlayQueueTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x55;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated renderer-layer trace skipped: unable to arm device-thread breakpoints (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_rendererLayerTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one map-gated renderer-layer trace on device thread %lu: per-view entry 0x004662C0, view-loop exit 0x00466FD9, post-view callback list 0x0046E900, and text-overlay queue 0x00461400.", g_rendererLayerTrace.threadId);
    return true;
}

void AppendRendererLayerTraceEvent(const wchar_t* label, const RendererLayerTraceEvent& event)
{
    AppendLog(
        L"Renderer-layer %s tick=%lu qpc=%lld eip=%08lX ecx=%08lX stackReturn=%08lX stackReadable=%d.",
        label,
        static_cast<unsigned long>(event.tick),
        static_cast<long long>(event.performanceCounter),
        static_cast<unsigned long>(event.instruction),
        static_cast<unsigned long>(event.ecx),
        static_cast<unsigned long>(event.stackReturnAddress),
        event.stackReadable);
}

void StartFrameModelTrace();
void StartSurfaceDescriptionTrace();

DWORD WINAPI RunRendererLayerTrace(void*)
{
    constexpr DWORD kRendererLayerTraceWindowMs = 5000;
    if (!ArmRendererLayerTrace())
    {
        return 0;
    }

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kRendererLayerTraceWindowMs)
    {
        if (InterlockedCompareExchange(&g_rendererLayerTrace.renderViewObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererLayerTrace.renderViewLoopExitObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererLayerTrace.postViewCallbackListObserved, 0, 0) != 0 &&
            InterlockedCompareExchange(&g_rendererLayerTrace.textOverlayQueueObserved, 0, 0) != 0)
        {
            break;
        }
        Sleep(10);
    }

    RestoreRendererLayerTrace();
    InterlockedExchange(&g_rendererLayerTrace.state, 2);
    AppendLog(
        L"Map-gated renderer-layer trace summary: renderView=%ld loopExit=%ld postViewCallbackList=%ld textOverlayQueue=%ld registersRestored=%d.",
        InterlockedCompareExchange(&g_rendererLayerTrace.renderViewObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererLayerTrace.renderViewLoopExitObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererLayerTrace.postViewCallbackListObserved, 0, 0),
        InterlockedCompareExchange(&g_rendererLayerTrace.textOverlayQueueObserved, 0, 0),
        g_rendererLayerTrace.cleanupRestored);
    if (InterlockedCompareExchange(&g_rendererLayerTrace.renderViewObserved, 0, 0) != 0)
    {
        AppendRendererLayerTraceEvent(L"per-view scene entry", g_rendererLayerTrace.renderView);
    }
    if (InterlockedCompareExchange(&g_rendererLayerTrace.renderViewLoopExitObserved, 0, 0) != 0)
    {
        AppendRendererLayerTraceEvent(L"per-view loop exit", g_rendererLayerTrace.renderViewLoopExit);
    }
    if (InterlockedCompareExchange(&g_rendererLayerTrace.postViewCallbackListObserved, 0, 0) != 0)
    {
        AppendRendererLayerTraceEvent(L"post-view callback list", g_rendererLayerTrace.postViewCallbackList);
    }
    if (InterlockedCompareExchange(&g_rendererLayerTrace.textOverlayQueueObserved, 0, 0) != 0)
    {
        AppendRendererLayerTraceEvent(L"text-overlay queue", g_rendererLayerTrace.textOverlayQueue);
    }
    StartFrameModelTrace();
    return 0;
}

void StartRendererLayerTrace()
{
    if (InterlockedCompareExchange(&g_rendererLayerTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunRendererLayerTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Map-gated renderer-layer trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one five-second renderer-layer trace after the subpass trace; it observes the scene-to-overlay handoff and writes no game/D3D state.");
}

bool IsFrameModelTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_frameModelTrace.frameCoordinatorTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_frameModelTrace.rendererCoordinatorTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 && context.Dr2 != reinterpret_cast<DWORD_PTR>(g_frameModelTrace.renderViewTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 && context.Dr3 != reinterpret_cast<DWORD_PTR>(g_frameModelTrace.presentTarget))
    {
        return false;
    }
    return true;
}

void RestoreFrameModelTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_frameModelTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsFrameModelTraceStateOwned(context))
    {
        context.Dr0 = g_frameModelTrace.originalDr0;
        context.Dr1 = g_frameModelTrace.originalDr1;
        context.Dr2 = g_frameModelTrace.originalDr2;
        context.Dr3 = g_frameModelTrace.originalDr3;
        context.Dr6 = g_frameModelTrace.originalDr6;
        context.Dr7 = g_frameModelTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_frameModelTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

bool RecordFrameModelTraceEvent(FrameModelEventKind kind)
{
    volatile LONG* storedForKind = nullptr;
    switch (kind)
    {
    case FrameModelEventKind::FrameCoordinator:
        InterlockedIncrement(&g_frameModelTrace.frameCoordinatorEvents);
        storedForKind = &g_frameModelTrace.frameCoordinatorStored;
        break;
    case FrameModelEventKind::RendererCoordinator:
        InterlockedIncrement(&g_frameModelTrace.rendererCoordinatorEvents);
        storedForKind = &g_frameModelTrace.rendererCoordinatorStored;
        break;
    case FrameModelEventKind::RenderView:
        InterlockedIncrement(&g_frameModelTrace.renderViewEvents);
        storedForKind = &g_frameModelTrace.renderViewStored;
        break;
    case FrameModelEventKind::Present:
        InterlockedIncrement(&g_frameModelTrace.presentEvents);
        storedForKind = &g_frameModelTrace.presentStored;
        break;
    }

    if (storedForKind == nullptr ||
        InterlockedIncrement(storedForKind) > kFrameModelTraceEventsPerKindMaximum)
    {
        InterlockedIncrement(&g_frameModelTrace.suppressedEvents);
        return true;
    }

    const LONG eventIndex = InterlockedIncrement(&g_frameModelTrace.eventsCaptured) - 1;
    if (eventIndex < 0 || eventIndex >= kFrameModelTraceEventMaximum)
    {
        InterlockedIncrement(&g_frameModelTrace.droppedEvents);
        InterlockedExchange(&g_frameModelTrace.capacityReached, 1);
        return false;
    }

    LARGE_INTEGER counter = {};
    if (QueryPerformanceCounter(&counter))
    {
        g_frameModelTrace.events[eventIndex].performanceCounter = counter.QuadPart;
    }
    g_frameModelTrace.events[eventIndex].kind = kind;
    return true;
}

void DisarmFrameModelTrace(CONTEXT& context)
{
    context.Dr0 = 0;
    context.Dr1 = 0;
    context.Dr2 = 0;
    context.Dr3 = 0;
    context.Dr6 = 0;
    context.Dr7 &= ~static_cast<DWORD_PTR>(0x55);
}

void DisarmFrameModelTraceSlot(CONTEXT& context, FrameModelEventKind kind)
{
    switch (kind)
    {
    case FrameModelEventKind::FrameCoordinator:
        context.Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        break;
    case FrameModelEventKind::RendererCoordinator:
        context.Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        break;
    case FrameModelEventKind::RenderView:
        context.Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        break;
    case FrameModelEventKind::Present:
        context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        break;
    }
    context.Dr6 = 0;
}

bool RearmFrameModelTraceSlots()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_frameModelTrace.threadId);
    if (thread == nullptr)
    {
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool rearmed = GetThreadContext(thread, &context) != FALSE &&
        IsFrameModelTraceStateOwned(context);
    if (rearmed)
    {
        context.Dr6 = 0;
        context.Dr7 |= 0x55;
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
    return rearmed;
}

LONG CALLBACK HandleFrameModelTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_frameModelTrace.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_frameModelTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    FrameModelEventKind kind = FrameModelEventKind::FrameCoordinator;
    if (context->Eip == reinterpret_cast<DWORD_PTR>(g_frameModelTrace.frameCoordinatorTarget))
    {
        kind = FrameModelEventKind::FrameCoordinator;
    }
    else if (context->Eip == reinterpret_cast<DWORD_PTR>(g_frameModelTrace.rendererCoordinatorTarget))
    {
        kind = FrameModelEventKind::RendererCoordinator;
    }
    else if (context->Eip == reinterpret_cast<DWORD_PTR>(g_frameModelTrace.renderViewTarget))
    {
        kind = FrameModelEventKind::RenderView;
    }
    else if (context->Eip == reinterpret_cast<DWORD_PTR>(g_frameModelTrace.presentTarget))
    {
        kind = FrameModelEventKind::Present;
    }
    else
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (!RecordFrameModelTraceEvent(kind))
    {
        DisarmFrameModelTrace(*context);
    }
    else
    {
        // Each target is one-shot per sampling period. Leaving a hot inner
        // renderer boundary armed starves the frame and Present targets.
        DisarmFrameModelTraceSlot(*context, kind);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

bool ArmFrameModelTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0 || g_createDeviceBreakpoint.presentTarget == nullptr)
    {
        AppendLog(L"Map-gated frame-model trace skipped because the device-thread Present target is unavailable.");
        return false;
    }
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"Map-gated frame-model trace skipped: game image is unavailable.");
        return false;
    }
    if (g_frameModelTraceHandler == nullptr)
    {
        g_frameModelTraceHandler = AddVectoredExceptionHandler(1, HandleFrameModelTrace);
        if (g_frameModelTraceHandler == nullptr)
        {
            AppendLog(L"Map-gated frame-model trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    LARGE_INTEGER frequency = {};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        AppendLog(L"Map-gated frame-model trace skipped: QueryPerformanceFrequency failed (%lu).", GetLastError());
        return false;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Map-gated frame-model trace skipped: unable to open the device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        AppendLog(L"Map-gated frame-model trace skipped: unable to suspend the device thread (%lu).", GetLastError());
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated frame-model trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_frameModelTrace = {};
    g_frameModelTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_frameModelTrace.frameCoordinatorTarget = const_cast<std::byte*>(gameImage) + kFrameCoordinatorRva;
    g_frameModelTrace.rendererCoordinatorTarget = const_cast<std::byte*>(gameImage) + kRendererCoordinatorRva;
    g_frameModelTrace.renderViewTarget = const_cast<std::byte*>(gameImage) + kRenderViewRva;
    g_frameModelTrace.presentTarget = g_createDeviceBreakpoint.presentTarget;
    g_frameModelTrace.originalDr0 = context.Dr0;
    g_frameModelTrace.originalDr1 = context.Dr1;
    g_frameModelTrace.originalDr2 = context.Dr2;
    g_frameModelTrace.originalDr3 = context.Dr3;
    g_frameModelTrace.originalDr6 = context.Dr6;
    g_frameModelTrace.originalDr7 = context.Dr7;
    g_frameModelTrace.performanceCounterFrequency = frequency.QuadPart;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_frameModelTrace.frameCoordinatorTarget);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_frameModelTrace.rendererCoordinatorTarget);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_frameModelTrace.renderViewTarget);
    context.Dr3 = reinterpret_cast<DWORD_PTR>(g_frameModelTrace.presentTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x55;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Map-gated frame-model trace skipped: unable to arm device-thread breakpoints (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_frameModelTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one map-gated 1.5-second frame-model trace on device thread %lu: frame coordinator, renderer coordinator, per-view renderer, and original D3D8 Present. It records execution timing only and writes no game/D3D state.", g_frameModelTrace.threadId);
    return true;
}

void AppendFrameModelTraceSummary()
{
    const LONG eventCount = InterlockedCompareExchange(&g_frameModelTrace.eventsCaptured, 0, 0);
    const LONG storedEventCount = eventCount < kFrameModelTraceEventMaximum ? eventCount : kFrameModelTraceEventMaximum;
    LONG presentIntervals = 0;
    LONG totalFramesPerPresent = 0;
    LONG totalRenderersPerPresent = 0;
    LONG totalViewsPerPresent = 0;
    double presentIntervalSumMs = 0.0;
    double presentIntervalMinimumMs = 0.0;
    double presentIntervalMaximumMs = 0.0;
    LONGLONG previousPresentCounter = 0;
    LONG framesSincePresent = 0;
    LONG renderersSincePresent = 0;
    LONG viewsSincePresent = 0;

    for (LONG index = 0; index < storedEventCount; ++index)
    {
        const FrameModelTraceEvent& event = g_frameModelTrace.events[index];
        if (event.kind == FrameModelEventKind::FrameCoordinator)
        {
            ++framesSincePresent;
            continue;
        }
        if (event.kind == FrameModelEventKind::RendererCoordinator)
        {
            ++renderersSincePresent;
            continue;
        }
        if (event.kind == FrameModelEventKind::RenderView)
        {
            ++viewsSincePresent;
            continue;
        }

        if (previousPresentCounter != 0 && event.performanceCounter >= previousPresentCounter &&
            g_frameModelTrace.performanceCounterFrequency > 0)
        {
            const double intervalMs = 1000.0 * static_cast<double>(event.performanceCounter - previousPresentCounter) /
                static_cast<double>(g_frameModelTrace.performanceCounterFrequency);
            ++presentIntervals;
            presentIntervalSumMs += intervalMs;
            if (presentIntervals == 1 || intervalMs < presentIntervalMinimumMs)
            {
                presentIntervalMinimumMs = intervalMs;
            }
            if (intervalMs > presentIntervalMaximumMs)
            {
                presentIntervalMaximumMs = intervalMs;
            }
            totalFramesPerPresent += framesSincePresent;
            totalRenderersPerPresent += renderersSincePresent;
            totalViewsPerPresent += viewsSincePresent;
        }
        previousPresentCounter = event.performanceCounter;
        framesSincePresent = 0;
        renderersSincePresent = 0;
        viewsSincePresent = 0;
    }

    const double meanPresentIntervalMs = presentIntervals > 0 ? presentIntervalSumMs / static_cast<double>(presentIntervals) : 0.0;
    const double meanFramesPerPresent = presentIntervals > 0 ? static_cast<double>(totalFramesPerPresent) / static_cast<double>(presentIntervals) : 0.0;
    const double meanRenderersPerPresent = presentIntervals > 0 ? static_cast<double>(totalRenderersPerPresent) / static_cast<double>(presentIntervals) : 0.0;
    const double meanViewsPerPresent = presentIntervals > 0 ? static_cast<double>(totalViewsPerPresent) / static_cast<double>(presentIntervals) : 0.0;
    AppendLog(
        L"Map-gated frame-model trace summary: storedEvents=%ld suppressedEvents=%ld droppedEvents=%ld frameCoordinator=%ld rendererCoordinator=%ld renderView=%ld present=%ld presentIntervals=%ld meanPresentMs=%.3f minPresentMs=%.3f maxPresentMs=%.3f meanBetweenPresents(frame=%.2f renderer=%.2f view=%.2f) capacityReached=%d registersRestored=%d.",
        storedEventCount,
        InterlockedCompareExchange(&g_frameModelTrace.suppressedEvents, 0, 0),
        InterlockedCompareExchange(&g_frameModelTrace.droppedEvents, 0, 0),
        InterlockedCompareExchange(&g_frameModelTrace.frameCoordinatorEvents, 0, 0),
        InterlockedCompareExchange(&g_frameModelTrace.rendererCoordinatorEvents, 0, 0),
        InterlockedCompareExchange(&g_frameModelTrace.renderViewEvents, 0, 0),
        InterlockedCompareExchange(&g_frameModelTrace.presentEvents, 0, 0),
        presentIntervals,
        meanPresentIntervalMs,
        presentIntervalMinimumMs,
        presentIntervalMaximumMs,
        meanFramesPerPresent,
        meanRenderersPerPresent,
        meanViewsPerPresent,
        InterlockedCompareExchange(&g_frameModelTrace.capacityReached, 0, 0),
        g_frameModelTrace.cleanupRestored);
}

DWORD WINAPI RunFrameModelTrace(void*)
{
    constexpr DWORD kFrameModelTraceWindowMs = 1500;
    constexpr DWORD kFrameModelTraceSamplePeriodMs = 16;
    if (!ArmFrameModelTrace())
    {
        return 0;
    }

    const DWORD startedAt = GetTickCount();
    DWORD lastRearmAt = startedAt;
    while (GetTickCount() - startedAt < kFrameModelTraceWindowMs &&
           InterlockedCompareExchange(&g_frameModelTrace.capacityReached, 0, 0) == 0)
    {
        Sleep(2);
        const DWORD now = GetTickCount();
        if (now - lastRearmAt >= kFrameModelTraceSamplePeriodMs)
        {
            RearmFrameModelTraceSlots();
            lastRearmAt = now;
        }
    }

    RestoreFrameModelTrace();
    InterlockedExchange(&g_frameModelTrace.state, 2);
    AppendFrameModelTraceSummary();
    StartSurfaceDescriptionTrace();
    return 0;
}

void StartFrameModelTrace()
{
    if (InterlockedCompareExchange(&g_frameModelTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunFrameModelTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Map-gated frame-model trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one 1.5-second frame-model trace after the scene-to-overlay trace; it records repeated execution boundaries to test frame/pass cadence without touching game or D3D state.");
}

bool IsSurfaceDescriptionTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xFE) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 &&
        context.Dr0 != reinterpret_cast<DWORD_PTR>(g_surfaceDescriptionTrace.target) &&
        context.Dr0 != g_surfaceDescriptionTrace.returnAddress)
    {
        return false;
    }
    return true;
}

void RestoreSurfaceDescriptionTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_surfaceDescriptionTrace.threadId);
    if (thread == nullptr)
    {
        return;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        return;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) && IsSurfaceDescriptionTraceStateOwned(context))
    {
        context.Dr0 = g_surfaceDescriptionTrace.originalDr0;
        context.Dr6 = g_surfaceDescriptionTrace.originalDr6;
        context.Dr7 = g_surfaceDescriptionTrace.originalDr7;
        if (SetThreadContext(thread, &context))
        {
            g_surfaceDescriptionTrace.cleanupRestored = TRUE;
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

LONG CALLBACK HandleSurfaceDescriptionTrace(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        GetCurrentThreadId() != g_surfaceDescriptionTrace.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* context = exceptionPointers->ContextRecord;
    const LONG state = InterlockedCompareExchange(&g_surfaceDescriptionTrace.state, 0, 0);
    if (state == 1 && context->Eip == reinterpret_cast<DWORD_PTR>(g_surfaceDescriptionTrace.target))
    {
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        __try
        {
            const DWORD surface = stack[1];
            const DWORD description = stack[2];
            if (surface == g_surfaceDescriptionTrace.surface && description != 0)
            {
                g_surfaceDescriptionTrace.returnAddress = stack[0];
                g_surfaceDescriptionTrace.descriptionAddress = description;
                context->Dr0 = g_surfaceDescriptionTrace.returnAddress;
                InterlockedExchange(&g_surfaceDescriptionTrace.state, 2);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            context->Dr0 = 0;
            context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
            InterlockedExchange(&g_surfaceDescriptionTrace.state, 3);
        }
        context->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (state == 2 && context->Eip == g_surfaceDescriptionTrace.returnAddress)
    {
        __try
        {
            g_surfaceDescriptionTrace.description =
                *reinterpret_cast<const D3DSurfaceDescription*>(g_surfaceDescriptionTrace.descriptionAddress);
            g_surfaceDescriptionTrace.descriptionReadable = TRUE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_surfaceDescriptionTrace.descriptionReadable = FALSE;
        }
        context->Dr0 = 0;
        context->Dr6 = 0;
        context->Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        InterlockedExchange(&g_surfaceDescriptionTrace.state, 3);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmSurfaceDescriptionTrace()
{
    if (g_createDeviceBreakpoint.threadId == 0 ||
        g_combinedFrameTrace.setRenderTargetColor == 0 ||
        g_combinedFrameTrace.setRenderTargetColorGetDescTarget == 0)
    {
        AppendLog(L"Color-surface descriptor trace skipped because the map-gated color surface is unavailable.");
        return false;
    }
    if (g_surfaceDescriptionTraceHandler == nullptr)
    {
        g_surfaceDescriptionTraceHandler = AddVectoredExceptionHandler(1, HandleSurfaceDescriptionTrace);
        if (g_surfaceDescriptionTraceHandler == nullptr)
        {
            AppendLog(L"Color-surface descriptor trace skipped: AddVectoredExceptionHandler failed (%lu).", GetLastError());
            return false;
        }
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
    if (thread == nullptr)
    {
        AppendLog(L"Color-surface descriptor trace skipped: unable to open device thread (%lu).", GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        CloseHandle(thread);
        AppendLog(L"Color-surface descriptor trace skipped: unable to suspend device thread (%lu).", GetLastError());
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    const bool contextAvailable = GetThreadContext(thread, &context) != FALSE;
    if (!contextAvailable || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Color-surface descriptor trace skipped to preserve device-thread debug-register state.");
        return false;
    }

    g_surfaceDescriptionTrace = {};
    g_surfaceDescriptionTrace.threadId = g_createDeviceBreakpoint.threadId;
    g_surfaceDescriptionTrace.target = reinterpret_cast<void*>(static_cast<DWORD_PTR>(g_combinedFrameTrace.setRenderTargetColorGetDescTarget));
    g_surfaceDescriptionTrace.surface = g_combinedFrameTrace.setRenderTargetColor;
    g_surfaceDescriptionTrace.originalDr0 = context.Dr0;
    g_surfaceDescriptionTrace.originalDr6 = context.Dr6;
    g_surfaceDescriptionTrace.originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_surfaceDescriptionTrace.target);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFFFF00FF)) | 0x1;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        AppendLog(L"Color-surface descriptor trace skipped: unable to arm GetDesc breakpoint (%lu).", GetLastError());
        return false;
    }
    InterlockedExchange(&g_surfaceDescriptionTrace.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one five-second read-only color-surface descriptor trace on device thread %lu: it waits for the game to call IDirect3DSurface8::GetDesc on surface=%08lX.", g_surfaceDescriptionTrace.threadId, static_cast<unsigned long>(g_surfaceDescriptionTrace.surface));
    return true;
}

DWORD WINAPI RunSurfaceDescriptionTrace(void*)
{
    constexpr DWORD kSurfaceDescriptionTraceWindowMs = 5000;
    if (!ArmSurfaceDescriptionTrace())
    {
        return 0;
    }

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kSurfaceDescriptionTraceWindowMs &&
           InterlockedCompareExchange(&g_surfaceDescriptionTrace.state, 0, 0) < 3)
    {
        Sleep(10);
    }
    RestoreSurfaceDescriptionTrace();
    InterlockedExchange(&g_surfaceDescriptionTrace.state, 4);
    if (g_surfaceDescriptionTrace.descriptionReadable)
    {
        const D3DSurfaceDescription& description = g_surfaceDescriptionTrace.description;
        AppendLog(L"Color-surface descriptor trace result: surface=%08lX format=%u type=%u usage=0x%08lX pool=%u bytes=%u multisample=%u size=%ux%u registersRestored=%d.",
            static_cast<unsigned long>(g_surfaceDescriptionTrace.surface),
            description.format,
            description.type,
            static_cast<unsigned long>(description.usage),
            description.pool,
            description.size,
            description.multiSampleType,
            description.width,
            description.height,
            g_surfaceDescriptionTrace.cleanupRestored);
    }
    else
    {
        AppendLog(L"Color-surface descriptor trace did not observe a game GetDesc call on surface=%08lX within five seconds; registersRestored=%d.",
            static_cast<unsigned long>(g_surfaceDescriptionTrace.surface),
            g_surfaceDescriptionTrace.cleanupRestored);
    }
    return 0;
}

void StartSurfaceDescriptionTrace()
{
    if (InterlockedCompareExchange(&g_surfaceDescriptionTraceStarted, 1, 0) != 0)
    {
        return;
    }
    HANDLE worker = CreateThread(nullptr, 0, RunSurfaceDescriptionTrace, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Color-surface descriptor trace could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Started one five-second color-surface descriptor trace after the frame-model trace; it invokes no D3D8 or game method.");
}
