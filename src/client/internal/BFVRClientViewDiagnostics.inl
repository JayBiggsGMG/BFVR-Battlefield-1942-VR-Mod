struct RenderViewTransformProbeMatrix
{
    DWORD address = 0;
    float values[16] = {};
    BOOL readable = FALSE;
};

struct RenderViewTransformProbeRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* setterDispatchTarget = nullptr;
    void* viewMatrixResultTarget = nullptr;
    void* projectionMatrixResultTarget = nullptr;
    void* transactionReturnTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    DWORD renderView = 0;
    DWORD renderViewVtable = 0;
    DWORD setterVirtualTarget = 0;
    DWORD viewGetterVirtualTarget = 0;
    DWORD projectionGetterVirtualTarget = 0;
    RenderViewTransformProbeMatrix inputTransform = {};
    RenderViewTransformProbeMatrix derivedView = {};
    RenderViewTransformProbeMatrix derivedProjection = {};
    RendererTransformCacheSample transactionAfterProjection = {};
    BOOL viewMatchesTransaction = FALSE;
    BOOL projectionMatchesTransaction = FALSE;
    DWORD failureInstruction = 0;
    BOOL structuredException = FALSE;
    BOOL cleanupRestored = FALSE;
};

RenderViewTransformProbeRecord g_renderViewTransformProbe = {};
PVOID g_renderViewTransformProbeHandler = nullptr;

void CaptureRenderViewTransformProbeMatrix(RenderViewTransformProbeMatrix& matrix, DWORD address)
{
    matrix.address = address;
    if (address == 0)
    {
        return;
    }

    __try
    {
        std::memcpy(matrix.values, reinterpret_cast<const void*>(static_cast<DWORD_PTR>(address)), sizeof(matrix.values));
        matrix.readable = TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        matrix.readable = FALSE;
    }
}

void RestoreRenderViewTransformProbeRegisters(CONTEXT& context)
{
    context.Dr0 = g_renderViewTransformProbe.originalDr0;
    context.Dr1 = g_renderViewTransformProbe.originalDr1;
    context.Dr2 = g_renderViewTransformProbe.originalDr2;
    context.Dr3 = g_renderViewTransformProbe.originalDr3;
    context.Dr6 = g_renderViewTransformProbe.originalDr6;
    context.Dr7 = g_renderViewTransformProbe.originalDr7;
    g_renderViewTransformProbe.cleanupRestored = TRUE;
}

bool IsRenderViewTransformProbeStateOwned(const CONTEXT& context)
{
    if (InterlockedCompareExchange(&g_renderViewTransformProbe.state, 0, 0) != 1 ||
        (context.Dr7 & 0x1) == 0)
    {
        return false;
    }

    const DWORD_PTR target = context.Dr0;
    return target == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.setterDispatchTarget) ||
        target == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.viewMatrixResultTarget) ||
        target == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.projectionMatrixResultTarget) ||
        target == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.transactionReturnTarget);
}

void CompleteRenderViewTransformProbe(CONTEXT& context)
{
    RestoreRenderViewTransformProbeRegisters(context);
    InterlockedExchange(&g_renderViewTransformProbe.state, 2);
}

LONG CALLBACK HandleRenderViewTransformProbe(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_renderViewTransformProbe.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_renderViewTransformProbe.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT& context = *exceptionPointers->ContextRecord;
    const DWORD instruction = static_cast<DWORD>(context.Eip);
    __try
    {
        if (instruction == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.setterDispatchTarget))
        {
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            if (gameImage == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }

            const DWORD renderView = *reinterpret_cast<const DWORD*>(gameImage + kActiveRenderViewGlobalRva);
            g_renderViewTransformProbe.renderView = renderView;
            g_renderViewTransformProbe.renderViewVtable =
                renderView == 0 ? 0 : *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(renderView));
            if (g_renderViewTransformProbe.renderViewVtable != 0)
            {
                const DWORD vtable = g_renderViewTransformProbe.renderViewVtable;
                g_renderViewTransformProbe.setterVirtualTarget = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(vtable + 0x58));
                g_renderViewTransformProbe.viewGetterVirtualTarget = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(vtable + 0x60));
                g_renderViewTransformProbe.projectionGetterVirtualTarget = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(vtable + 0x64));
            }
            CaptureRenderViewTransformProbeMatrix(
                g_renderViewTransformProbe.inputTransform,
                static_cast<DWORD>(context.Esp + kRenderViewTransformStackOffset));
            context.Dr0 = reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.viewMatrixResultTarget);
            context.Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (instruction == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.viewMatrixResultTarget))
        {
            CaptureRenderViewTransformProbeMatrix(g_renderViewTransformProbe.derivedView, static_cast<DWORD>(context.Eax));
            context.Dr0 = reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.projectionMatrixResultTarget);
            context.Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (instruction == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.projectionMatrixResultTarget))
        {
            CaptureRenderViewTransformProbeMatrix(g_renderViewTransformProbe.derivedProjection, static_cast<DWORD>(context.Eax));
            context.Dr0 = reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.transactionReturnTarget);
            context.Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (instruction == reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.transactionReturnTarget))
        {
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            CaptureRendererTransformCaches(
                g_renderViewTransformProbe.transactionAfterProjection,
                gameImage == nullptr ? nullptr : gameImage + kRendererTransactionStateGlobalRva);
            if (g_renderViewTransformProbe.transactionAfterProjection.readable &&
                g_renderViewTransformProbe.derivedView.readable &&
                g_renderViewTransformProbe.derivedProjection.readable)
            {
                g_renderViewTransformProbe.viewMatchesTransaction =
                    std::memcmp(
                        g_renderViewTransformProbe.derivedView.values,
                        g_renderViewTransformProbe.transactionAfterProjection.view,
                        sizeof(g_renderViewTransformProbe.derivedView.values)) == 0;
                g_renderViewTransformProbe.projectionMatchesTransaction =
                    std::memcmp(
                        g_renderViewTransformProbe.derivedProjection.values,
                        g_renderViewTransformProbe.transactionAfterProjection.projection,
                        sizeof(g_renderViewTransformProbe.derivedProjection.values)) == 0;
            }
            CompleteRenderViewTransformProbe(context);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_renderViewTransformProbe.failureInstruction = instruction;
        g_renderViewTransformProbe.structuredException = TRUE;
        CompleteRenderViewTransformProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool ArmRenderViewTransformProbe()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }

    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        return false;
    }
    if (g_renderViewTransformProbeHandler == nullptr)
    {
        g_renderViewTransformProbeHandler = AddVectoredExceptionHandler(1, HandleRenderViewTransformProbe);
        if (g_renderViewTransformProbeHandler == nullptr)
        {
            AppendLog(L"RenderView transform probe could not install its vectored handler (%lu).", GetLastError());
            return false;
        }
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
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
    if (!GetThreadContext(thread, &context) || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    g_renderViewTransformProbe = {};
    g_renderViewTransformProbe.threadId = g_createDeviceBreakpoint.threadId;
    g_renderViewTransformProbe.setterDispatchTarget = const_cast<std::byte*>(gameImage) + kRenderViewTransformSetterDispatchRva;
    g_renderViewTransformProbe.viewMatrixResultTarget = const_cast<std::byte*>(gameImage) + kRenderViewViewMatrixResultRva;
    g_renderViewTransformProbe.projectionMatrixResultTarget = const_cast<std::byte*>(gameImage) + kRenderViewProjectionMatrixResultRva;
    g_renderViewTransformProbe.transactionReturnTarget = const_cast<std::byte*>(gameImage) + kRenderViewTransformTransactionReturnRva;
    g_renderViewTransformProbe.originalDr0 = context.Dr0;
    g_renderViewTransformProbe.originalDr1 = context.Dr1;
    g_renderViewTransformProbe.originalDr2 = context.Dr2;
    g_renderViewTransformProbe.originalDr3 = context.Dr3;
    g_renderViewTransformProbe.originalDr6 = context.Dr6;
    g_renderViewTransformProbe.originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_renderViewTransformProbe.setterDispatchTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0x3)) | 0x1;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    InterlockedExchange(&g_renderViewTransformProbe.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(
        L"Armed one read-only RenderView transform probe on device thread %lu at 0x004668C1; it will copy the local input matrix, virtual +0x58/+0x60/+0x64 targets, the derived matrices, and post-setter renderer caches without invoking or writing game code/data.",
        g_renderViewTransformProbe.threadId);
    return true;
}

