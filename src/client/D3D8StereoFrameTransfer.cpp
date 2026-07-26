#include "client/D3D8StereoFrameTransfer.h"

#include "client/D3D8StereoProbeReporting.h"

#include <algorithm>
#include <array>

namespace bfvr::d3d8probe
{

bool TransferStereoFrameToSharedPresentation(
    const D3D8StereoReadbackApi& readbackApi,
    void* device,
    StereoFrameRecord& frame,
    DWORD worldClearColor,
    DWORD uiClearColor,
    D3D8SharedPresentationBridge& presentationBridge,
    const D3D8RuntimeRenderRequest& renderRequest,
    bool analyzePixels)
{
    if (presentationBridge.UsesGpuSharedTargets())
    {
        frame.readbackQpcTicks = 0;
        const std::int64_t publishStarted = ReadPerformanceCounter();
        const bool published = presentationBridge.PublishGpuFrame(
            device,
            renderRequest,
            2000);
        frame.uploadQpcTicks =
            ReadPerformanceCounter() - publishStarted;
        return published;
    }

    const std::array<void*, 3> sources = {
        frame.ownedColor[0],
        frame.ownedColor[1],
        frame.menuColor};
    const std::array<D3DSurfaceDescription, 3> descriptions = {
        frame.colorDescription,
        frame.colorDescription,
        frame.menuColorDescription};
    const std::array<DWORD, 3> clearColors = {
        worldClearColor,
        worldClearColor,
        uiClearColor};
    std::array<ReadbackResult*, 3> results = {
        &frame.readback[0],
        &frame.readback[1],
        &frame.menuReadback};
    std::array<D3D8LockedReadback, 3> locked = {};

    const std::int64_t readbackStarted = ReadPerformanceCounter();
    for (std::size_t index = 0; index < locked.size(); ++index)
    {
        *results[index] = BeginReusableReadback(
            readbackApi,
            device,
            sources[index],
            descriptions[index],
            clearColors[index],
            frame.reusableReadback[index],
            frame.reusableReadbackDescription[index],
            locked[index],
            analyzePixels);
    }
    frame.readbackQpcTicks =
        ReadPerformanceCounter() - readbackStarted;

    const bool allLocked = std::all_of(
        locked.begin(),
        locked.end(),
        [](const D3D8LockedReadback& readback)
        {
            return readback.surface != nullptr &&
                readback.data != nullptr &&
                readback.rowPitch != 0;
        });

    bool published = false;
    if (allLocked)
    {
        std::array<D3D8SharedFramePixels, 3> sharedFrame = {};
        for (std::size_t index = 0; index < sharedFrame.size(); ++index)
        {
            sharedFrame[index] = {
                locked[index].data,
                locked[index].rowPitch,
                locked[index].width,
                locked[index].height};
        }
        const std::int64_t uploadStarted = ReadPerformanceCounter();
        published =
            presentationBridge.PublishFrame(renderRequest, sharedFrame);
        frame.uploadQpcTicks =
            ReadPerformanceCounter() - uploadStarted;
    }

    const std::int64_t unlockStarted = ReadPerformanceCounter();
    for (std::size_t index = 0; index < locked.size(); ++index)
    {
        FinishReusableReadback(locked[index], *results[index]);
    }
    frame.readbackQpcTicks +=
        ReadPerformanceCounter() - unlockStarted;
    return published;
}

} // namespace bfvr::d3d8probe
