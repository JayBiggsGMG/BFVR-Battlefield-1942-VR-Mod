// Included inside D3D8StereoPairProbe.cpp after AppendLog is defined.

void LogWaterPassState(
    void* device,
    const DrawStateSnapshot& snapshot)
{
    if (g_methods.getRenderState == nullptr ||
        g_methods.getTextureStageState == nullptr)
    {
        return;
    }

    const LONG passBit = snapshot.zWriteEnable != 0 ? 0x2 : 0x1;
    if ((InterlockedOr(&g_loggedWaterPassStateMask, passBit) & passBit) != 0)
    {
        return;
    }

    DWORD sourceBlend = 0;
    DWORD destinationBlend = 0;
    DWORD textureFactor = 0;
    const HRESULT sourceBlendResult = g_methods.getRenderState(
        device,
        kD3DRenderStateSourceBlend,
        &sourceBlend);
    const HRESULT destinationBlendResult = g_methods.getRenderState(
        device,
        kD3DRenderStateDestinationBlend,
        &destinationBlend);
    const HRESULT textureFactorResult = g_methods.getRenderState(
        device,
        kD3DRenderStateTextureFactor,
        &textureFactor);

    constexpr std::array<DWORD, 8> kWaterStageStates = {
        1,  // D3DTSS_COLOROP
        2,  // D3DTSS_COLORARG1
        3,  // D3DTSS_COLORARG2
        4,  // D3DTSS_ALPHAOP
        5,  // D3DTSS_ALPHAARG1
        6,  // D3DTSS_ALPHAARG2
        11, // D3DTSS_TEXCOORDINDEX
        24  // D3DTSS_TEXTURETRANSFORMFLAGS
    };
    std::array<std::array<DWORD, kWaterStageStates.size()>, 2> stages = {};
    DWORD successfulStageReads = 0;
    for (DWORD stage = 0; stage < stages.size(); ++stage)
    {
        for (std::size_t state = 0; state < kWaterStageStates.size(); ++state)
        {
            if (SUCCEEDED(g_methods.getTextureStageState(
                    device,
                    stage,
                    kWaterStageStates[state],
                    &stages[stage][state])))
            {
                successfulStageReads |= 1U <<
                    (stage * kWaterStageStates.size() + state);
            }
        }
    }

    AppendLog(
        L"D3D8 water pass diagnostic: zWrite=%lu blend[src=%lu(%08lX) dst=%lu(%08lX)] textureFactor=%08lX(%08lX) stageReads=%04lX stage0[colorOp=%lu arg1=%lu arg2=%lu alphaOp=%lu arg1=%lu arg2=%lu coord=%08lX transform=%lu] stage1[colorOp=%lu arg1=%lu arg2=%lu alphaOp=%lu arg1=%lu arg2=%lu coord=%08lX transform=%lu].",
        snapshot.zWriteEnable,
        sourceBlend,
        static_cast<unsigned long>(sourceBlendResult),
        destinationBlend,
        static_cast<unsigned long>(destinationBlendResult),
        textureFactor,
        static_cast<unsigned long>(textureFactorResult),
        successfulStageReads,
        stages[0][0],
        stages[0][1],
        stages[0][2],
        stages[0][3],
        stages[0][4],
        stages[0][5],
        stages[0][6],
        stages[0][7],
        stages[1][0],
        stages[1][1],
        stages[1][2],
        stages[1][3],
        stages[1][4],
        stages[1][5],
        stages[1][6],
        stages[1][7]);
}