void RestoreRenderViewTransformProbe()
{
    if (InterlockedCompareExchange(&g_renderViewTransformProbe.state, 0, 0) != 1)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_renderViewTransformProbe.threadId);
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
    if (GetThreadContext(thread, &context) && IsRenderViewTransformProbeStateOwned(context))
    {
        RestoreRenderViewTransformProbeRegisters(context);
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

void AppendRenderViewTransformProbeMatrix(const wchar_t* label, const RenderViewTransformProbeMatrix& matrix)
{
    if (!matrix.readable)
    {
        AppendLog(L"RenderView transform probe %s matrix was not readable (address=%08lX).", label, static_cast<unsigned long>(matrix.address));
        return;
    }

    AppendLog(
        L"RenderView transform probe %s address=%08lX m=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f].",
        label,
        static_cast<unsigned long>(matrix.address),
        static_cast<double>(matrix.values[0]), static_cast<double>(matrix.values[1]), static_cast<double>(matrix.values[2]), static_cast<double>(matrix.values[3]),
        static_cast<double>(matrix.values[4]), static_cast<double>(matrix.values[5]), static_cast<double>(matrix.values[6]), static_cast<double>(matrix.values[7]),
        static_cast<double>(matrix.values[8]), static_cast<double>(matrix.values[9]), static_cast<double>(matrix.values[10]), static_cast<double>(matrix.values[11]),
        static_cast<double>(matrix.values[12]), static_cast<double>(matrix.values[13]), static_cast<double>(matrix.values[14]), static_cast<double>(matrix.values[15]));
}

DWORD WINAPI RunRenderViewTransformProbe(void*)
{
    constexpr DWORD kArmWindowMs = 120000;
    constexpr DWORD kObservationWindowMs = 15000;
    const DWORD armStartedAt = GetTickCount();
    bool armed = false;
    while (GetTickCount() - armStartedAt < kArmWindowMs)
    {
        if (ArmRenderViewTransformProbe())
        {
            armed = true;
            break;
        }
        Sleep(250);
    }

    if (!armed)
    {
        AppendLog(L"RenderView transform probe did not find an idle device-thread debug-register slot within %lu ms; no game code/data was changed.", kArmWindowMs);
        return 0;
    }

    const DWORD observationStartedAt = GetTickCount();
    while (GetTickCount() - observationStartedAt < kObservationWindowMs &&
           InterlockedCompareExchange(&g_renderViewTransformProbe.state, 0, 0) == 1)
    {
        Sleep(10);
    }
    RestoreRenderViewTransformProbe();
    if (InterlockedCompareExchange(&g_renderViewTransformProbe.state, 0, 0) == 1)
    {
        InterlockedExchange(&g_renderViewTransformProbe.state, 2);
    }

    const RenderViewTransformProbeRecord& record = g_renderViewTransformProbe;
    AppendLog(
        L"RenderView transform probe result: renderView=%08lX vtable=%08lX setter=%08lX viewGetter=%08lX projectionGetter=%08lX inputReadable=%d viewReadable=%d projectionReadable=%d transactionReadable=%d viewMatchesTransaction=%d projectionMatchesTransaction=%d structuredException=%d failureEip=%08lX registersRestored=%d.",
        static_cast<unsigned long>(record.renderView),
        static_cast<unsigned long>(record.renderViewVtable),
        static_cast<unsigned long>(record.setterVirtualTarget),
        static_cast<unsigned long>(record.viewGetterVirtualTarget),
        static_cast<unsigned long>(record.projectionGetterVirtualTarget),
        record.inputTransform.readable,
        record.derivedView.readable,
        record.derivedProjection.readable,
        record.transactionAfterProjection.readable,
        record.viewMatchesTransaction,
        record.projectionMatchesTransaction,
        record.structuredException,
        static_cast<unsigned long>(record.failureInstruction),
        record.cleanupRestored);
    AppendRenderViewTransformProbeMatrix(L"input transform", record.inputTransform);
    AppendRenderViewTransformProbeMatrix(L"derived View", record.derivedView);
    AppendRenderViewTransformProbeMatrix(L"derived Projection", record.derivedProjection);
    return 0;
}

void StartRenderViewTransformProbe()
{
    if (InterlockedCompareExchange(&g_renderViewTransformProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_renderViewTransformProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunRenderViewTransformProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"RenderView transform probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested one read-only RenderView transform probe; it waits for the existing passive traces to release the device-thread debug registers, then observes one ordinary per-view transform handoff.");
}

struct RenderViewSetterBaselineProbeRecord
{
    volatile LONG state = 0;
    volatile LONG matchingCalls = 0;
    volatile LONG captureClaimed = 0;
    DWORD threadId = 0;
    DWORD renderView = 0;
    DWORD activeRenderView = 0;
    DWORD callerReturn = 0;
    RenderViewTransformProbeMatrix sourceTransform = {};
    RenderViewTransformProbeMatrix storedTransform = {};
    BOOL sourceMatchesStored = FALSE;
    MH_STATUS disableStatus = MH_UNKNOWN;
    MH_STATUS removeStatus = MH_UNKNOWN;
    MH_STATUS uninitializeStatus = MH_UNKNOWN;
};

RenderViewSetterBaselineProbeRecord g_renderViewSetterBaselineProbe = {};
void* g_renderViewSetterBaselineProbeTarget = nullptr;
const std::byte* g_renderViewSetterBaselineGameImage = nullptr;
RenderViewSetTransformationFn g_originalRenderViewSetTransformationForBaselineProbe = nullptr;

bool IsVerifiedRenderViewSetterTarget(const void* target)
{
    constexpr BYTE kExpectedPrefix[] = {
        0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0xC1, 0x57, 0x8D,
        0x78, 0x3C, 0xB9, 0x10, 0x00, 0x00, 0x00, 0xF3, 0xA5,
    };
    if (target == nullptr)
    {
        return false;
    }

    __try
    {
        return std::memcmp(target, kExpectedPrefix, sizeof(kExpectedPrefix)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void __fastcall HookRenderViewSetTransformationBaselineProbe(void* renderView, void*, const void* transformation)
{
    const RenderViewSetTransformationFn original = g_originalRenderViewSetTransformationForBaselineProbe;
    if (original == nullptr)
    {
        return;
    }

    const std::byte* const gameImage = g_renderViewSetterBaselineGameImage;
    void* activeRenderView = nullptr;
    if (gameImage != nullptr)
    {
        __try
        {
            activeRenderView = *reinterpret_cast<void* const*>(gameImage + kActiveRenderViewGlobalRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeRenderView = nullptr;
        }
    }

    const DWORD callerReturn = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    const DWORD expectedReturn = gameImage == nullptr
        ? 0
        : static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(gameImage + kRenderViewSetTransformationReturnRva));
    const bool isConfirmedOrdinaryWorldCall =
        InterlockedCompareExchange(&g_renderViewSetterBaselineProbe.state, 0, 0) == 1 &&
        renderView != nullptr &&
        renderView == activeRenderView &&
        callerReturn == expectedReturn;
    const bool captureThisCall =
        isConfirmedOrdinaryWorldCall &&
        InterlockedCompareExchange(&g_renderViewSetterBaselineProbe.captureClaimed, 1, 0) == 0;

    if (isConfirmedOrdinaryWorldCall)
    {
        InterlockedIncrement(&g_renderViewSetterBaselineProbe.matchingCalls);
    }
    if (captureThisCall)
    {
        g_renderViewSetterBaselineProbe.threadId = GetCurrentThreadId();
        g_renderViewSetterBaselineProbe.renderView = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(renderView));
        g_renderViewSetterBaselineProbe.activeRenderView = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(activeRenderView));
        g_renderViewSetterBaselineProbe.callerReturn = callerReturn;
        CaptureRenderViewTransformProbeMatrix(
            g_renderViewSetterBaselineProbe.sourceTransform,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(transformation)));
    }

    // This is intentionally an exact pass-through. No matrix is changed in
    // the baseline mode; it only proves that a future eye-transform hook can
    // be installed and removed at the confirmed ordinary-world seam.
    original(renderView, transformation);

    if (captureThisCall)
    {
        CaptureRenderViewTransformProbeMatrix(
            g_renderViewSetterBaselineProbe.storedTransform,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(renderView) + kRenderViewTransformationOffset));
        if (g_renderViewSetterBaselineProbe.sourceTransform.readable &&
            g_renderViewSetterBaselineProbe.storedTransform.readable)
        {
            g_renderViewSetterBaselineProbe.sourceMatchesStored =
                std::memcmp(
                    g_renderViewSetterBaselineProbe.sourceTransform.values,
                    g_renderViewSetterBaselineProbe.storedTransform.values,
                    sizeof(g_renderViewSetterBaselineProbe.sourceTransform.values)) == 0;
        }
        InterlockedExchange(&g_renderViewSetterBaselineProbe.state, 2);
    }
}

DWORD WINAPI RunRenderViewSetterBaselineProbe(void*)
{
    constexpr DWORD kObservationWindowMs = 30000;
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"RenderView setter baseline probe skipped: the game image is unavailable.");
        return 0;
    }

    void* const target = const_cast<std::byte*>(gameImage) + kRenderViewSetTransformationRva;
    if (!IsVerifiedRenderViewSetterTarget(target))
    {
        AppendLog(L"RenderView setter baseline probe skipped: target=%p failed the profiled 0x005B7E00 instruction-prefix check.", target);
        return 0;
    }

    g_renderViewSetterBaselineProbe = {};
    g_renderViewSetterBaselineProbeTarget = target;
    g_renderViewSetterBaselineGameImage = gameImage;
    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK)
    {
        AppendLog(L"RenderView setter baseline probe skipped: MH_Initialize failed (%d).", static_cast<int>(initializeStatus));
        return 0;
    }

    const MH_STATUS createStatus = MH_CreateHook(
        target,
        reinterpret_cast<LPVOID>(&HookRenderViewSetTransformationBaselineProbe),
        reinterpret_cast<LPVOID*>(&g_originalRenderViewSetTransformationForBaselineProbe));
    if (createStatus != MH_OK || g_originalRenderViewSetTransformationForBaselineProbe == nullptr)
    {
        AppendLog(L"RenderView setter baseline probe skipped: MH_CreateHook target=%p status=%d trampoline=%p.",
            target,
            static_cast<int>(createStatus),
            reinterpret_cast<void*>(g_originalRenderViewSetTransformationForBaselineProbe));
        MH_Uninitialize();
        return 0;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK)
    {
        AppendLog(L"RenderView setter baseline probe skipped: MH_EnableHook target=%p failed (%d).", target, static_cast<int>(enableStatus));
        MH_RemoveHook(target);
        MH_Uninitialize();
        return 0;
    }

    InterlockedExchange(&g_renderViewSetterBaselineProbe.state, 1);
    AppendLog(
        L"Enabled one reversible pass-through hook at RenderView::setTransformation target=%p. It accepts only the confirmed ordinary-world return 0x004668D1 with the active DAT_009AB868 object, forwards the original matrix unchanged, then disables itself after one validation.",
        target);

    const DWORD startedAt = GetTickCount();
    while (GetTickCount() - startedAt < kObservationWindowMs &&
           InterlockedCompareExchange(&g_renderViewSetterBaselineProbe.state, 0, 0) == 1)
    {
        Sleep(10);
    }

    g_renderViewSetterBaselineProbe.disableStatus = MH_DisableHook(target);
    g_renderViewSetterBaselineProbe.removeStatus = MH_RemoveHook(target);
    g_renderViewSetterBaselineProbe.uninitializeStatus = MH_Uninitialize();
    if (InterlockedCompareExchange(&g_renderViewSetterBaselineProbe.state, 0, 0) == 1)
    {
        InterlockedExchange(&g_renderViewSetterBaselineProbe.state, 3);
    }

    const RenderViewSetterBaselineProbeRecord& record = g_renderViewSetterBaselineProbe;
    AppendLog(
        L"RenderView setter baseline result: matchingCalls=%ld renderView=%08lX activeRenderView=%08lX callerReturn=%08lX sourceReadable=%d storedReadable=%d sourceMatchesStored=%d disable=%d remove=%d uninitialize=%d.",
        static_cast<long>(record.matchingCalls),
        static_cast<unsigned long>(record.renderView),
        static_cast<unsigned long>(record.activeRenderView),
        static_cast<unsigned long>(record.callerReturn),
        record.sourceTransform.readable,
        record.storedTransform.readable,
        record.sourceMatchesStored,
        static_cast<int>(record.disableStatus),
        static_cast<int>(record.removeStatus),
        static_cast<int>(record.uninitializeStatus));
    AppendRenderViewTransformProbeMatrix(L"setter baseline source", record.sourceTransform);
    AppendRenderViewTransformProbeMatrix(L"setter baseline stored", record.storedTransform);
    return 0;
}

void StartRenderViewSetterBaselineProbe()
{
    if (InterlockedCompareExchange(&g_renderViewSetterBaselineProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_renderViewSetterBaselineProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunRenderViewSetterBaselineProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"RenderView setter baseline probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the one-shot RenderView setter baseline probe; it will install an opt-in exact pass-through hook, validate one confirmed ordinary-world call, and remove the hook before reporting.");
}

// This is deliberately a one-view, one-frame transform experiment rather than
// a stereo implementation. The source transform uses the D3D row-vector
// convention: rows 0..2 are its local axes and row 3 is translation. We move
// the copied source by a small diagnostic distance along local-right, then let
// the following ordinary-world setter call restore the engine's own source
// transform. Engine-world scale is not yet calibrated to a physical IPD.
constexpr float kRenderViewSingleEyeProbeLocalRightOffset = -0.032F;

struct RenderViewSingleEyeProbeRecord
{
    volatile LONG state = 0;
    volatile LONG matchingCalls = 0;
    volatile LONG adjustmentClaimed = 0;
    volatile LONG restorationClaimed = 0;
    DWORD threadId = 0;
    DWORD renderView = 0;
    DWORD activeRenderView = 0;
    DWORD callerReturn = 0;
    RenderViewTransformProbeMatrix sourceTransform = {};
    RenderViewTransformProbeMatrix adjustedTransform = {};
    RenderViewTransformProbeMatrix storedAfterAdjustment = {};
    RenderViewTransformProbeMatrix restorationSource = {};
    RenderViewTransformProbeMatrix storedAfterRestoration = {};
    BOOL adjustedTransformStored = FALSE;
    BOOL restorationSourceStored = FALSE;
    MH_STATUS disableStatus = MH_UNKNOWN;
    MH_STATUS removeStatus = MH_UNKNOWN;
    MH_STATUS uninitializeStatus = MH_UNKNOWN;
};

RenderViewSingleEyeProbeRecord g_renderViewSingleEyeProbe = {};
void* g_renderViewSingleEyeProbeTarget = nullptr;
const std::byte* g_renderViewSingleEyeProbeGameImage = nullptr;
RenderViewSetTransformationFn g_originalRenderViewSetTransformationForSingleEyeProbe = nullptr;

bool BuildRenderViewSingleEyeProbeTransform(
    const RenderViewTransformProbeMatrix& source,
    RenderViewTransformProbeMatrix& adjusted)
{
    if (!source.readable)
    {
        return false;
    }

    adjusted = {};
    adjusted.readable = TRUE;
    for (std::size_t index = 0; index < std::size(source.values); ++index)
    {
        if (!std::isfinite(source.values[index]))
        {
            adjusted = {};
            return false;
        }
        adjusted.values[index] = source.values[index];
    }

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float shifted = adjusted.values[12 + axis] +
            adjusted.values[axis] * kRenderViewSingleEyeProbeLocalRightOffset;
        if (!std::isfinite(shifted))
        {
            adjusted = {};
            return false;
        }
        adjusted.values[12 + axis] = shifted;
    }
    return true;
}

