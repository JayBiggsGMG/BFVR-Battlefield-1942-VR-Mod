// Included inside D3D8StereoPairProbe.cpp's anonymous namespace after the
// draw-invocation type and game-image range have been defined.

bool AppendGameStackAddress(
    FrameDrawInvocation& invocation,
    void* candidate)
{
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(candidate);
    if (address < g_gameImageBegin || address >= g_gameImageEnd)
    {
        return false;
    }
    for (UINT index = 0; index < invocation.gameStackDepth; ++index)
    {
        if (invocation.gameStack[index] == candidate)
        {
            return true;
        }
    }
    if (invocation.gameStackDepth >= kProvenanceStackDepth)
    {
        return false;
    }
    invocation.gameStack[invocation.gameStackDepth++] = candidate;
    return true;
}

void CaptureGameStack(FrameDrawInvocation& invocation)
{
    void* capturedFrames[16] = {};
    const USHORT captured = RtlCaptureStackBackTrace(
        0,
        static_cast<ULONG>(std::size(capturedFrames)),
        capturedFrames,
        nullptr);
    for (USHORT index = 0;
         index < captured &&
         invocation.gameStackDepth < kProvenanceStackDepth;
         ++index)
    {
        AppendGameStackAddress(invocation, capturedFrames[index]);
    }
}

bool IsKnownTreeMeshOuterReturn(std::uintptr_t address)
{
    switch (address)
    {
    case 0x0067D690:
    case 0x0067D724:
    case 0x0067DA82:
    case 0x0067DCEB:
    case 0x0067DD04:
    case 0x0067DDB3:
    case 0x0067E1FD:
        return true;
    default:
        return false;
    }
}

constexpr std::uintptr_t kPatchCellBlockDrawReturn = 0x0069922E;

bool CaptureKnownWrapperCaller(
    FrameDrawInvocation& invocation,
    void** returnAddressSlot,
    std::uintptr_t expectedReturnAddress,
    std::size_t callerStackIndex)
{
    void* wrapper = nullptr;
    void* caller = nullptr;
    void* treeMeshOuterReturn = nullptr;
    void* patchCellOuterReturn = nullptr;
    void* patchCellPrimitiveType = nullptr;
    void* patchCellStartIndex = nullptr;
    void* patchCellPrimitiveCount = nullptr;
    void* patchCellSavedEsi = nullptr;
    __try
    {
        wrapper = returnAddressSlot == nullptr
            ? nullptr
            : returnAddressSlot[0];
        if (reinterpret_cast<std::uintptr_t>(wrapper) !=
            expectedReturnAddress)
        {
            return false;
        }
        caller = returnAddressSlot[callerStackIndex];
        if (callerStackIndex == 10 &&
            reinterpret_cast<std::uintptr_t>(caller) ==
                kTreeMeshDrawBlocksReturn)
        {
            treeMeshOuterReturn = returnAddressSlot[25];
        }
        if (callerStackIndex == 10 &&
            reinterpret_cast<std::uintptr_t>(caller) ==
                kPatchCellBlockDrawReturn)
        {
            // The Renderer::drawIndexedPrimitive wrapper returns with `ret
            // 0x0c`, so its three caller-owned arguments occupy slots 11-13
            // after the wrapper return at slot 10. PatchCellBlock::draw then
            // contributes its saved ESI at slot 14 and its outer return at
            // slot 15. The vector and linked-list shadow traversals return at
            // 0x00682E95 and 0x00683ADD respectively; ordinary users of the
            // shared PatchCellBlock submission have different outer callers.
            patchCellPrimitiveType = returnAddressSlot[11];
            patchCellStartIndex = returnAddressSlot[12];
            patchCellPrimitiveCount = returnAddressSlot[13];
            patchCellSavedEsi = returnAddressSlot[14];
            patchCellOuterReturn = returnAddressSlot[15];
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!AppendGameStackAddress(invocation, wrapper) ||
        !AppendGameStackAddress(invocation, caller))
    {
        return false;
    }
    if (IsKnownTreeMeshOuterReturn(
            reinterpret_cast<std::uintptr_t>(treeMeshOuterReturn)))
    {
        AppendGameStackAddress(invocation, treeMeshOuterReturn);
    }
    if (patchCellOuterReturn != nullptr &&
        bfvr::IsD3D8RuntimeDiagnosticsEnabled(g_runtimeDiagnostics) &&
        (InterlockedOr(&g_projectedShadowAuditMask, 0x40) & 0x40) == 0)
    {
        AppendLog(
            L"PROJECTED_SHADOW_AUDIT patchCellStack slot11PrimitiveType=0x%08lX slot12StartIndex=0x%08lX slot13PrimitiveCount=0x%08lX slot14SavedEsi=0x%08lX slot15OuterReturn=0x%08lX.",
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(patchCellPrimitiveType)),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(patchCellStartIndex)),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(patchCellPrimitiveCount)),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(patchCellSavedEsi)),
            static_cast<unsigned long>(
                reinterpret_cast<std::uintptr_t>(patchCellOuterReturn)));
    }
    if (patchCellOuterReturn != nullptr)
    {
        // Retain the actual outer caller for the two proven static shadow
        // paths and for state-gated fallback discovery. The append helper
        // accepts only addresses inside the profiled game image.
        AppendGameStackAddress(invocation, patchCellOuterReturn);
    }
    return true;
}

void CaptureDrawStack(
    FrameDrawInvocation& invocation,
    void** returnAddressSlot,
    std::uintptr_t expectedReturnAddress,
    std::size_t callerStackIndex,
    bool recoverNestedMenuProducer = false)
{
    if (!g_runUntilStopped)
    {
        CaptureGameStack(invocation);
    }
    const bool exact = CaptureKnownWrapperCaller(
        invocation,
        returnAddressSlot,
        expectedReturnAddress,
        callerStackIndex);
    if (g_runUntilStopped &&
        exact &&
        recoverNestedMenuProducer &&
        invocation.gameStackDepth >= 2 &&
        reinterpret_cast<std::uintptr_t>(invocation.gameStack[1]) ==
            kRendererDrawPrimitiveUpReturn)
    {
        CaptureGameStack(invocation);
    }
}
