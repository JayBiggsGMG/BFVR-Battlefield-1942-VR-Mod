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

bool CaptureKnownWrapperCaller(
    FrameDrawInvocation& invocation,
    void** returnAddressSlot,
    std::uintptr_t expectedReturnAddress,
    std::size_t callerStackIndex)
{
    void* wrapper = nullptr;
    void* caller = nullptr;
    void* treeMeshOuterReturn = nullptr;
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