void __fastcall HookRenderViewSetTransformationSingleEyeProbe(void* renderView, void*, const void* transformation)
{
    const RenderViewSetTransformationFn original = g_originalRenderViewSetTransformationForSingleEyeProbe;
    if (original == nullptr)
    {
        return;
    }

    const std::byte* const gameImage = g_renderViewSingleEyeProbeGameImage;
    void* activeRenderView = nullptr;
    if (gameImage != nullptr)
    {
        __try
        {
            activeRenderView = *reinterpret_cast<void* const*>(gameImage + kActiveRenderViewGlobalRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            activeRenderView = nullptr;
        }
    }

    const DWORD callerReturn = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(_ReturnAddress()));
    const DWORD expectedReturn = gameImage == nullptr
        ? 0
        : static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(gameImage + kRenderViewSetTransformationReturnRva));
    const LONG state = InterlockedCompareExchange(&g_renderViewSingleEyeProbe.state, 0, 0);
    const bool isConfirmedOrdinaryWorldCall =
        (state == 1 || state == 2) &&
        renderView != nullptr &&
        renderView == activeRenderView &&
        callerReturn == expectedReturn;
    if (!isConfirmedOrdinaryWorldCall)
    {
        original(renderView, transformation);
        return;
    }

    InterlockedIncrement(&g_renderViewSingleEyeProbe.matchingCalls);
    if (state == 1 && InterlockedCompareExchange(&g_renderViewSingleEyeProbe.adjustmentClaimed, 1, 0) == 0)
    {
        RenderViewSingleEyeProbeRecord& record = g_renderViewSingleEyeProbe;
        record.threadId = GetCurrentThreadId();
        record.renderView = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(renderView));
        record.activeRenderView = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(activeRenderView));
        record.callerReturn = callerReturn;
        CaptureRenderViewTransformProbeMatrix(
            record.sourceTransform,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(transformation)));
        if (!BuildRenderViewSingleEyeProbeTransform(record.sourceTransform, record.adjustedTransform))
        {
            original(renderView, transformation);
            InterlockedExchange(&record.state, 4);
            return;
        }

        original(renderView, record.adjustedTransform.values);
        CaptureRenderViewTransformProbeMatrix(
            record.storedAfterAdjustment,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(renderView) + kRenderViewTransformationOffset));
        record.adjustedTransformStored =
            record.storedAfterAdjustment.readable &&
            std::memcmp(
                record.adjustedTransform.values,
                record.storedAfterAdjustment.values,
                sizeof(record.adjustedTransform.values)) == 0;
        InterlockedExchange(&record.state, 2);
        return;
    }

    if (state == 2 && InterlockedCompareExchange(&g_renderViewSingleEyeProbe.restorationClaimed, 1, 0) == 0)
    {
        RenderViewSingleEyeProbeRecord& record = g_renderViewSingleEyeProbe;
        CaptureRenderViewTransformProbeMatrix(
            record.restorationSource,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(transformation)));
        original(renderView, transformation);
        CaptureRenderViewTransformProbeMatrix(
            record.storedAfterRestoration,
            static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(renderView) + kRenderViewTransformationOffset));
        record.restorationSourceStored =
            record.restorationSource.readable &&
            record.storedAfterRestoration.readable &&
            std::memcmp(
                record.restorationSource.values,
                record.storedAfterRestoration.values,
                sizeof(record.restorationSource.values)) == 0;
        InterlockedExchange(&record.state, 3);
        return;
    }

    original(renderView, transformation);
}

DWORD WINAPI RunRenderViewSingleEyeProbe(void*)
{
    constexpr DWORD kObservationWindowMs = 30000;
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        AppendLog(L"RenderView single-eye probe skipped: the game image is unavailable.");
        return 0;
    }

    void* const target = const_cast<std::byte*>(gameImage) + kRenderViewSetTransformationRva;
    if (!IsVerifiedRenderViewSetterTarget(target))
    {
        AppendLog(L"RenderView single-eye probe skipped: target=%p failed the profiled 0x005B7E00 instruction-prefix check.", target);
        return 0;
    }

    g_renderViewSingleEyeProbe = {};
    g_renderViewSingleEyeProbeTarget = target;
    g_renderViewSingleEyeProbeGameImage = gameImage;
    const MH_STATUS initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK)
    {
        AppendLog(L"RenderView single-eye probe skipped: MH_Initialize failed (%d).", static_cast<int>(initializeStatus));
        return 0;
    }

    const MH_STATUS createStatus = MH_CreateHook(
        target,
        reinterpret_cast<LPVOID>(&HookRenderViewSetTransformationSingleEyeProbe),
        reinterpret_cast<LPVOID*>(&g_originalRenderViewSetTransformationForSingleEyeProbe));
    if (createStatus != MH_OK || g_originalRenderViewSetTransformationForSingleEyeProbe == nullptr)
    {
        AppendLog(L"RenderView single-eye probe skipped: MH_CreateHook target=%p status=%d trampoline=%p.",
            target,
            static_cast<int>(createStatus),
            reinterpret_cast<void*>(g_originalRenderViewSetTransformationForSingleEyeProbe));
        MH_Uninitialize();
        return 0;
    }

    const MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK)
    {
        AppendLog(L"RenderView single-eye probe skipped: MH_EnableHook target=%p failed (%d).", target, static_cast<int>(enableStatus));
        MH_RemoveHook(target);
        MH_Uninitialize();
        return 0;
    }

    InterlockedExchange(&g_renderViewSingleEyeProbe.state, 1);
    AppendLog(
        L"Enabled one reversible single-eye RenderView transform hook at target=%p. It accepts only the confirmed ordinary-world return 0x004668D1 with the active DAT_009AB868 object, offsets one copied transform by %.3f along local-right, then requires the next matching game setter call to restore and verify the engine transform before hook removal.",
        target,
        static_cast<double>(kRenderViewSingleEyeProbeLocalRightOffset));

    const DWORD startedAt = GetTickCount();
    while (true)
    {
        const LONG state = InterlockedCompareExchange(&g_renderViewSingleEyeProbe.state, 0, 0);
        if (state == 3 || state == 4)
        {
            break;
        }
        // Before any adjustment this remains a bounded no-op observation. Once
        // a copied offset was accepted, keep the narrow detour installed until
        // a later confirmed engine setter call restores the ordinary matrix.
        // The loader requires a bounded diagnostic child for this experiment,
        // so an interrupted run cannot leave a persistent game process altered.
        if (state == 1 && GetTickCount() - startedAt >= kObservationWindowMs)
        {
            InterlockedExchange(&g_renderViewSingleEyeProbe.state, 4);
            break;
        }
        Sleep(10);
    }

    g_renderViewSingleEyeProbe.disableStatus = MH_DisableHook(target);
    g_renderViewSingleEyeProbe.removeStatus = MH_RemoveHook(target);
    g_renderViewSingleEyeProbe.uninitializeStatus = MH_Uninitialize();

    const RenderViewSingleEyeProbeRecord& record = g_renderViewSingleEyeProbe;
    AppendLog(
        L"RenderView single-eye result: state=%ld matchingCalls=%ld renderView=%08lX activeRenderView=%08lX callerReturn=%08lX adjustedStored=%d restorationStored=%d disable=%d remove=%d uninitialize=%d.",
        static_cast<long>(record.state),
        static_cast<long>(record.matchingCalls),
        static_cast<unsigned long>(record.renderView),
        static_cast<unsigned long>(record.activeRenderView),
        static_cast<unsigned long>(record.callerReturn),
        record.adjustedTransformStored,
        record.restorationSourceStored,
        static_cast<int>(record.disableStatus),
        static_cast<int>(record.removeStatus),
        static_cast<int>(record.uninitializeStatus));
    AppendRenderViewTransformProbeMatrix(L"single-eye source", record.sourceTransform);
    AppendRenderViewTransformProbeMatrix(L"single-eye adjusted", record.adjustedTransform);
    AppendRenderViewTransformProbeMatrix(L"single-eye stored after adjustment", record.storedAfterAdjustment);
    AppendRenderViewTransformProbeMatrix(L"single-eye restoration source", record.restorationSource);
    AppendRenderViewTransformProbeMatrix(L"single-eye stored after restoration", record.storedAfterRestoration);
    return 0;
}

void StartRenderViewSingleEyeProbe()
{
    if (InterlockedCompareExchange(&g_renderViewSingleEyeProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_renderViewSingleEyeProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunRenderViewSingleEyeProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"RenderView single-eye probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the opt-in one-shot RenderView single-eye probe; it offsets only one confirmed ordinary-world transform and requires the next matching game setter call to prove restoration before removing the hook.");
}

constexpr std::size_t kConfiguredViewListProbeMaximumHandles = 8;

struct ConfiguredViewListProbeRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* dispatchTarget = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    DWORD configuredViewManager = 0;
    DWORD listBegin = 0;
    DWORD listEnd = 0;
    DWORD declaredHandleCount = 0;
    DWORD storedHandleCount = 0;
    DWORD handles[kConfiguredViewListProbeMaximumHandles] = {};
    DWORD resolvedViewConfig = 0;
    DWORD configurationDword0 = 0;
    DWORD viewOwner = 0;
    DWORD renderView = 0;
    DWORD viewOwnerRenderView = 0;
    DWORD renderViewVtable = 0;
    DWORD cloneVirtualTarget = 0;
    float viewPort[4] = {};
    BOOL viewPortReadable = FALSE;
    BOOL viewOwnerRenderViewMatches = FALSE;
    BOOL cleanupRestored = FALSE;
    BOOL structuredException = FALSE;
    DWORD failureInstruction = 0;
};

ConfiguredViewListProbeRecord g_configuredViewListProbe = {};
PVOID g_configuredViewListProbeHandler = nullptr;

void RestoreConfiguredViewListProbeRegisters(CONTEXT& context)
{
    context.Dr0 = g_configuredViewListProbe.originalDr0;
    context.Dr1 = g_configuredViewListProbe.originalDr1;
    context.Dr2 = g_configuredViewListProbe.originalDr2;
    context.Dr3 = g_configuredViewListProbe.originalDr3;
    context.Dr6 = g_configuredViewListProbe.originalDr6;
    context.Dr7 = g_configuredViewListProbe.originalDr7;
    g_configuredViewListProbe.cleanupRestored = TRUE;
}

void CompleteConfiguredViewListProbe(CONTEXT& context)
{
    RestoreConfiguredViewListProbeRegisters(context);
    InterlockedExchange(&g_configuredViewListProbe.state, 2);
}

LONG CALLBACK HandleConfiguredViewListProbe(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_configuredViewListProbe.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT& context = *exceptionPointers->ContextRecord;
    const DWORD instruction = static_cast<DWORD>(context.Eip);
    if (instruction != reinterpret_cast<DWORD_PTR>(g_configuredViewListProbe.dispatchTarget))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    __try
    {
        const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        if (gameImage == nullptr)
        {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        }

        ConfiguredViewListProbeRecord& record = g_configuredViewListProbe;
        record.resolvedViewConfig = static_cast<DWORD>(context.Ebp);
        record.viewOwner = static_cast<DWORD>(context.Eax);
        record.renderView = static_cast<DWORD>(context.Ecx);
        record.configuredViewManager = *reinterpret_cast<const DWORD*>(gameImage + kConfiguredViewManagerGlobalRva);
        if (record.configuredViewManager != 0)
        {
            const DWORD manager = record.configuredViewManager;
            record.listBegin = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListBeginOffset));
            record.listEnd = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset));
            if (record.listEnd >= record.listBegin && (record.listEnd - record.listBegin) % sizeof(DWORD) == 0)
            {
                record.declaredHandleCount = (record.listEnd - record.listBegin) / sizeof(DWORD);
                record.storedHandleCount = static_cast<DWORD>(
                    std::min<std::size_t>(record.declaredHandleCount, kConfiguredViewListProbeMaximumHandles));
                for (DWORD index = 0; index < record.storedHandleCount; ++index)
                {
                    record.handles[index] = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(record.listBegin + index * sizeof(DWORD)));
                }
            }
        }
        if (record.resolvedViewConfig != 0)
        {
            record.configurationDword0 = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(record.resolvedViewConfig));
        }
        if (record.viewOwner != 0)
        {
            record.viewOwnerRenderView = *reinterpret_cast<const DWORD*>(
                static_cast<DWORD_PTR>(record.viewOwner + kViewOwnerRenderViewOffset));
        }
        if (record.renderView != 0)
        {
            record.renderViewVtable = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(record.renderView));
            if (record.renderViewVtable != 0)
            {
                record.cloneVirtualTarget = *reinterpret_cast<const DWORD*>(
                    static_cast<DWORD_PTR>(record.renderViewVtable + kRenderViewCloneVtableOffset));
            }
            std::memcpy(
                record.viewPort,
                reinterpret_cast<const void*>(static_cast<DWORD_PTR>(record.renderView + kRenderViewViewPortOffset)),
                sizeof(record.viewPort));
            record.viewPortReadable = TRUE;
        }
        record.viewOwnerRenderViewMatches =
            record.viewOwnerRenderView != 0 && record.viewOwnerRenderView == record.renderView;
        CompleteConfiguredViewListProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_configuredViewListProbe.failureInstruction = instruction;
        g_configuredViewListProbe.structuredException = TRUE;
        CompleteConfiguredViewListProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

bool IsConfiguredViewListProbeStateOwned(const CONTEXT& context)
{
    return InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) == 1 &&
        (context.Dr7 & 0x1) != 0 &&
        context.Dr0 == reinterpret_cast<DWORD_PTR>(g_configuredViewListProbe.dispatchTarget);
}

bool ArmConfiguredViewListProbe()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }

    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        return false;
    }
    if (g_configuredViewListProbeHandler == nullptr)
    {
        g_configuredViewListProbeHandler = AddVectoredExceptionHandler(1, HandleConfiguredViewListProbe);
        if (g_configuredViewListProbeHandler == nullptr)
        {
            AppendLog(L"Configured-view list probe could not install its vectored handler (%lu).", GetLastError());
            return false;
        }
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
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
    if (!GetThreadContext(thread, &context) || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    g_configuredViewListProbe = {};
    g_configuredViewListProbe.threadId = g_createDeviceBreakpoint.threadId;
    g_configuredViewListProbe.dispatchTarget = const_cast<std::byte*>(gameImage) + kRenderViewDispatchPostAssignmentRva;
    g_configuredViewListProbe.originalDr0 = context.Dr0;
    g_configuredViewListProbe.originalDr1 = context.Dr1;
    g_configuredViewListProbe.originalDr2 = context.Dr2;
    g_configuredViewListProbe.originalDr3 = context.Dr3;
    g_configuredViewListProbe.originalDr6 = context.Dr6;
    g_configuredViewListProbe.originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_configuredViewListProbe.dispatchTarget);
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0x3)) | 0x1;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    InterlockedExchange(&g_configuredViewListProbe.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one read-only configured-view list probe at 0x004662DA on device thread %lu; it will copy the list metadata, one resolved view owner, RenderView, viewport, and clone target without calling or writing game code/data.", g_configuredViewListProbe.threadId);
    return true;
}

void RestoreConfiguredViewListProbe()
{
    if (InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) != 1)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_configuredViewListProbe.threadId);
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
    if (GetThreadContext(thread, &context) && IsConfiguredViewListProbeStateOwned(context))
    {
        RestoreConfiguredViewListProbeRegisters(context);
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

DWORD WINAPI RunConfiguredViewListProbe(void*)
{
    constexpr DWORD kArmWindowMs = 120000;
    constexpr DWORD kObservationWindowMs = 30000;
    const DWORD armStartedAt = GetTickCount();
    while (GetTickCount() - armStartedAt < kArmWindowMs)
    {
        if (ArmConfiguredViewListProbe())
        {
            break;
        }
        Sleep(250);
    }
    if (InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) != 1)
    {
        AppendLog(L"Configured-view list probe did not find an idle device-thread debug-register slot within %lu ms; no game code/data was changed.", kArmWindowMs);
        return 0;
    }

    const DWORD observationStartedAt = GetTickCount();
    while (GetTickCount() - observationStartedAt < kObservationWindowMs &&
           InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) == 1)
    {
        Sleep(10);
    }
    RestoreConfiguredViewListProbe();
    if (InterlockedCompareExchange(&g_configuredViewListProbe.state, 0, 0) == 1)
    {
        InterlockedExchange(&g_configuredViewListProbe.state, 3);
    }

    const ConfiguredViewListProbeRecord& record = g_configuredViewListProbe;
    AppendLog(
        L"Configured-view list probe result: manager=%08lX begin=%08lX end=%08lX declaredHandles=%lu storedHandles=%lu resolvedConfig=%08lX configDword0=%08lX viewOwner=%08lX ownerRenderView=%08lX renderView=%08lX renderViewVtable=%08lX ownerMatches=%d clone=%08lX viewportReadable=%d viewport=[%.3f %.3f %.3f %.3f] structuredException=%d failureEip=%08lX registersRestored=%d.",
        static_cast<unsigned long>(record.configuredViewManager),
        static_cast<unsigned long>(record.listBegin),
        static_cast<unsigned long>(record.listEnd),
        static_cast<unsigned long>(record.declaredHandleCount),
        static_cast<unsigned long>(record.storedHandleCount),
        static_cast<unsigned long>(record.resolvedViewConfig),
        static_cast<unsigned long>(record.configurationDword0),
        static_cast<unsigned long>(record.viewOwner),
        static_cast<unsigned long>(record.viewOwnerRenderView),
        static_cast<unsigned long>(record.renderView),
        static_cast<unsigned long>(record.renderViewVtable),
        record.viewOwnerRenderViewMatches,
        static_cast<unsigned long>(record.cloneVirtualTarget),
        record.viewPortReadable,
        static_cast<double>(record.viewPort[0]), static_cast<double>(record.viewPort[1]),
        static_cast<double>(record.viewPort[2]), static_cast<double>(record.viewPort[3]),
        record.structuredException,
        static_cast<unsigned long>(record.failureInstruction),
        record.cleanupRestored);
    for (DWORD index = 0; index < record.storedHandleCount; ++index)
    {
        AppendLog(L"Configured-view list handle[%lu]=%08lX.", static_cast<unsigned long>(index), static_cast<unsigned long>(record.handles[index]));
    }
    return 0;
}

void StartConfiguredViewListProbe()
{
    if (InterlockedCompareExchange(&g_configuredViewListProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_configuredViewListProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunConfiguredViewListProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Configured-view list probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the one-shot configured-view list probe; it waits for existing passive traces to release the device-thread debug registers, then observes one per-view dispatch without invoking or writing game code/data.");
}

// This probe intentionally observes the submitted-scene stack below the
// per-view renderer.  It never calls an engine method or a D3D interface.  DR3
// remains unused until a mode entry occurs, so the recorded per-item route is
// causally after a known ordinary-view batch entry rather than an unrelated
// earlier renderer item on the same thread.
struct SceneBatchProbeVector
{
    DWORD address = 0;
    DWORD begin = 0;
    DWORD end = 0;
    DWORD itemCount = 0;
    BOOL layoutValid = FALSE;
};

struct SceneBatchProbeEntry
{
    DWORD instruction = 0;
    DWORD receiver = 0;
    DWORD renderModeBefore = 0;
    DWORD resourceAtBc = 0;
    DWORD dispatchRenderView = 0;
    DWORD dispatchParameter = 0;
    DWORD rendererContext = 0;
    DWORD stackReturnAddress = 0;
    SceneBatchProbeVector prepassList = {};
    SceneBatchProbeVector itemList = {};
};

struct SceneBatchProbeSinkCapture
{
    DWORD instruction = 0;
    DWORD thisPointer = 0;
    DWORD stackReturnAddress = 0;
    DWORD arguments[3] = {};
};

struct SceneBatchProbeRecord
{
    volatile LONG state = 0;
    DWORD threadId = 0;
    void* mode0Target = nullptr;
    void* mode1Target = nullptr;
    void* mode2Target = nullptr;
    void* itemRouterTarget = nullptr;
    void* itemTypeQueryReturnTarget = nullptr;
    void* itemSubmissionTarget = nullptr;
    void* itemSubmissionSinkTarget = nullptr;
    void* itemSubmissionTransformTarget = nullptr;
    void* itemSubmissionPrimitive78Target = nullptr;
    void* itemSubmissionPrimitive7CTarget = nullptr;
    void* itemSubmissionPrimitive88Target = nullptr;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    volatile LONG mode0Observed = 0;
    volatile LONG mode1Observed = 0;
    volatile LONG mode2Observed = 0;
    volatile LONG itemRouterObserved = 0;
    volatile LONG itemRouterSkipped = 0;
    volatile LONG itemTypeQueryObserved = 0;
    volatile LONG itemSubmissionObserved = 0;
    volatile LONG itemSubmissionTransformObserved = 0;
    volatile LONG itemSubmissionPrimitive78Observed = 0;
    volatile LONG itemSubmissionPrimitive7CObserved = 0;
    volatile LONG itemSubmissionPrimitive88Observed = 0;
    BOOL itemRouterArmed = FALSE;
    BOOL itemRouterBoundaryComplete = FALSE;
    BOOL itemTypeQueryReturnArmed = FALSE;
    BOOL itemSubmissionArmed = FALSE;
    BOOL itemSubmissionSinkTraceArmed = FALSE;
    BOOL cleanupRestored = FALSE;
    BOOL structuredException = FALSE;
    DWORD failureInstruction = 0;
    SceneBatchProbeEntry mode0 = {};
    SceneBatchProbeEntry mode1 = {};
    SceneBatchProbeEntry mode2 = {};
    DWORD item = 0;
    DWORD itemVtable = 0;
    DWORD itemTypeQueryTarget = 0;
    DWORD itemContextRenderView = 0;
    DWORD itemContextParameter = 0;
    DWORD itemStackReturnAddress = 0;
    DWORD itemTypeId = 0;
    DWORD primaryItemTypeId = 0;
    DWORD secondaryItemTypeId = 0;
    DWORD itemTypeQueryReturnAddress = 0;
    DWORD itemSubmissionInstruction = 0;
    DWORD itemSubmissionThis = 0;
    DWORD itemSubmissionArgument0 = 0;
    DWORD itemSubmissionArgument1 = 0;
    DWORD itemSubmissionReturnAddress = 0;
    DWORD itemSubmissionOwner = 0;
    DWORD itemSubmissionBatchBegin = 0;
    DWORD itemSubmissionBatchEnd = 0;
    DWORD itemSubmissionLastBatchRecord = 0;
    DWORD itemSubmissionTransformDispatcher = 0;
    DWORD itemSubmissionTransformDispatcherVtable = 0;
    DWORD itemSubmissionPrimitiveDispatcher = 0;
    DWORD itemSubmissionPrimitiveDispatcherVtable = 0;
    DWORD itemSubmissionSinkStage = 0;
    SceneBatchProbeSinkCapture itemSubmissionTransformSink = {};
    SceneBatchProbeSinkCapture itemSubmissionPrimitive78Sink = {};
    SceneBatchProbeSinkCapture itemSubmissionPrimitive7CSink = {};
    SceneBatchProbeSinkCapture itemSubmissionPrimitive88Sink = {};
    DWORD activeRenderView = 0;
    DWORD rendererTransaction = 0;
};

SceneBatchProbeRecord g_sceneBatchProbe = {};
PVOID g_sceneBatchProbeHandler = nullptr;

void CaptureSceneBatchProbeVector(DWORD address, SceneBatchProbeVector& vector)
{
    vector.address = address;
    if (address == 0)
    {
        return;
    }
    vector.begin = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(address) + 4);
    vector.end = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(address) + 8);
    if (vector.end >= vector.begin && (vector.end - vector.begin) % sizeof(DWORD) == 0)
    {
        vector.itemCount = (vector.end - vector.begin) / sizeof(DWORD);
        vector.layoutValid = TRUE;
    }
}

void CaptureSceneBatchProbeEntry(
    SceneBatchProbeEntry& entry,
    const CONTEXT& context,
    DWORD expectedMode,
    std::size_t prepassListOffset,
    std::size_t itemListOffset)
{
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    }

    entry.instruction = static_cast<DWORD>(context.Eip);
    entry.receiver = static_cast<DWORD>(context.Ecx);
    entry.renderModeBefore = *reinterpret_cast<const DWORD*>(gameImage + kSkinningShaderRenderModeGlobalRva);
    entry.stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
    if (entry.receiver == 0)
    {
        return;
    }

    const DWORD_PTR receiver = entry.receiver;
    entry.resourceAtBc = *reinterpret_cast<const DWORD*>(receiver + 0xBC);
    entry.dispatchRenderView = *reinterpret_cast<const DWORD*>(receiver + 0x98);
    entry.dispatchParameter = *reinterpret_cast<const DWORD*>(receiver + 0x9C);
    entry.rendererContext = *reinterpret_cast<const DWORD*>(receiver + 0xB0);
    if (prepassListOffset != 0)
    {
        CaptureSceneBatchProbeVector(
            static_cast<DWORD>(receiver + prepassListOffset),
            entry.prepassList);
    }
    CaptureSceneBatchProbeVector(
        static_cast<DWORD>(receiver + itemListOffset),
        entry.itemList);
    (void)expectedMode;
}

void CaptureSceneBatchProbeSink(
    SceneBatchProbeSinkCapture& sink,
    const CONTEXT& context)
{
    sink.instruction = static_cast<DWORD>(context.Eip);
    sink.thisPointer = static_cast<DWORD>(context.Ecx);
    sink.stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
    sink.arguments[0] = *reinterpret_cast<const DWORD*>(context.Esp + 4);
    sink.arguments[1] = *reinterpret_cast<const DWORD*>(context.Esp + 8);
    sink.arguments[2] = *reinterpret_cast<const DWORD*>(context.Esp + 12);
}

bool ArmSceneBatchProbeSink(CONTEXT& context, DWORD stage, void* target)
{
    if (target == nullptr)
    {
        return false;
    }
    g_sceneBatchProbe.itemSubmissionSinkStage = stage;
    g_sceneBatchProbe.itemSubmissionSinkTarget = target;
    context.Dr3 = reinterpret_cast<DWORD_PTR>(target);
    context.Dr7 |= 0x40;
    return true;
}

void RestoreSceneBatchProbeRegisters(CONTEXT& context)
{
    context.Dr0 = g_sceneBatchProbe.originalDr0;
    context.Dr1 = g_sceneBatchProbe.originalDr1;
    context.Dr2 = g_sceneBatchProbe.originalDr2;
    context.Dr3 = g_sceneBatchProbe.originalDr3;
    context.Dr6 = g_sceneBatchProbe.originalDr6;
    context.Dr7 = g_sceneBatchProbe.originalDr7;
    g_sceneBatchProbe.cleanupRestored = TRUE;
}

void CompleteSceneBatchProbe(CONTEXT& context)
{
    RestoreSceneBatchProbeRegisters(context);
    InterlockedExchange(&g_sceneBatchProbe.state, 2);
}

bool SceneBatchProbeHasAllEvents()
{
    return InterlockedCompareExchange(&g_sceneBatchProbe.mode0Observed, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_sceneBatchProbe.mode1Observed, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_sceneBatchProbe.mode2Observed, 0, 0) != 0 &&
        InterlockedCompareExchange(&g_sceneBatchProbe.itemRouterObserved, 0, 0) != 0 &&
        (g_sceneBatchProbe.itemRouterBoundaryComplete ||
            (InterlockedCompareExchange(&g_sceneBatchProbe.itemTypeQueryObserved, 0, 0) != 0 &&
             InterlockedCompareExchange(&g_sceneBatchProbe.itemSubmissionObserved, 0, 0) != 0 &&
             (!g_sceneBatchProbe.itemSubmissionSinkTraceArmed ||
                (InterlockedCompareExchange(&g_sceneBatchProbe.itemSubmissionTransformObserved, 0, 0) != 0 &&
                 InterlockedCompareExchange(&g_sceneBatchProbe.itemSubmissionPrimitive78Observed, 0, 0) != 0 &&
                 InterlockedCompareExchange(&g_sceneBatchProbe.itemSubmissionPrimitive7CObserved, 0, 0) != 0 &&
                 InterlockedCompareExchange(&g_sceneBatchProbe.itemSubmissionPrimitive88Observed, 0, 0) != 0))));
}

LONG CALLBACK HandleSceneBatchProbe(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_sceneBatchProbe.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_sceneBatchProbe.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT& context = *exceptionPointers->ContextRecord;
    const DWORD_PTR instruction = context.Eip;
    __try
    {
        if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode0Target))
        {
            CaptureSceneBatchProbeEntry(g_sceneBatchProbe.mode0, context, 0, 0x78, 0x48);
            InterlockedExchange(&g_sceneBatchProbe.mode0Observed, 1);
            context.Dr0 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode1Target))
        {
            CaptureSceneBatchProbeEntry(g_sceneBatchProbe.mode1, context, 1, 0, 0x58);
            InterlockedExchange(&g_sceneBatchProbe.mode1Observed, 1);
            context.Dr1 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode2Target))
        {
            CaptureSceneBatchProbeEntry(g_sceneBatchProbe.mode2, context, 2, 0x88, 0x68);
            InterlockedExchange(&g_sceneBatchProbe.mode2Observed, 1);
            context.Dr2 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemRouterTarget))
        {
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            if (gameImage == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
            const DWORD item = static_cast<DWORD>(context.Ecx);
            const DWORD stackReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
            const DWORD renderView = *reinterpret_cast<const DWORD*>(context.Esp + 4);
            const DWORD dispatchParameter = *reinterpret_cast<const DWORD*>(context.Esp + 8);
            g_sceneBatchProbe.item = item;
            g_sceneBatchProbe.itemStackReturnAddress = stackReturnAddress;
            g_sceneBatchProbe.itemContextRenderView = renderView;
            g_sceneBatchProbe.itemContextParameter = dispatchParameter;
            if (g_sceneBatchProbe.item != 0)
            {
                g_sceneBatchProbe.itemVtable = *reinterpret_cast<const DWORD*>(g_sceneBatchProbe.item);
                if (g_sceneBatchProbe.itemVtable != 0)
                {
                    g_sceneBatchProbe.itemTypeQueryTarget = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(g_sceneBatchProbe.itemVtable) + 0x0C);
                }
            }
            g_sceneBatchProbe.activeRenderView = *reinterpret_cast<const DWORD*>(gameImage + kActiveRenderViewGlobalRva);
            g_sceneBatchProbe.rendererTransaction = *reinterpret_cast<const DWORD*>(gameImage + kRendererTransactionStateGlobalRva);
            InterlockedExchange(&g_sceneBatchProbe.itemRouterObserved, 1);
            // This default probe ends at one router entry. In particular, the
            // null second argument takes 0x0062B2F0 instead of the nearby
            // type-query branch, and this hot path must never be re-armed to
            // search for another item. Deeper classification needs a separate
            // opt-in probe with a proven one-shot gate.
            if (dispatchParameter == 0)
            {
                InterlockedIncrement(&g_sceneBatchProbe.itemRouterSkipped);
            }
            g_sceneBatchProbe.itemRouterBoundaryComplete = TRUE;
            context.Dr3 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemTypeQueryReturnTarget))
        {
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            if (gameImage == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
            g_sceneBatchProbe.itemTypeId = static_cast<DWORD>(context.Eax);
            g_sceneBatchProbe.primaryItemTypeId = *reinterpret_cast<const DWORD*>(gameImage + kSkinningShaderPrimaryItemTypeGlobalRva);
            g_sceneBatchProbe.secondaryItemTypeId = *reinterpret_cast<const DWORD*>(gameImage + kSkinningShaderSecondaryItemTypeGlobalRva);
            g_sceneBatchProbe.itemTypeQueryReturnAddress = static_cast<DWORD>(context.Eip);
            InterlockedExchange(&g_sceneBatchProbe.itemTypeQueryObserved, 1);
            if (g_sceneBatchProbe.itemTypeId == g_sceneBatchProbe.primaryItemTypeId)
            {
                g_sceneBatchProbe.itemSubmissionTarget = const_cast<std::byte*>(gameImage) + kSkinningShaderPrimaryItemSubmissionRva;
            }
            else if (g_sceneBatchProbe.itemTypeId == g_sceneBatchProbe.secondaryItemTypeId)
            {
                g_sceneBatchProbe.itemSubmissionTarget = const_cast<std::byte*>(gameImage) + kSkinningShaderSecondaryItemSubmissionRva;
            }
            if (g_sceneBatchProbe.itemSubmissionTarget != nullptr)
            {
                g_sceneBatchProbe.itemSubmissionArmed = TRUE;
                context.Dr3 = reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionTarget);
                context.Dr7 |= 0x40;
            }
            else
            {
                context.Dr3 = 0;
                context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
            }
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionTarget))
        {
            g_sceneBatchProbe.itemSubmissionInstruction = static_cast<DWORD>(context.Eip);
            g_sceneBatchProbe.itemSubmissionThis = static_cast<DWORD>(context.Ecx);
            g_sceneBatchProbe.itemSubmissionReturnAddress = *reinterpret_cast<const DWORD*>(context.Esp);
            g_sceneBatchProbe.itemSubmissionArgument0 = *reinterpret_cast<const DWORD*>(context.Esp + 4);
            g_sceneBatchProbe.itemSubmissionArgument1 = *reinterpret_cast<const DWORD*>(context.Esp + 8);
            InterlockedExchange(&g_sceneBatchProbe.itemSubmissionObserved, 1);
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            if (gameImage == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
            if (instruction == reinterpret_cast<DWORD_PTR>(const_cast<std::byte*>(gameImage) + kSkinningShaderPrimaryItemSubmissionRva))
            {
                if (g_sceneBatchProbe.itemSubmissionThis != 0)
                {
                    g_sceneBatchProbe.itemSubmissionOwner = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionThis) + 0x24);
                }
                if (g_sceneBatchProbe.itemSubmissionOwner != 0)
                {
                    const DWORD_PTR owner = g_sceneBatchProbe.itemSubmissionOwner;
                    g_sceneBatchProbe.itemSubmissionBatchBegin = *reinterpret_cast<const DWORD*>(owner + 0xBC);
                    g_sceneBatchProbe.itemSubmissionBatchEnd = *reinterpret_cast<const DWORD*>(owner + 0xC0);
                    if (g_sceneBatchProbe.itemSubmissionBatchEnd >= g_sceneBatchProbe.itemSubmissionBatchBegin &&
                        g_sceneBatchProbe.itemSubmissionBatchEnd - g_sceneBatchProbe.itemSubmissionBatchBegin >= 0x10 &&
                        (g_sceneBatchProbe.itemSubmissionBatchEnd - g_sceneBatchProbe.itemSubmissionBatchBegin) % 0x10 == 0)
                    {
                        g_sceneBatchProbe.itemSubmissionLastBatchRecord = g_sceneBatchProbe.itemSubmissionBatchEnd - 0x10;
                    }
                }
                g_sceneBatchProbe.itemSubmissionTransformDispatcher = *reinterpret_cast<const DWORD*>(
                    gameImage + kSkinningShaderTransformDispatcherGlobalRva);
                if (g_sceneBatchProbe.itemSubmissionTransformDispatcher != 0)
                {
                    g_sceneBatchProbe.itemSubmissionTransformDispatcherVtable = *reinterpret_cast<const DWORD*>(
                        g_sceneBatchProbe.itemSubmissionTransformDispatcher);
                    if (g_sceneBatchProbe.itemSubmissionTransformDispatcherVtable != 0)
                    {
                        g_sceneBatchProbe.itemSubmissionTransformTarget = reinterpret_cast<void*>(static_cast<DWORD_PTR>(
                            *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionTransformDispatcherVtable) + 0x94)));
                    }
                }
                g_sceneBatchProbe.itemSubmissionPrimitiveDispatcher = *reinterpret_cast<const DWORD*>(
                    gameImage + kSkinningShaderPrimitiveDispatcherGlobalRva);
                if (g_sceneBatchProbe.itemSubmissionPrimitiveDispatcher != 0)
                {
                    g_sceneBatchProbe.itemSubmissionPrimitiveDispatcherVtable = *reinterpret_cast<const DWORD*>(
                        g_sceneBatchProbe.itemSubmissionPrimitiveDispatcher);
                    if (g_sceneBatchProbe.itemSubmissionPrimitiveDispatcherVtable != 0)
                    {
                        const DWORD_PTR primitiveVtable = g_sceneBatchProbe.itemSubmissionPrimitiveDispatcherVtable;
                        g_sceneBatchProbe.itemSubmissionPrimitive78Target = reinterpret_cast<void*>(static_cast<DWORD_PTR>(
                            *reinterpret_cast<const DWORD*>(primitiveVtable + 0x78)));
                        g_sceneBatchProbe.itemSubmissionPrimitive7CTarget = reinterpret_cast<void*>(static_cast<DWORD_PTR>(
                            *reinterpret_cast<const DWORD*>(primitiveVtable + 0x7C)));
                        g_sceneBatchProbe.itemSubmissionPrimitive88Target = reinterpret_cast<void*>(static_cast<DWORD_PTR>(
                            *reinterpret_cast<const DWORD*>(primitiveVtable + 0x88)));
                    }
                }
                if (g_sceneBatchProbe.itemSubmissionTransformTarget != nullptr &&
                    g_sceneBatchProbe.itemSubmissionPrimitive78Target != nullptr &&
                    g_sceneBatchProbe.itemSubmissionPrimitive7CTarget != nullptr &&
                    g_sceneBatchProbe.itemSubmissionPrimitive88Target != nullptr)
                {
                    g_sceneBatchProbe.itemSubmissionSinkTraceArmed = ArmSceneBatchProbeSink(
                        context,
                        1,
                        g_sceneBatchProbe.itemSubmissionTransformTarget);
                }
            }
            if (!g_sceneBatchProbe.itemSubmissionSinkTraceArmed)
            {
                context.Dr3 = 0;
                context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
            }
        }
        else if (instruction == reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionSinkTarget))
        {
            switch (g_sceneBatchProbe.itemSubmissionSinkStage)
            {
            case 1:
                CaptureSceneBatchProbeSink(g_sceneBatchProbe.itemSubmissionTransformSink, context);
                InterlockedExchange(&g_sceneBatchProbe.itemSubmissionTransformObserved, 1);
                ArmSceneBatchProbeSink(context, 2, g_sceneBatchProbe.itemSubmissionPrimitive78Target);
                break;
            case 2:
                CaptureSceneBatchProbeSink(g_sceneBatchProbe.itemSubmissionPrimitive78Sink, context);
                InterlockedExchange(&g_sceneBatchProbe.itemSubmissionPrimitive78Observed, 1);
                ArmSceneBatchProbeSink(context, 3, g_sceneBatchProbe.itemSubmissionPrimitive7CTarget);
                break;
            case 3:
                CaptureSceneBatchProbeSink(g_sceneBatchProbe.itemSubmissionPrimitive7CSink, context);
                InterlockedExchange(&g_sceneBatchProbe.itemSubmissionPrimitive7CObserved, 1);
                ArmSceneBatchProbeSink(context, 4, g_sceneBatchProbe.itemSubmissionPrimitive88Target);
                break;
            case 4:
                CaptureSceneBatchProbeSink(g_sceneBatchProbe.itemSubmissionPrimitive88Sink, context);
                InterlockedExchange(&g_sceneBatchProbe.itemSubmissionPrimitive88Observed, 1);
                context.Dr3 = 0;
                context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
                break;
            default:
                return EXCEPTION_CONTINUE_SEARCH;
            }
        }
        else
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        if (!g_sceneBatchProbe.itemRouterArmed)
        {
            g_sceneBatchProbe.itemRouterArmed = TRUE;
            context.Dr3 = reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemRouterTarget);
            context.Dr7 |= 0x40;
        }
        context.Dr6 = 0;
        if (SceneBatchProbeHasAllEvents())
        {
            CompleteSceneBatchProbe(context);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_sceneBatchProbe.structuredException = TRUE;
        g_sceneBatchProbe.failureInstruction = static_cast<DWORD>(instruction);
        CompleteSceneBatchProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

bool IsSceneBatchProbeStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode0Target))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode1Target))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 && context.Dr2 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode2Target))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 &&
        context.Dr3 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemRouterTarget) &&
        context.Dr3 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemTypeQueryReturnTarget) &&
        context.Dr3 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionTarget) &&
        context.Dr3 != reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemSubmissionSinkTarget))
    {
        return false;
    }
    return true;
}

bool ArmSceneBatchProbe()
{
    if (g_createDeviceBreakpoint.threadId == 0)
    {
        return false;
    }
    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        return false;
    }
    if (g_sceneBatchProbeHandler == nullptr)
    {
        g_sceneBatchProbeHandler = AddVectoredExceptionHandler(1, HandleSceneBatchProbe);
        if (g_sceneBatchProbeHandler == nullptr)
        {
            AppendLog(L"Scene-batch probe could not install its vectored handler (%lu).", GetLastError());
            return false;
        }
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
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
    if (!GetThreadContext(thread, &context) || (context.Dr7 & 0xFF) != 0)
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    g_sceneBatchProbe = {};
    g_sceneBatchProbe.threadId = g_createDeviceBreakpoint.threadId;
    g_sceneBatchProbe.mode0Target = const_cast<std::byte*>(gameImage) + kSkinningShaderSceneBatchMode0Rva;
    g_sceneBatchProbe.mode1Target = const_cast<std::byte*>(gameImage) + kSkinningShaderSceneBatchMode1Rva;
    g_sceneBatchProbe.mode2Target = const_cast<std::byte*>(gameImage) + kSkinningShaderSceneBatchMode2Rva;
    g_sceneBatchProbe.itemRouterTarget = const_cast<std::byte*>(gameImage) + kSkinningShaderSubmittedItemRouterRva;
    g_sceneBatchProbe.itemTypeQueryReturnTarget = const_cast<std::byte*>(gameImage) + kSkinningShaderItemTypeQueryReturnRva;
    g_sceneBatchProbe.originalDr0 = context.Dr0;
    g_sceneBatchProbe.originalDr1 = context.Dr1;
    g_sceneBatchProbe.originalDr2 = context.Dr2;
    g_sceneBatchProbe.originalDr3 = context.Dr3;
    g_sceneBatchProbe.originalDr6 = context.Dr6;
    g_sceneBatchProbe.originalDr7 = context.Dr7;
    context.Dr0 = reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode0Target);
    context.Dr1 = reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode1Target);
    context.Dr2 = reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode2Target);
    context.Dr3 = 0;
    context.Dr6 = 0;
    context.Dr7 = (context.Dr7 & ~static_cast<DWORD_PTR>(0xFF)) | 0x15;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    InterlockedExchange(&g_sceneBatchProbe.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(L"Armed one read-only scene-batch probe on device thread %lu: mode 0=%08lX, mode 1=%08lX, mode 2=%08lX, item router=%08lX, type-query return=%08lX; it will follow one ordinary submitted item without invoking game or D3D code.", g_sceneBatchProbe.threadId, static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode0Target)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode1Target)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.mode2Target)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemRouterTarget)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(g_sceneBatchProbe.itemTypeQueryReturnTarget)));
    return true;
}

void RestoreSceneBatchProbe()
{
    if (InterlockedCompareExchange(&g_sceneBatchProbe.state, 0, 0) != 1)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_sceneBatchProbe.threadId);
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
    if (GetThreadContext(thread, &context) && IsSceneBatchProbeStateOwned(context))
    {
        RestoreSceneBatchProbeRegisters(context);
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

void AppendSceneBatchProbeEntry(const wchar_t* label, const SceneBatchProbeEntry& entry)
{
    AppendLog(L"Scene-batch %s entry=%08lX receiver=%08lX modeBefore=%lu resourceBc=%08lX renderView=%08lX parameter=%08lX rendererContext=%08lX return=%08lX prepass=[addr=%08lX begin=%08lX end=%08lX count=%lu valid=%d] items=[addr=%08lX begin=%08lX end=%08lX count=%lu valid=%d].", label, static_cast<unsigned long>(entry.instruction), static_cast<unsigned long>(entry.receiver), static_cast<unsigned long>(entry.renderModeBefore), static_cast<unsigned long>(entry.resourceAtBc), static_cast<unsigned long>(entry.dispatchRenderView), static_cast<unsigned long>(entry.dispatchParameter), static_cast<unsigned long>(entry.rendererContext), static_cast<unsigned long>(entry.stackReturnAddress), static_cast<unsigned long>(entry.prepassList.address), static_cast<unsigned long>(entry.prepassList.begin), static_cast<unsigned long>(entry.prepassList.end), static_cast<unsigned long>(entry.prepassList.itemCount), entry.prepassList.layoutValid, static_cast<unsigned long>(entry.itemList.address), static_cast<unsigned long>(entry.itemList.begin), static_cast<unsigned long>(entry.itemList.end), static_cast<unsigned long>(entry.itemList.itemCount), entry.itemList.layoutValid);
}

void AppendSceneBatchProbeSink(
    const wchar_t* label,
    const void* target,
    const SceneBatchProbeSinkCapture& sink)
{
    AppendLog(L"Scene-batch submission sink %s: target=%08lX entry=%08lX this=%08lX arguments=[%08lX,%08lX,%08lX] return=%08lX.", label, static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(target)), static_cast<unsigned long>(sink.instruction), static_cast<unsigned long>(sink.thisPointer), static_cast<unsigned long>(sink.arguments[0]), static_cast<unsigned long>(sink.arguments[1]), static_cast<unsigned long>(sink.arguments[2]), static_cast<unsigned long>(sink.stackReturnAddress));
}

DWORD WINAPI RunSceneBatchProbe(void*)
{
    constexpr DWORD kArmWindowMs = 120000;
    constexpr DWORD kObservationWindowMs = 30000;
    const DWORD armStartedAt = GetTickCount();
    while (GetTickCount() - armStartedAt < kArmWindowMs)
    {
        if (ArmSceneBatchProbe())
        {
            break;
        }
        Sleep(250);
    }
    if (InterlockedCompareExchange(&g_sceneBatchProbe.state, 0, 0) != 1)
    {
        AppendLog(L"Scene-batch probe did not find an idle device-thread debug-register slot within %lu ms; no game code/data was changed.", kArmWindowMs);
        return 0;
    }

    const DWORD observationStartedAt = GetTickCount();
    while (GetTickCount() - observationStartedAt < kObservationWindowMs &&
           InterlockedCompareExchange(&g_sceneBatchProbe.state, 0, 0) == 1)
    {
        Sleep(10);
    }
    RestoreSceneBatchProbe();
    if (InterlockedCompareExchange(&g_sceneBatchProbe.state, 0, 0) == 1)
    {
        InterlockedExchange(&g_sceneBatchProbe.state, 3);
    }

    const SceneBatchProbeRecord& record = g_sceneBatchProbe;
    AppendLog(L"Scene-batch probe result: state=%ld mode0=%ld mode1=%ld mode2=%ld itemRouter=%ld skippedRouter=%ld routerBoundaryComplete=%d typeQuery=%ld submission=%ld sinks=[armed=%d transform=%ld primitive78=%ld primitive7C=%ld primitive88=%ld] routerArmed=%d typeQueryArmed=%d submissionArmed=%d structuredException=%d failureEip=%08lX registersRestored=%d.", record.state, record.mode0Observed, record.mode1Observed, record.mode2Observed, record.itemRouterObserved, record.itemRouterSkipped, record.itemRouterBoundaryComplete, record.itemTypeQueryObserved, record.itemSubmissionObserved, record.itemSubmissionSinkTraceArmed, record.itemSubmissionTransformObserved, record.itemSubmissionPrimitive78Observed, record.itemSubmissionPrimitive7CObserved, record.itemSubmissionPrimitive88Observed, record.itemRouterArmed, record.itemTypeQueryReturnArmed, record.itemSubmissionArmed, record.structuredException, static_cast<unsigned long>(record.failureInstruction), record.cleanupRestored);
    AppendSceneBatchProbeEntry(L"mode0", record.mode0);
    AppendSceneBatchProbeEntry(L"mode1", record.mode1);
    AppendSceneBatchProbeEntry(L"mode2", record.mode2);
    AppendLog(L"Scene-batch item router: item=%08lX vtable=%08lX typeQuery=%08lX context=[renderView=%08lX parameter=%08lX] return=%08lX activeRenderView=%08lX rendererTransaction=%08lX.", static_cast<unsigned long>(record.item), static_cast<unsigned long>(record.itemVtable), static_cast<unsigned long>(record.itemTypeQueryTarget), static_cast<unsigned long>(record.itemContextRenderView), static_cast<unsigned long>(record.itemContextParameter), static_cast<unsigned long>(record.itemStackReturnAddress), static_cast<unsigned long>(record.activeRenderView), static_cast<unsigned long>(record.rendererTransaction));
    AppendLog(L"Scene-batch item type-query: return=%08lX type=%08lX primary=%08lX secondary=%08lX selectedSubmission=%08lX.", static_cast<unsigned long>(record.itemTypeQueryReturnAddress), static_cast<unsigned long>(record.itemTypeId), static_cast<unsigned long>(record.primaryItemTypeId), static_cast<unsigned long>(record.secondaryItemTypeId), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(record.itemSubmissionTarget)));
    AppendLog(L"Scene-batch item submission: entry=%08lX this=%08lX arguments=[%08lX,%08lX] return=%08lX.", static_cast<unsigned long>(record.itemSubmissionInstruction), static_cast<unsigned long>(record.itemSubmissionThis), static_cast<unsigned long>(record.itemSubmissionArgument0), static_cast<unsigned long>(record.itemSubmissionArgument1), static_cast<unsigned long>(record.itemSubmissionReturnAddress));
    AppendLog(L"Scene-batch primary submission setup: owner=%08lX records=[begin=%08lX end=%08lX last=%08lX] transformDispatcher=%08lX vtable=%08lX target=%08lX primitiveDispatcher=%08lX vtable=%08lX targets=[%08lX,%08lX,%08lX].", static_cast<unsigned long>(record.itemSubmissionOwner), static_cast<unsigned long>(record.itemSubmissionBatchBegin), static_cast<unsigned long>(record.itemSubmissionBatchEnd), static_cast<unsigned long>(record.itemSubmissionLastBatchRecord), static_cast<unsigned long>(record.itemSubmissionTransformDispatcher), static_cast<unsigned long>(record.itemSubmissionTransformDispatcherVtable), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(record.itemSubmissionTransformTarget)), static_cast<unsigned long>(record.itemSubmissionPrimitiveDispatcher), static_cast<unsigned long>(record.itemSubmissionPrimitiveDispatcherVtable), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(record.itemSubmissionPrimitive78Target)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(record.itemSubmissionPrimitive7CTarget)), static_cast<unsigned long>(reinterpret_cast<DWORD_PTR>(record.itemSubmissionPrimitive88Target)));
    AppendSceneBatchProbeSink(L"transform", record.itemSubmissionTransformTarget, record.itemSubmissionTransformSink);
    AppendSceneBatchProbeSink(L"primitive+0x78", record.itemSubmissionPrimitive78Target, record.itemSubmissionPrimitive78Sink);
    AppendSceneBatchProbeSink(L"primitive+0x7C", record.itemSubmissionPrimitive7CTarget, record.itemSubmissionPrimitive7CSink);
    AppendSceneBatchProbeSink(L"primitive+0x88", record.itemSubmissionPrimitive88Target, record.itemSubmissionPrimitive88Sink);
    return 0;
}

void StartSceneBatchProbe()
{
    if (InterlockedCompareExchange(&g_sceneBatchProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_sceneBatchProbeStarted, 1, 0) != 0)
    {
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunSceneBatchProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Scene-batch probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the one-shot scene-batch probe; it waits for the ordinary device thread, copies only batch/item/normal-submission metadata, and restores the complete prior debug-register context.");
}

// The configured-view vector is only read by the ordinary render functions
// recovered so far. This probe begins while the loader still holds the primary
// game thread suspended. An execution breakpoint immediately after
// FUN_004568B0 publishes DAT_00971EAC transitions in-place to paired four-byte
// write breakpoints on owner+0x22C and owner+0x168. This reaches the startup
// append that had already happened by the old CreateDevice-time arm point and
// then observes the builder's active-index update without invoking game code.
constexpr DWORD_PTR kConfiguredViewListWriterProbeExecuteDr7 = 0x00000001;
constexpr DWORD_PTR kConfiguredViewListWriterProbeWriteDr7 = 0x000D0001;
constexpr DWORD_PTR kConfiguredViewListWriterProbeActiveIndexWriteDr7 = 0x00D00004;
constexpr DWORD_PTR kConfiguredViewListWriterProbeDualWriteDr7 = 0x00DD0005;
constexpr std::byte kConfiguredViewManagerAssignmentPrefix[] = {
    std::byte{0x89}, std::byte{0x35}, std::byte{0xAC},
    std::byte{0x1E}, std::byte{0x97}, std::byte{0x00}
};

struct ConfiguredViewListWriterProbeRecord
{
    volatile LONG state = 0;
    volatile LONG stage = 0;
    DWORD threadId = 0;
    DWORD ownerAssignmentTarget = 0;
    DWORD configuredViewManager = 0;
    DWORD watchedEndAddress = 0;
    DWORD watchedActiveIndexAddress = 0;
    DWORD listBeginBefore = 0;
    DWORD listEndBefore = 0;
    DWORD listCapacityBefore = 0;
    DWORD listBeginAtEndWrite = 0;
    DWORD listEndAtEndWrite = 0;
    DWORD listCapacityAtEndWrite = 0;
    DWORD listBeginAfter = 0;
    DWORD listEndAfter = 0;
    DWORD listCapacityAfter = 0;
    DWORD lastHandleAfter = 0;
    DWORD writerInstructionAfter = 0;
    DWORD activeIndexBefore = 0;
    DWORD activeIndexAfter = 0;
    DWORD activeIndexWriterInstructionAfter = 0;
    DWORD writerEax = 0;
    DWORD writerEbx = 0;
    DWORD writerEcx = 0;
    DWORD writerEdx = 0;
    DWORD writerEsi = 0;
    DWORD writerEdi = 0;
    DWORD writerEbp = 0;
    DWORD writerEsp = 0;
    DWORD writerStackWords[24] = {};
    DWORD registry = 0;
    DWORD registryVtable = 0;
    DWORD registryRegisterTarget = 0;
    DWORD registryRollbackTarget = 0;
    DWORD registryResolveTarget = 0;
    DWORD registryDestroyTarget = 0;
    DWORD_PTR originalDr0 = 0;
    DWORD_PTR originalDr1 = 0;
    DWORD_PTR originalDr2 = 0;
    DWORD_PTR originalDr3 = 0;
    DWORD_PTR originalDr6 = 0;
    DWORD_PTR originalDr7 = 0;
    BOOL ownerAssignmentObserved = FALSE;
    BOOL afterStateReadable = FALSE;
    BOOL writerStackReadable = FALSE;
    BOOL activeIndexMatchesAppendedSlot = FALSE;
    BOOL cleanupRestored = FALSE;
    BOOL structuredException = FALSE;
};

ConfiguredViewListWriterProbeRecord g_configuredViewListWriterProbe = {};
PVOID g_configuredViewListWriterProbeHandler = nullptr;

void RestoreConfiguredViewListWriterProbeRegisters(CONTEXT& context)
{
    context.Dr0 = g_configuredViewListWriterProbe.originalDr0;
    context.Dr1 = g_configuredViewListWriterProbe.originalDr1;
    context.Dr2 = g_configuredViewListWriterProbe.originalDr2;
    context.Dr3 = g_configuredViewListWriterProbe.originalDr3;
    context.Dr6 = g_configuredViewListWriterProbe.originalDr6;
    context.Dr7 = g_configuredViewListWriterProbe.originalDr7;
    g_configuredViewListWriterProbe.cleanupRestored = TRUE;
}

void CompleteConfiguredViewListWriterProbe(CONTEXT& context)
{
    RestoreConfiguredViewListWriterProbeRegisters(context);
    InterlockedExchange(&g_configuredViewListWriterProbe.state, 2);
}

LONG CALLBACK HandleConfiguredViewListWriterProbe(EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr || exceptionPointers->ContextRecord == nullptr ||
        exceptionPointers->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP ||
        InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) != 1 ||
        GetCurrentThreadId() != g_configuredViewListWriterProbe.threadId)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT& context = *exceptionPointers->ContextRecord;
    __try
    {
        ConfiguredViewListWriterProbeRecord& record = g_configuredViewListWriterProbe;
        const LONG stage = InterlockedCompareExchange(&record.stage, 0, 0);
        if (stage == 1)
        {
            if ((context.Dr6 & 0x1) == 0 ||
                static_cast<DWORD>(context.Eip) != record.ownerAssignmentTarget)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            if (gameImage == nullptr)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }
            record.configuredViewManager =
                *reinterpret_cast<const DWORD*>(gameImage + kConfiguredViewManagerGlobalRva);
            if (record.configuredViewManager == 0)
            {
                RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
            }

            const DWORD manager = record.configuredViewManager;
            record.watchedEndAddress = manager + kConfiguredViewListEndOffset;
            record.watchedActiveIndexAddress = manager + kConfiguredViewActiveIndexOffset;
            record.activeIndexBefore = *reinterpret_cast<const DWORD*>(
                static_cast<DWORD_PTR>(record.watchedActiveIndexAddress));
            record.listBeginBefore = *reinterpret_cast<const DWORD*>(
                static_cast<DWORD_PTR>(manager + kConfiguredViewListBeginOffset));
            record.listEndBefore = *reinterpret_cast<const DWORD*>(
                static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset));
            record.listCapacityBefore = *reinterpret_cast<const DWORD*>(
                static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset + sizeof(DWORD)));
            record.ownerAssignmentObserved = TRUE;

            context.Dr0 = record.watchedEndAddress;
            context.Dr1 = record.watchedActiveIndexAddress;
            context.Dr6 = 0;
            context.Dr7 =
                (context.Dr7 & ~static_cast<DWORD_PTR>(0x00FF000F)) |
                kConfiguredViewListWriterProbeDualWriteDr7;
            InterlockedExchange(&record.stage, 2);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (stage == 2)
        {
            if ((context.Dr6 & 0x1) == 0)
            {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            record.writerInstructionAfter = static_cast<DWORD>(context.Eip);
            record.writerEax = static_cast<DWORD>(context.Eax);
            record.writerEbx = static_cast<DWORD>(context.Ebx);
            record.writerEcx = static_cast<DWORD>(context.Ecx);
            record.writerEdx = static_cast<DWORD>(context.Edx);
            record.writerEsi = static_cast<DWORD>(context.Esi);
            record.writerEdi = static_cast<DWORD>(context.Edi);
            record.writerEbp = static_cast<DWORD>(context.Ebp);
            record.writerEsp = static_cast<DWORD>(context.Esp);
            const auto* const writerStack = reinterpret_cast<const DWORD*>(context.Esp);
            for (std::size_t wordIndex = 0; wordIndex < std::size(record.writerStackWords); ++wordIndex)
            {
                record.writerStackWords[wordIndex] = writerStack[wordIndex];
            }
            record.writerStackReadable = TRUE;
            const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            record.registry = *reinterpret_cast<const DWORD*>(gameImage + kConfiguredViewRegistryGlobalRva);
            if (record.registry != 0)
            {
                record.registryVtable =
                    *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(record.registry));
                if (record.registryVtable != 0)
                {
                    record.registryRegisterTarget = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(record.registryVtable + 0x1C));
                    record.registryRollbackTarget = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(record.registryVtable + 0x20));
                    record.registryResolveTarget = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(record.registryVtable + 0x24));
                    record.registryDestroyTarget = *reinterpret_cast<const DWORD*>(
                        static_cast<DWORD_PTR>(record.registryVtable + 0x54));
                }
            }
            const DWORD manager = record.configuredViewManager;
            record.listBeginAtEndWrite = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListBeginOffset));
            record.listEndAtEndWrite = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset));
            record.listCapacityAtEndWrite = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset + sizeof(DWORD)));

            context.Dr0 = 0;
            context.Dr6 = 0;
            context.Dr7 =
                (context.Dr7 & ~static_cast<DWORD_PTR>(0x000F0003)) |
                kConfiguredViewListWriterProbeActiveIndexWriteDr7;
            InterlockedExchange(&record.stage, 3);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (stage != 3 || (context.Dr6 & 0x2) == 0)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        record.activeIndexWriterInstructionAfter = static_cast<DWORD>(context.Eip);
        record.activeIndexAfter = *reinterpret_cast<const DWORD*>(
            static_cast<DWORD_PTR>(record.watchedActiveIndexAddress));
        const DWORD manager = record.configuredViewManager;
        record.listBeginAfter = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListBeginOffset));
        record.listEndAfter = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset));
        record.listCapacityAfter = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(manager + kConfiguredViewListEndOffset + sizeof(DWORD)));
        if (record.listEndAfter >= record.listBeginAfter &&
            (record.listEndAfter - record.listBeginAfter) >= sizeof(DWORD) &&
            (record.listEndAfter - record.listBeginAfter) % sizeof(DWORD) == 0)
        {
            record.lastHandleAfter = *reinterpret_cast<const DWORD*>(static_cast<DWORD_PTR>(record.listEndAfter - sizeof(DWORD)));
            const DWORD appendedSlot =
                (record.listEndAfter - record.listBeginAfter) / sizeof(DWORD) - 1;
            record.activeIndexMatchesAppendedSlot = record.activeIndexAfter == appendedSlot;
        }
        record.afterStateReadable = TRUE;
        CompleteConfiguredViewListWriterProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_configuredViewListWriterProbe.structuredException = TRUE;
        CompleteConfiguredViewListWriterProbe(context);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}

bool IsConfiguredViewListWriterProbeStateOwned(const CONTEXT& context)
{
    if (InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) != 1)
    {
        return false;
    }

    const LONG stage = InterlockedCompareExchange(&g_configuredViewListWriterProbe.stage, 0, 0);
    if (stage == 1)
    {
        return context.Dr0 == g_configuredViewListWriterProbe.ownerAssignmentTarget &&
            (context.Dr7 & 0x000F0003) == kConfiguredViewListWriterProbeExecuteDr7;
    }
    if (stage == 2)
    {
        return context.Dr0 == g_configuredViewListWriterProbe.watchedEndAddress &&
            context.Dr1 == g_configuredViewListWriterProbe.watchedActiveIndexAddress &&
            (context.Dr7 & 0x00FF000F) == kConfiguredViewListWriterProbeDualWriteDr7;
    }
    return stage == 3 &&
        context.Dr1 == g_configuredViewListWriterProbe.watchedActiveIndexAddress &&
        (context.Dr7 & 0x00F0000C) == kConfiguredViewListWriterProbeActiveIndexWriteDr7;
}

bool ArmConfiguredViewListWriterProbe()
{
    if (g_loaderPrimaryThreadId == 0)
    {
        AppendLog(L"Configured-view list writer probe could not arm: the loader did not supply the suspended primary-thread id.");
        return false;
    }

    const auto* const gameImage = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (gameImage == nullptr)
    {
        return false;
    }
    if (g_configuredViewListWriterProbeHandler == nullptr)
    {
        g_configuredViewListWriterProbeHandler = AddVectoredExceptionHandler(1, HandleConfiguredViewListWriterProbe);
        if (g_configuredViewListWriterProbeHandler == nullptr)
        {
            AppendLog(L"Configured-view list writer probe could not install its vectored handler (%lu).", GetLastError());
            return false;
        }
    }

    const auto* const ownerAssignmentTarget = gameImage + kConfiguredViewManagerAssignedRva;
    if (std::memcmp(
            ownerAssignmentTarget - std::size(kConfiguredViewManagerAssignmentPrefix),
            kConfiguredViewManagerAssignmentPrefix,
            sizeof(kConfiguredViewManagerAssignmentPrefix)) != 0)
    {
        AppendLog(
            L"Configured-view list writer probe could not arm: profiled owner-assignment target=%p failed its instruction-prefix check.",
            ownerAssignmentTarget);
        return false;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_loaderPrimaryThreadId);
    if (thread == nullptr)
    {
        AppendLog(
            L"Configured-view list writer probe could not open suspended primary thread %lu (%lu).",
            g_loaderPrimaryThreadId,
            GetLastError());
        return false;
    }
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
    {
        AppendLog(
            L"Configured-view list writer probe could not add its temporary suspend to primary thread %lu (%lu).",
            g_loaderPrimaryThreadId,
            GetLastError());
        CloseHandle(thread);
        return false;
    }

    CONTEXT context = {};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &context) || (context.Dr7 & 0xFF) != 0)
    {
        AppendLog(
            L"Configured-view list writer probe preserved a non-idle or unreadable primary-thread debug-register context (thread=%lu DR7=%p error=%lu).",
            g_loaderPrimaryThreadId,
            reinterpret_cast<void*>(context.Dr7),
            GetLastError());
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    g_configuredViewListWriterProbe = {};
    g_configuredViewListWriterProbe.threadId = g_loaderPrimaryThreadId;
    g_configuredViewListWriterProbe.ownerAssignmentTarget =
        reinterpret_cast<DWORD_PTR>(ownerAssignmentTarget);
    g_configuredViewListWriterProbe.originalDr0 = context.Dr0;
    g_configuredViewListWriterProbe.originalDr1 = context.Dr1;
    g_configuredViewListWriterProbe.originalDr2 = context.Dr2;
    g_configuredViewListWriterProbe.originalDr3 = context.Dr3;
    g_configuredViewListWriterProbe.originalDr6 = context.Dr6;
    g_configuredViewListWriterProbe.originalDr7 = context.Dr7;

    context.Dr0 = g_configuredViewListWriterProbe.ownerAssignmentTarget;
    context.Dr6 = 0;
    context.Dr7 =
        (context.Dr7 & ~static_cast<DWORD_PTR>(0x000F0003)) |
        kConfiguredViewListWriterProbeExecuteDr7;
    if (!SetThreadContext(thread, &context))
    {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    InterlockedExchange(&g_configuredViewListWriterProbe.stage, 1);
    InterlockedExchange(&g_configuredViewListWriterProbe.state, 1);
    ResumeThread(thread);
    CloseHandle(thread);
    AppendLog(
        L"Armed the read-only configured-view startup writer probe at owner-assignment target=%08lX on loader-supplied suspended primary thread %lu. At that exact boundary it will use DR0/DR1 four-byte write watches on owner+0x22C and owner+0x168, capture the first native append plus active-index writer, and restore the complete prior debug-register state.",
        static_cast<unsigned long>(g_configuredViewListWriterProbe.ownerAssignmentTarget),
        g_configuredViewListWriterProbe.threadId);
    return true;
}

void RestoreConfiguredViewListWriterProbe()
{
    if (InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) != 1)
    {
        return;
    }

    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_configuredViewListWriterProbe.threadId);
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
    if (GetThreadContext(thread, &context) && IsConfiguredViewListWriterProbeStateOwned(context))
    {
        RestoreConfiguredViewListWriterProbeRegisters(context);
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

DWORD WINAPI RunConfiguredViewListWriterProbe(void*)
{
    constexpr DWORD kObservationWindowMs = 120000;
    if (InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) != 1)
    {
        return 0;
    }

    const DWORD observationStartedAt = GetTickCount();
    while (GetTickCount() - observationStartedAt < kObservationWindowMs &&
           InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) == 1)
    {
        Sleep(10);
    }
    RestoreConfiguredViewListWriterProbe();
    if (InterlockedCompareExchange(&g_configuredViewListWriterProbe.state, 0, 0) == 1)
    {
        InterlockedExchange(&g_configuredViewListWriterProbe.state, 3);
    }

    const ConfiguredViewListWriterProbeRecord& record = g_configuredViewListWriterProbe;
    AppendLog(
        L"Configured-view list writer probe result: state=%ld stage=%ld primaryThread=%lu ownerAssignmentTarget=%08lX ownerAssignmentObserved=%d manager=%08lX endAddress=%08lX activeIndexAddress=%08lX activeIndex=[%08lX,%08lX] activeIndexWriterEipAfter=%08lX activeIndexMatchesAppendedSlot=%d before=[%08lX,%08lX,%08lX] atEndWrite=[%08lX,%08lX,%08lX] afterIndex=[%08lX,%08lX,%08lX] lastHandle=%08lX writerEipAfter=%08lX regs=[eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX] registry=%08lX vtable=%08lX methods=[register=%08lX rollback=%08lX resolve=%08lX destroy=%08lX] stackReadable=%d afterReadable=%d structuredException=%d registersRestored=%d.",
        record.state,
        record.stage,
        record.threadId,
        static_cast<unsigned long>(record.ownerAssignmentTarget),
        record.ownerAssignmentObserved,
        static_cast<unsigned long>(record.configuredViewManager),
        static_cast<unsigned long>(record.watchedEndAddress),
        static_cast<unsigned long>(record.watchedActiveIndexAddress),
        static_cast<unsigned long>(record.activeIndexBefore),
        static_cast<unsigned long>(record.activeIndexAfter),
        static_cast<unsigned long>(record.activeIndexWriterInstructionAfter),
        record.activeIndexMatchesAppendedSlot,
        static_cast<unsigned long>(record.listBeginBefore),
        static_cast<unsigned long>(record.listEndBefore),
        static_cast<unsigned long>(record.listCapacityBefore),
        static_cast<unsigned long>(record.listBeginAtEndWrite),
        static_cast<unsigned long>(record.listEndAtEndWrite),
        static_cast<unsigned long>(record.listCapacityAtEndWrite),
        static_cast<unsigned long>(record.listBeginAfter),
        static_cast<unsigned long>(record.listEndAfter),
        static_cast<unsigned long>(record.listCapacityAfter),
        static_cast<unsigned long>(record.lastHandleAfter),
        static_cast<unsigned long>(record.writerInstructionAfter),
        static_cast<unsigned long>(record.writerEax),
        static_cast<unsigned long>(record.writerEbx),
        static_cast<unsigned long>(record.writerEcx),
        static_cast<unsigned long>(record.writerEdx),
        static_cast<unsigned long>(record.writerEsi),
        static_cast<unsigned long>(record.writerEdi),
        static_cast<unsigned long>(record.writerEbp),
        static_cast<unsigned long>(record.writerEsp),
        static_cast<unsigned long>(record.registry),
        static_cast<unsigned long>(record.registryVtable),
        static_cast<unsigned long>(record.registryRegisterTarget),
        static_cast<unsigned long>(record.registryRollbackTarget),
        static_cast<unsigned long>(record.registryResolveTarget),
        static_cast<unsigned long>(record.registryDestroyTarget),
        record.writerStackReadable,
        record.afterStateReadable,
        record.structuredException,
        record.cleanupRestored);
    AppendLog(
        L"Configured-view list writer stack: [%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX].",
        static_cast<unsigned long>(record.writerStackWords[0]),
        static_cast<unsigned long>(record.writerStackWords[1]),
        static_cast<unsigned long>(record.writerStackWords[2]),
        static_cast<unsigned long>(record.writerStackWords[3]),
        static_cast<unsigned long>(record.writerStackWords[4]),
        static_cast<unsigned long>(record.writerStackWords[5]),
        static_cast<unsigned long>(record.writerStackWords[6]),
        static_cast<unsigned long>(record.writerStackWords[7]),
        static_cast<unsigned long>(record.writerStackWords[8]),
        static_cast<unsigned long>(record.writerStackWords[9]),
        static_cast<unsigned long>(record.writerStackWords[10]),
        static_cast<unsigned long>(record.writerStackWords[11]),
        static_cast<unsigned long>(record.writerStackWords[12]),
        static_cast<unsigned long>(record.writerStackWords[13]),
        static_cast<unsigned long>(record.writerStackWords[14]),
        static_cast<unsigned long>(record.writerStackWords[15]),
        static_cast<unsigned long>(record.writerStackWords[16]),
        static_cast<unsigned long>(record.writerStackWords[17]),
        static_cast<unsigned long>(record.writerStackWords[18]),
        static_cast<unsigned long>(record.writerStackWords[19]),
        static_cast<unsigned long>(record.writerStackWords[20]),
        static_cast<unsigned long>(record.writerStackWords[21]),
        static_cast<unsigned long>(record.writerStackWords[22]),
        static_cast<unsigned long>(record.writerStackWords[23]));
    return 0;
}

void StartConfiguredViewListWriterProbe()
{
    if (InterlockedCompareExchange(&g_configuredViewListWriterProbeRequested, 0, 0) == 0 ||
        InterlockedCompareExchange(&g_configuredViewListWriterProbeStarted, 1, 0) != 0)
    {
        return;
    }

    if (!ArmConfiguredViewListWriterProbe())
    {
        AppendLog(L"Configured-view list startup writer probe was not armed; no game code/data was changed.");
        return;
    }

    HANDLE worker = CreateThread(nullptr, 0, RunConfiguredViewListWriterProbe, nullptr, 0, nullptr);
    if (worker == nullptr)
    {
        AppendLog(L"Configured-view list writer probe could not start (%lu).", GetLastError());
        return;
    }
    CloseHandle(worker);
    AppendLog(L"Requested the one-shot configured-view startup writer probe; it is armed before the loader resumes BF1942's primary thread and will only observe the owner publication plus the first native list-end write.");
}

void DisableCreateDeviceTraceForCombinedFrameTrace()
{
    HANDLE thread = OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
        FALSE,
        g_createDeviceBreakpoint.threadId);
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
    if (GetThreadContext(thread, &context))
    {
        if (context.Dr0 == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.presentTarget) && (context.Dr7 & 0x1) != 0)
        {
            context.Dr0 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x1);
        }
        if (context.Dr1 == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setRenderTargetTarget) && (context.Dr7 & 0x4) != 0)
        {
            context.Dr1 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x4);
        }
        if (context.Dr2 == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.setTransformTarget) && (context.Dr7 & 0x10) != 0)
        {
            context.Dr2 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x10);
        }
        if (context.Dr3 == reinterpret_cast<DWORD_PTR>(g_createDeviceBreakpoint.beginSceneTarget) && (context.Dr7 & 0x40) != 0)
        {
            context.Dr3 = 0;
            context.Dr7 &= ~static_cast<DWORD_PTR>(0x40);
        }
        context.Dr6 = 0;
        SetThreadContext(thread, &context);
    }
    ResumeThread(thread);
    CloseHandle(thread);
    InterlockedExchange(&g_createDeviceBreakpoint.stage, 6);
}

bool IsCombinedFrameTraceStateOwned(const CONTEXT& context)
{
    if ((context.Dr7 & 0xAA) != 0)
    {
        return false;
    }
    if ((context.Dr7 & 0x1) != 0 && context.Dr0 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.presentTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x4) != 0 && context.Dr1 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setTransformTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x10) != 0 &&
        context.Dr2 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.beginSceneTarget) &&
        context.Dr2 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.endSceneTarget) &&
        context.Dr2 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.clearTarget))
    {
        return false;
    }
    if ((context.Dr7 & 0x40) != 0 && context.Dr3 != reinterpret_cast<DWORD_PTR>(g_combinedFrameTrace.setRenderTargetTarget))
    {
        return false;
    }
    return true;
}

