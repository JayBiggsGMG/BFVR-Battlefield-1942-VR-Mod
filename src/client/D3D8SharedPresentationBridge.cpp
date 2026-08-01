#include "client/D3D8SharedPresentationBridge.h"

#include "client/ControllerInputCache.h"
#include "client/D3D8To9SharedTextureProducer.h"
#include "client/D3D8StereoProbeRecords.h"
#include "presenter/SharedControlChannel.h"
#include "stereo/StereoMath.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
constexpr DWORD kD3DFormatA8R8G8B8 = 21;
constexpr DWORD kD3DFormatA2B10G10R10 = 35;
constexpr DWORD kD3DFormatA16B16G16R16F = 113;

std::wstring QuoteArgument(const std::wstring& argument)
{
    std::wstring quoted = L"\"";
    quoted += argument;
    quoted += L"\"";
    return quoted;
}

bool IsProcessRunning(HANDLE process)
{
    return process != nullptr &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool GetModuleDirectory(std::wstring& directory)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
            &module))
    {
        return false;
    }
    std::array<wchar_t, 32768> path = {};
    const DWORD length = GetModuleFileNameW(
        module,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return false;
    }
    wchar_t* const separator = wcsrchr(path.data(), L'\\');
    if (separator == nullptr)
    {
        return false;
    }
    *separator = L'\0';
    directory = path.data();
    return true;
}

bfvr::shared::SharedTextureRequirements ReadRequirements(
    const bfvr::shared::ControlBlock& block,
    UINT logicalUiWidth,
    UINT logicalUiHeight)
{
    bfvr::shared::SharedTextureRequirements requirements = {};
    requirements.adapterLuid.HighPart = block.requirements.adapterLuidHigh;
    requirements.adapterLuid.LowPart = block.requirements.adapterLuidLow;
    requirements.minimumFeatureLevel =
        static_cast<D3D_FEATURE_LEVEL>(block.requirements.minimumFeatureLevel);
    requirements.leftWorldWidth = block.requirements.leftWorldWidth;
    requirements.leftWorldHeight = block.requirements.leftWorldHeight;
    requirements.rightWorldWidth = block.requirements.rightWorldWidth;
    requirements.rightWorldHeight = block.requirements.rightWorldHeight;
    requirements.uiWidth = logicalUiWidth;
    requirements.uiHeight = logicalUiHeight;
    requirements.format = static_cast<DXGI_FORMAT>(block.requirements.format);
    return requirements;
}

UINT ScaleWorldDimension(UINT dimension, float scale)
{
    if (dimension == 0)
    {
        return 0;
    }
    const double scaled =
        std::round(static_cast<double>(dimension) * static_cast<double>(scale));
    UINT result = static_cast<UINT>(std::max(2.0, scaled));
    if ((result & 1U) != 0)
    {
        ++result;
    }
    return result;
}

bfvr::shared::SharedTextureRequirements MakeProducerRequirements(
    const bfvr::shared::SharedTextureRequirements& destination,
    float worldRenderScale)
{
    bfvr::shared::SharedTextureRequirements producer = destination;
    producer.leftWorldWidth =
        ScaleWorldDimension(destination.leftWorldWidth, worldRenderScale);
    producer.leftWorldHeight =
        ScaleWorldDimension(destination.leftWorldHeight, worldRenderScale);
    producer.rightWorldWidth =
        ScaleWorldDimension(destination.rightWorldWidth, worldRenderScale);
    producer.rightWorldHeight =
        ScaleWorldDimension(destination.rightWorldHeight, worldRenderScale);
    return producer;
}

bool IsFinitePose(const bfvr::shared::SharedPresentationPose& pose)
{
    return
        std::isfinite(pose.orientationX) &&
        std::isfinite(pose.orientationY) &&
        std::isfinite(pose.orientationZ) &&
        std::isfinite(pose.orientationW) &&
        std::isfinite(pose.positionX) &&
        std::isfinite(pose.positionY) &&
        std::isfinite(pose.positionZ);
}

bool IsFiniteUnitQuaternion(const bfvr::shared::SharedPresentationPose& pose)
{
    const float lengthSquared =
        pose.orientationX * pose.orientationX +
        pose.orientationY * pose.orientationY +
        pose.orientationZ * pose.orientationZ +
        pose.orientationW * pose.orientationW;
    return std::isfinite(lengthSquared) &&
        lengthSquared >= 0.25F && lengthSquared <= 2.25F;
}

void CopyControllerPose(
    const bfvr::shared::SharedPresentationPose& source,
    bfvr::D3D8RuntimeControllerPose& destination)
{
    destination.orientationX = source.orientationX;
    destination.orientationY = source.orientationY;
    destination.orientationZ = source.orientationZ;
    destination.orientationW = source.orientationW;
    destination.positionX = source.positionX;
    destination.positionY = source.positionY;
    destination.positionZ = source.positionZ;
}
} // namespace

namespace bfvr
{
class D3D8SharedPresentationBridge::Impl
{
public:
    ~Impl()
    {
        Shutdown();
    }

    bool Initialize(
        UINT logicalUiWidth,
        UINT logicalUiHeight,
        float worldRenderScale,
        D3D8PresentationCompanion requestedCompanion,
        D3D8SharedPresentationLogCallback callback,
        bool forceCpuTransport)
    {
        Shutdown();
        ClearAcceptedControllerInput();
        logCallback = callback;
        if (logicalUiWidth == 0 || logicalUiHeight == 0)
        {
            WriteLog(L"OpenXR game bridge rejected an empty logical UI size.");
            return false;
        }
        if (!std::isfinite(worldRenderScale) ||
            worldRenderScale < 0.5F ||
            worldRenderScale > 1.25F)
        {
            WriteLog(
                L"OpenXR game bridge rejected world render scale %.3f; the supported probe range is 0.50-1.25.",
                worldRenderScale);
            return false;
        }

        std::wstring moduleDirectory;
        if (!GetModuleDirectory(moduleDirectory))
        {
            WriteLog(L"OpenXR game bridge could not resolve the BFVR client directory.");
            return false;
        }
        companion = requestedCompanion;
        presenterPath = moduleDirectory +
            (companion == D3D8PresentationCompanion::OfflineTransport
                ? L"\\BFVRSharedTextureConsumerProbe.exe"
                : L"\\BFVRPresenter.exe");
        if (GetFileAttributesW(presenterPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            WriteLog(
                L"D3D8 presentation bridge requires its x64 companion beside BFVRClient.dll: %s.",
                presenterPath.c_str());
            return false;
        }

        wchar_t channelBuffer[96] = {};
        swprintf_s(
            channelBuffer,
            L"Local\\BFVR-GamePresenter-%08lX-%08lX",
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetTickCount()));
        channelName = channelBuffer;
        if (!channel.Create(channelName.c_str(), GetCurrentProcessId()))
        {
            WriteLog(
                L"OpenXR game bridge could not create its control mapping (error %lu).",
                channel.LastErrorCode());
            return false;
        }
        block = channel.Get();
        block->producerFlags = shared::kProducerFlagRuntimeTimedRender;

        const std::wstring presenterLog = moduleDirectory +
            (companion == D3D8PresentationCompanion::OfflineTransport
                ? L"\\BFVRSharedTextureConsumer-game.log"
                : L"\\BFVRPresenter-game.log");
        wchar_t runUntilStoppedValue[2] = {};
        const bool runUntilStopped =
            companion == D3D8PresentationCompanion::OpenXR &&
            GetEnvironmentVariableW(
                L"BFVR_PRESENTATION_RUN_UNTIL_STOPPED",
                runUntilStoppedValue,
                static_cast<DWORD>(
                    std::size(runUntilStoppedValue))) == 1 &&
            runUntilStoppedValue[0] == L'1';
        std::wstring command =
            QuoteArgument(presenterPath) +
            L" --channel " + QuoteArgument(channelName) +
            (runUntilStopped
                ? L" --run-until-stopped"
                : L" --duration-ms 90000") +
            L" --log " + QuoteArgument(presenterLog);
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        if (!CreateProcessW(
                presenterPath.c_str(),
                mutableCommand.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                moduleDirectory.c_str(),
                &startupInfo,
                &presenterProcess))
        {
            WriteLog(
                L"OpenXR game bridge could not launch its x64 companion (error %lu).",
                GetLastError());
            shared::PublishState(
                &block->producerState,
                shared::ProcessState::Failed);
            Shutdown();
            return false;
        }
        CloseHandle(presenterProcess.hThread);
        presenterProcess.hThread = nullptr;

        const DWORD waitStarted = GetTickCount();
        while (GetTickCount() - waitStarted < 20000)
        {
            const shared::ProcessState state =
                shared::ReadState(&block->presenterState);
            if (state == shared::ProcessState::RequirementsReady)
            {
                break;
            }
            if (state == shared::ProcessState::Failed ||
                !IsProcessRunning(presenterProcess.hProcess))
            {
                WriteLog(
                    L"OpenXR game bridge companion failed before publishing requirements (state=%ld error=%ld).",
                    static_cast<long>(state),
                    InterlockedCompareExchange(&block->presenterError, 0, 0));
                Shutdown();
                return false;
            }
            Sleep(5);
        }
        if (shared::ReadState(&block->presenterState) !=
            shared::ProcessState::RequirementsReady)
        {
            WriteLog(L"OpenXR game bridge timed out waiting for runtime requirements.");
            Shutdown();
            return false;
        }

        runtimeUiWidth = block->requirements.uiWidth;
        runtimeUiHeight = block->requirements.uiHeight;
        destinationRequirements =
            ReadRequirements(*block, logicalUiWidth, logicalUiHeight);
        if (destinationRequirements.format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            destinationRequirements.format !=
                DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        {
            WriteLog(
                L"OpenXR game bridge requires BGRA8 UNORM or BGRA8 sRGB transport; runtime selected format %u.",
                static_cast<unsigned int>(destinationRequirements.format));
            Shutdown();
            return false;
        }
        requirements =
            MakeProducerRequirements(destinationRequirements, worldRenderScale);
        gpuSharedTargets =
            !forceCpuTransport && gpuProducer.Resolve();
        if (companion == D3D8PresentationCompanion::OfflineTransport &&
            !gpuSharedTargets)
        {
            WriteLog(
                L"Offline shared-target control requires the BFVR d3d8to9 bridge ABI.");
            Shutdown();
            return false;
        }
        if (!gpuSharedTargets &&
            !producer.Initialize(
                channelName.c_str(),
                requirements,
                &Impl::ProducerLogThunk,
                this))
        {
            WriteLog(L"OpenXR game bridge could not create its x86 shared textures.");
            Shutdown();
            return false;
        }

        if (!gpuSharedTargets)
        {
            producer.CopyDescriptions(
                block->textures,
                shared::kTextureCount);
            shared::PublishState(
                &block->producerState,
                shared::ProcessState::TexturesReady);
            texturesPublished = true;
        }
        initialized = true;
        WriteLog(
            L"D3D8 presentation bridge is ready: transport=%s source world=%ux%u/%ux%u at scale %.3f, destination=%ux%u/%ux%u, logical UI=%ux%u, destinationFormat=%u.",
            gpuSharedTargets
                ? L"D3D9Ex legacy shared GPU targets"
                : L"D3D8 readback plus D3D11 upload",
            requirements.leftWorldWidth,
            requirements.leftWorldHeight,
            requirements.rightWorldWidth,
            requirements.rightWorldHeight,
            worldRenderScale,
            destinationRequirements.leftWorldWidth,
            destinationRequirements.leftWorldHeight,
            destinationRequirements.rightWorldWidth,
            destinationRequirements.rightWorldHeight,
            requirements.uiWidth,
            requirements.uiHeight,
            static_cast<unsigned int>(requirements.format));
        return true;
    }

    bool EnsureGpuFrameTargets(
        void* d3d8Device,
        std::array<void*, shared::kTextureCount>& surfaces)
    {
        if (!initialized || block == nullptr)
        {
            return false;
        }
        if (!gpuSharedTargets)
        {
            return true;
        }
        if (texturesPublished)
        {
            return std::all_of(
                surfaces.begin(),
                surfaces.end(),
                [](const void* surface) { return surface != nullptr; });
        }

        std::array<shared::SharedTextureDescription, shared::kTextureCount>
            descriptions = {};
        if (!gpuProducer.CreateTargets(
                d3d8Device,
                requirements,
                surfaces,
                descriptions))
        {
            WriteLog(
                L"D3D9Ex shared-target creation failed on the D3D8 device thread: target=%zu size=%ux%u format=%s result=0x%08lX small64=0x%08lX extended=%d cooperative=0x%08lX helperAttempts=%ld helperStage=%lu helperResult=0x%08lX createDevice=0x%08lX createTexture=0x%08lX gameOpen=0x%08lX.",
                gpuProducer.FailedTargetIndex(),
                gpuProducer.FailedTargetIndex() == 1
                    ? requirements.rightWorldWidth
                    : gpuProducer.FailedTargetIndex() == 2
                    ? requirements.uiWidth
                    : requirements.leftWorldWidth,
                gpuProducer.FailedTargetIndex() == 1
                    ? requirements.rightWorldHeight
                    : gpuProducer.FailedTargetIndex() == 2
                    ? requirements.uiHeight
                    : requirements.leftWorldHeight,
                gpuProducer.FailedTargetIndex() == 2
                    ? L"A16B16G16R16F"
                    : L"A2B10G10R10",
                static_cast<unsigned long>(
                    gpuProducer.LastCreateResult()),
                static_cast<unsigned long>(
                    gpuProducer.SmallProbeResult()),
                gpuProducer.DeviceDiagnostics().extendedDevice,
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().cooperativeLevel),
                gpuProducer.DeviceDiagnostics().helperAttempts,
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().lastHelperStage),
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().lastHelperResult),
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().
                        lastHelperCreateDeviceResult),
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().
                        lastHelperCreateTextureResult),
                static_cast<unsigned long>(
                    gpuProducer.DeviceDiagnostics().lastGameOpenResult));
            shared::PublishState(
                &block->producerState,
                shared::ProcessState::Failed);
            return false;
        }
        for (std::size_t index = 0; index < descriptions.size(); ++index)
        {
            block->textures[index] = descriptions[index];
        }
        shared::PublishState(
            &block->producerState,
            shared::ProcessState::TexturesReady);
        texturesPublished = true;
        WriteLog(
            L"Published Reset-owned D3D9Ex shared targets: world=%ux%u R10G10B10A2 x2, UI=%ux%u R16G16B16A16_FLOAT, helperDeviceCreations=%ld. Legacy handles are lifetime-bound resource tokens and are never passed to CloseHandle.",
            requirements.leftWorldWidth,
            requirements.leftWorldHeight,
            requirements.uiWidth,
            requirements.uiHeight,
            gpuProducer.DeviceDiagnostics().helperDeviceCreations);
        return true;
    }

    bool ReadControllerSample(
        LONG sequence,
        std::int64_t predictedDisplayTime,
        D3D8RuntimeControllerSample& destination)
    {
        destination = {};
        if (InterlockedCompareExchange(&block->controllerSampleSequence, 0, 0) !=
            sequence)
        {
            WriteLog(
                L"OpenXR controller input rejected missing or stale sample for render request %ld (sampleSequence=%ld).",
                sequence,
                InterlockedCompareExchange(
                    &block->controllerSampleSequence,
                    0,
                    0));
            return false;
        }
        MemoryBarrier();
        const shared::SharedControllerSample source = block->controllerSample;
        if (source.predictedDisplayTime != predictedDisplayTime ||
            (source.flags & shared::kControllerSampleFlagSessionFocused) == 0)
        {
            WriteLog(
                L"OpenXR controller input rejected sample %ld: timestamp=%lld expected=%lld flags=0x%08lX.",
                sequence,
                static_cast<long long>(source.predictedDisplayTime),
                static_cast<long long>(predictedDisplayTime),
                static_cast<unsigned long>(source.flags));
            return false;
        }
        for (std::size_t hand = 0; hand < destination.hands.size(); ++hand)
        {
            const shared::SharedControllerHandSample& sourceHand = source.hands[hand];
            const bool finiteValues =
                std::isfinite(sourceHand.triggerValue) &&
                std::isfinite(sourceHand.squeezeValue) &&
                std::isfinite(sourceHand.thumbstickX) &&
                std::isfinite(sourceHand.thumbstickY) &&
                sourceHand.triggerValue >= 0.0F && sourceHand.triggerValue <= 1.0F &&
                sourceHand.squeezeValue >= 0.0F && sourceHand.squeezeValue <= 1.0F &&
                sourceHand.thumbstickX >= -1.0F && sourceHand.thumbstickX <= 1.0F &&
                sourceHand.thumbstickY >= -1.0F && sourceHand.thumbstickY <= 1.0F;
            const bool aimCoordinatesValid =
                (sourceHand.flags &
                    (shared::kControllerHandFlagAimPositionValid |
                     shared::kControllerHandFlagAimOrientationValid)) == 0 ||
                (IsFinitePose(sourceHand.aimPose) &&
                 IsFiniteUnitQuaternion(sourceHand.aimPose));
            const bool gripCoordinatesValid =
                (sourceHand.flags &
                    (shared::kControllerHandFlagGripPositionValid |
                     shared::kControllerHandFlagGripOrientationValid)) == 0 ||
                (IsFinitePose(sourceHand.gripPose) &&
                 IsFiniteUnitQuaternion(sourceHand.gripPose));
            if (!finiteValues || !aimCoordinatesValid || !gripCoordinatesValid)
            {
                WriteLog(
                    L"OpenXR controller input rejected invalid coordinates or ranges in sample %ld hand=%zu.",
                    sequence,
                    hand);
                return false;
            }
            D3D8RuntimeControllerHand& destinationHand = destination.hands[hand];
            destinationHand.flags = sourceHand.flags;
            destinationHand.buttons = sourceHand.buttons;
            CopyControllerPose(sourceHand.aimPose, destinationHand.aimPose);
            CopyControllerPose(sourceHand.gripPose, destinationHand.gripPose);
            destinationHand.triggerValue = sourceHand.triggerValue;
            destinationHand.squeezeValue = sourceHand.squeezeValue;
            destinationHand.thumbstickX = sourceHand.thumbstickX;
            destinationHand.thumbstickY = sourceHand.thumbstickY;
        }
        destination.valid = true;
        destination.sessionFocused = true;
        destination.predictedDisplayTime = source.predictedDisplayTime;
        LogControllerSample(sequence, destination);
        return true;
    }

    void LogControllerSample(
        LONG sequence,
        const D3D8RuntimeControllerSample& sample)
    {
        constexpr DWORD kControllerDiagnosticLogPeriodMs = 5000;
        const D3D8RuntimeControllerHand& left = sample.hands[0];
        const D3D8RuntimeControllerHand& right = sample.hands[1];
        const DWORD flags = left.flags ^ (right.flags << 16);
        const DWORD buttons = left.buttons ^ (right.buttons << 16);
        const DWORD analogPresses =
            (left.triggerValue >= 0.55F ? 0x1U : 0U) |
            (left.squeezeValue >= 0.55F ? 0x2U : 0U) |
            (right.triggerValue >= 0.55F ? 0x4U : 0U) |
            (right.squeezeValue >= 0.55F ? 0x8U : 0U);
        const DWORD now = GetTickCount();
        // This is diagnostic output only. Input samples continue to be
        // copied every frame; keep controller movement from opening and
        // closing observer.log at render cadence.
        if (now - lastControllerLogAt < kControllerDiagnosticLogPeriodMs)
        {
            return;
        }
        lastControllerFlags = flags;
        lastControllerButtons = buttons;
        lastControllerAnalogPresses = analogPresses;
        lastControllerLogAt = now;
        WriteLog(
            L"OpenXR controller sample %ld accepted for the local-frame overlay: left flags=0x%03lX buttons=0x%02lX trigger=%.3f squeeze=%.3f stick=(%.3f,%.3f); right flags=0x%03lX buttons=0x%02lX trigger=%.3f squeeze=%.3f stick=(%.3f,%.3f).",
            sequence,
            static_cast<unsigned long>(left.flags),
            static_cast<unsigned long>(left.buttons),
            left.triggerValue,
            left.squeezeValue,
            left.thumbstickX,
            left.thumbstickY,
            static_cast<unsigned long>(right.flags),
            static_cast<unsigned long>(right.buttons),
            right.triggerValue,
            right.squeezeValue,
            right.thumbstickX,
            right.thumbstickY);
    }

    bool RequestRender(D3D8RuntimeRenderRequest& request, DWORD timeoutMs)
    {
        request = {};
        if (!initialized || block == nullptr)
        {
            WriteLog(
                L"OpenXR game bridge cannot request a render frame because it is not initialized (initialized=%d channel=%p).",
                initialized ? 1 : 0,
                block);
            return false;
        }
        // The runtime may legitimately decline to render for a short period
        // (for example while focus is changing).  Keep one request sequence
        // outstanding so a nonblocking continuous-mode poll neither loses the
        // request nor advances ahead of the x64 presenter.
        const LONG sequence = pendingRenderRequest != 0
            ? pendingRenderRequest
            : InterlockedIncrement(&block->renderReadySequence);
        pendingRenderRequest = sequence;
        const DWORD waitStarted = GetTickCount();
        for (;;)
        {
            if (InterlockedCompareExchange(
                    &block->renderRequestSequence,
                    0,
                    0) == sequence)
            {
                MemoryBarrier();
                const shared::SharedRenderRequest source = block->renderRequest;
                if (source.shouldRender == 0 || source.viewsValid == 0)
                {
                    WriteLog(
                        L"OpenXR game bridge rejected render request %ld: shouldRender=%ld viewsValid=%ld presenterState=%ld presenterError=%ld.",
                        sequence,
                        source.shouldRender,
                        source.viewsValid,
                        static_cast<long>(
                            shared::ReadState(&block->presenterState)),
                        InterlockedCompareExchange(
                            &block->presenterError,
                            0,
                            0));
                    return false;
                }
                request.sequence = sequence;
                request.predictedDisplayTime = source.predictedDisplayTime;
                request.headPoseValid =
                    source.headPoseValid != 0 &&
                    IsFinitePose(source.headPose) &&
                    IsFiniteUnitQuaternion(source.headPose);
                request.headPoseTracked =
                    request.headPoseValid && source.headPoseTracked != 0;
                if (request.headPoseValid)
                {
                    request.headPose.orientationX =
                        source.headPose.orientationX;
                    request.headPose.orientationY =
                        source.headPose.orientationY;
                    request.headPose.orientationZ =
                        source.headPose.orientationZ;
                    request.headPose.orientationW =
                        source.headPose.orientationW;
                    request.headPose.positionX = source.headPose.positionX;
                    request.headPose.positionY = source.headPose.positionY;
                    request.headPose.positionZ = source.headPose.positionZ;
                }
                for (std::size_t eye = 0; eye < request.views.size(); ++eye)
                {
                    const shared::SharedPresentationView& sourceView =
                        source.views[eye];
                    D3D8RuntimeView& destination = request.views[eye];
                    destination.orientationX = sourceView.pose.orientationX;
                    destination.orientationY = sourceView.pose.orientationY;
                    destination.orientationZ = sourceView.pose.orientationZ;
                    destination.orientationW = sourceView.pose.orientationW;
                    destination.positionX = sourceView.pose.positionX;
                    destination.positionY = sourceView.pose.positionY;
                    destination.positionZ = sourceView.pose.positionZ;
                    destination.angleLeft = sourceView.fov.angleLeft;
                    destination.angleRight = sourceView.fov.angleRight;
                    destination.angleUp = sourceView.fov.angleUp;
                    destination.angleDown = sourceView.fov.angleDown;
                }
                // A rejected controller sample still does not reject the
                // visual frame. It clears the input cache so native input
                // remains unchanged until a fresh focused sample arrives.
                if (ReadControllerSample(
                        sequence,
                        request.predictedDisplayTime,
                        request.controllerInput))
                {
                    PublishAcceptedControllerInput(
                        request.controllerInput,
                        MakeD3D8RuntimeHeadReference(request),
                        request.headPoseTracked);
                }
                else
                {
                    ClearAcceptedControllerInput();
                }
                pendingRenderRequest = 0;
                return true;
            }
            if (!IsHealthy())
            {
                WriteLog(
                    L"OpenXR game bridge became unhealthy while waiting for render request %ld: requestSequence=%ld frameSequence=%ld presenterState=%ld presenterError=%ld shutdown=%ld processRunning=%d.",
                    sequence,
                    InterlockedCompareExchange(
                        &block->renderRequestSequence,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->frameSequence,
                        0,
                        0),
                    static_cast<long>(
                        shared::ReadState(&block->presenterState)),
                    InterlockedCompareExchange(
                        &block->presenterError,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->shutdownRequested,
                        0,
                        0),
                    IsProcessRunning(presenterProcess.hProcess) ? 1 : 0);
                pendingRenderRequest = 0;
                return false;
            }
            if (timeoutMs == 0 || GetTickCount() - waitStarted >= timeoutMs)
            {
                break;
            }
            Sleep(1);
        }
        if (timeoutMs != 0)
        {
            WriteLog(
                L"OpenXR game bridge timed out waiting %lu ms for render request %ld: requestSequence=%ld frameSequence=%ld presenterState=%ld presenterError=%ld.",
                static_cast<unsigned long>(timeoutMs),
                sequence,
                InterlockedCompareExchange(
                    &block->renderRequestSequence,
                    0,
                    0),
                InterlockedCompareExchange(
                    &block->frameSequence,
                    0,
                    0),
                static_cast<long>(
                    shared::ReadState(&block->presenterState)),
                InterlockedCompareExchange(
                    &block->presenterError,
                    0,
                    0));
        }
        return false;
    }

    bool PublishFrame(
        const D3D8RuntimeRenderRequest& request,
        const std::array<D3D8SharedFramePixels, 3>& frame,
        const D3D8RuntimeUiPlacement& uiPlacement)
    {
        if (!initialized || block == nullptr || request.sequence <= 0)
        {
            return false;
        }
        std::array<shared::SharedTexturePixels, shared::kTextureCount> pixels = {};
        for (std::size_t index = 0; index < pixels.size(); ++index)
        {
            pixels[index].data = frame[index].data;
            pixels[index].rowPitch = frame[index].rowPitch;
            pixels[index].width = frame[index].width;
            pixels[index].height = frame[index].height;
            pixels[index].format = requirements.format;
        }
        if (!producer.PublishFrame(pixels))
        {
            return false;
        }
        PublishUiPlacement(uiPlacement);
        InterlockedIncrement(&block->producedFrameCount);
        MemoryBarrier();
        InterlockedExchange(&block->frameSequence, request.sequence);
        return true;
    }

    bool PublishGpuFrame(
        void* d3d8Device,
        const D3D8RuntimeRenderRequest& request,
        DWORD timeoutMs,
        const D3D8RuntimeUiPlacement& uiPlacement)
    {
        if (!initialized ||
            block == nullptr ||
            !gpuSharedTargets ||
            !texturesPublished ||
            request.sequence <= 0)
        {
            return false;
        }
        if (!gpuProducer.WaitForGpu(d3d8Device, timeoutMs))
        {
            WriteLog(
                L"D3D9Ex shared-target GPU completion timed out before frame %ld publication.",
                request.sequence);
            return false;
        }
        PublishUiPlacement(uiPlacement);
        InterlockedIncrement(&block->producedFrameCount);
        MemoryBarrier();
        InterlockedExchange(&block->frameSequence, request.sequence);
        return true;
    }

    bool WaitForConsumption(LONG sequence, DWORD timeoutMs)
    {
        if (!initialized || block == nullptr || sequence <= 0)
        {
            return false;
        }
        const DWORD waitStarted = GetTickCount();
        while (GetTickCount() - waitStarted < timeoutMs)
        {
            if (InterlockedCompareExchange(
                    &block->consumedFrameSequence,
                    0,
                    0) == sequence)
            {
                return true;
            }
            if (!IsHealthy())
            {
                WriteLog(
                    L"OpenXR game bridge became unhealthy while waiting for source consumption %ld: rendered=%ld consumed=%ld presenterState=%ld presenterError=%ld shutdown=%ld processRunning=%d.",
                    sequence,
                    InterlockedCompareExchange(
                        &block->renderedFrameSequence,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->consumedFrameSequence,
                        0,
                        0),
                    static_cast<long>(
                        shared::ReadState(&block->presenterState)),
                    InterlockedCompareExchange(
                        &block->presenterError,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->shutdownRequested,
                        0,
                        0),
                    IsProcessRunning(presenterProcess.hProcess) ? 1 : 0);
                return false;
            }
            Sleep(2);
        }
        WriteLog(
            L"OpenXR game bridge timed out waiting %lu ms for source consumption %ld: rendered=%ld consumed=%ld presenterState=%ld presenterError=%ld.",
            static_cast<unsigned long>(timeoutMs),
            sequence,
            InterlockedCompareExchange(
                &block->renderedFrameSequence,
                0,
                0),
            InterlockedCompareExchange(
                &block->consumedFrameSequence,
                0,
                0),
            static_cast<long>(
                shared::ReadState(&block->presenterState)),
            InterlockedCompareExchange(
                &block->presenterError,
                0,
                0));
        return false;
    }

    bool WaitForPresentation(LONG sequence, DWORD timeoutMs)
    {
        if (!initialized || block == nullptr || sequence <= 0)
        {
            return false;
        }
        const DWORD waitStarted = GetTickCount();
        while (GetTickCount() - waitStarted < timeoutMs)
        {
            if (InterlockedCompareExchange(
                    &block->renderedFrameSequence,
                    0,
                    0) == sequence)
            {
                return true;
            }
            if (!IsHealthy())
            {
                WriteLog(
                    L"OpenXR game bridge became unhealthy while waiting for presentation %ld: rendered=%ld consumed=%ld presenterState=%ld presenterError=%ld shutdown=%ld processRunning=%d.",
                    sequence,
                    InterlockedCompareExchange(
                        &block->renderedFrameSequence,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->consumedFrameSequence,
                        0,
                        0),
                    static_cast<long>(
                        shared::ReadState(&block->presenterState)),
                    InterlockedCompareExchange(
                        &block->presenterError,
                        0,
                        0),
                    InterlockedCompareExchange(
                        &block->shutdownRequested,
                        0,
                        0),
                    IsProcessRunning(presenterProcess.hProcess) ? 1 : 0);
                return false;
            }
            Sleep(2);
        }
        WriteLog(
            L"OpenXR game bridge timed out waiting %lu ms for presentation %ld: rendered=%ld consumed=%ld presenterState=%ld presenterError=%ld.",
            static_cast<unsigned long>(timeoutMs),
            sequence,
            InterlockedCompareExchange(
                &block->renderedFrameSequence,
                0,
                0),
            InterlockedCompareExchange(
                &block->consumedFrameSequence,
                0,
                0),
            static_cast<long>(shared::ReadState(&block->presenterState)),
            InterlockedCompareExchange(&block->presenterError, 0, 0));
        return false;
    }

    void PrepareForResourceRelease()
    {
        if (gpuSharedTargets)
        {
            StopCompanion();
        }
    }

    void Shutdown()
    {
        ClearAcceptedControllerInput();
        StopCompanion();
        channel.Close();
        block = nullptr;
        requirements = {};
        destinationRequirements = {};
        runtimeUiWidth = 0;
        runtimeUiHeight = 0;
        presenterProcess = {};
        channelName.clear();
        presenterPath.clear();
        gpuProducer = {};
        companion = D3D8PresentationCompanion::OpenXR;
        gpuSharedTargets = false;
        texturesPublished = false;
        companionStopped = false;
        pendingRenderRequest = 0;
        lastControllerFlags = 0;
        lastControllerButtons = 0;
        lastControllerAnalogPresses = 0;
        lastControllerLogAt = 0;
        initialized = false;
    }

    void StopCompanion()
    {
        if (companionStopped)
        {
            return;
        }
        if (block != nullptr)
        {
            shared::PublishState(
                &block->producerState,
                shared::ProcessState::Stopping);
            InterlockedExchange(&block->shutdownRequested, 1);
        }
        if (presenterProcess.hProcess != nullptr)
        {
            const DWORD wait = WaitForSingleObject(
                presenterProcess.hProcess,
                5000);
            if (wait == WAIT_TIMEOUT)
            {
                TerminateProcess(presenterProcess.hProcess, 9);
                WaitForSingleObject(presenterProcess.hProcess, 2000);
                WriteLog(
                    L"D3D8 presentation bridge terminated only its owned companion after bounded shutdown timed out.");
            }
            CloseHandle(presenterProcess.hProcess);
            presenterProcess.hProcess = nullptr;
        }
        producer.Shutdown();
        if (block != nullptr)
        {
            shared::PublishState(
                &block->producerState,
                shared::ProcessState::Stopped);
        }
        companionStopped = true;
    }

    bool IsHealthy() const
    {
        return block != nullptr &&
            IsProcessRunning(presenterProcess.hProcess) &&
            shared::ReadState(&block->presenterState) !=
                shared::ProcessState::Failed &&
            InterlockedCompareExchange(&block->shutdownRequested, 0, 0) == 0;
    }

    static void ProducerLogThunk(void* context, const wchar_t* message)
    {
        static_cast<Impl*>(context)->WriteLog(L"%s", message);
    }

    void PublishUiPlacement(const D3D8RuntimeUiPlacement& placement)
    {
        if (block == nullptr)
        {
            return;
        }

        shared::SharedPresentationPose anchor = {};
        anchor.orientationX = placement.worldAnchor.orientationX;
        anchor.orientationY = placement.worldAnchor.orientationY;
        anchor.orientationZ = placement.worldAnchor.orientationZ;
        anchor.orientationW = placement.worldAnchor.orientationW;
        anchor.positionX = placement.worldAnchor.positionX;
        anchor.positionY = placement.worldAnchor.positionY;
        anchor.positionZ = placement.worldAnchor.positionZ;
        const bool hasValidWorldAnchor =
            !placement.headLocked &&
            placement.worldAnchorValid &&
            IsFinitePose(anchor) &&
            IsFiniteUnitQuaternion(anchor);
        if (hasValidWorldAnchor)
        {
            block->frameUiWorldAnchor = anchor;
            MemoryBarrier();
        }
        InterlockedExchange(
            &block->frameUiWorldAnchorValid,
            hasValidWorldAnchor ? 1 : 0);
        InterlockedExchange(
            &block->frameUiReferenceMode,
            static_cast<LONG>(
                placement.headLocked
                    ? shared::UiReferenceMode::HeadLocked
                    : shared::UiReferenceMode::WorldLocked));
        LONG overlayFlags = placement.backToGameVisible
            ? shared::kFrameOverlayBackToGameVisible
            : 0;
        if (placement.backToGameVisible &&
            placement.backToGameHovered)
        {
            overlayFlags |= shared::kFrameOverlayBackToGameHovered;
        }
        InterlockedExchange(
            &block->frameOverlayFlags,
            overlayFlags);
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr)
        {
            return;
        }
        wchar_t message[1200] = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        logCallback(message);
    }

    shared::SharedControlChannel channel;
    shared::SharedTextureProducer producer;
    D3D8To9SharedTextureProducer gpuProducer;
    shared::ControlBlock* block = nullptr;
    shared::SharedTextureRequirements requirements = {};
    shared::SharedTextureRequirements destinationRequirements = {};
    PROCESS_INFORMATION presenterProcess = {};
    std::wstring channelName;
    std::wstring presenterPath;
    D3D8SharedPresentationLogCallback logCallback = nullptr;
    D3D8PresentationCompanion companion =
        D3D8PresentationCompanion::OpenXR;
    bool gpuSharedTargets = false;
    bool texturesPublished = false;
    bool companionStopped = false;
    LONG pendingRenderRequest = 0;
    DWORD lastControllerFlags = 0;
    DWORD lastControllerButtons = 0;
    DWORD lastControllerAnalogPresses = 0;
    DWORD lastControllerLogAt = 0;
    UINT runtimeUiWidth = 0;
    UINT runtimeUiHeight = 0;
    bool initialized = false;
};

D3D8SharedPresentationBridge::D3D8SharedPresentationBridge()
    : impl_(std::make_unique<Impl>())
{
}

D3D8SharedPresentationBridge::~D3D8SharedPresentationBridge() = default;

bool D3D8SharedPresentationBridge::Initialize(
    UINT logicalUiWidth,
    UINT logicalUiHeight,
    float worldRenderScale,
    D3D8PresentationCompanion companion,
    D3D8SharedPresentationLogCallback logCallback,
    bool forceCpuTransport)
{
    return impl_ != nullptr &&
        impl_->Initialize(
            logicalUiWidth,
            logicalUiHeight,
            worldRenderScale,
            companion,
            logCallback,
            forceCpuTransport);
}

bool D3D8SharedPresentationBridge::EnsureGpuFrameTargets(
    void* d3d8Device,
    std::array<void*, 3>& surfaces)
{
    return impl_ != nullptr &&
        impl_->EnsureGpuFrameTargets(d3d8Device, surfaces);
}

bool D3D8SharedPresentationBridge::RequestRender(
    D3D8RuntimeRenderRequest& request,
    DWORD timeoutMs)
{
    return impl_ != nullptr && impl_->RequestRender(request, timeoutMs);
}

bool D3D8SharedPresentationBridge::PublishFrame(
    const D3D8RuntimeRenderRequest& request,
    const std::array<D3D8SharedFramePixels, 3>& frame,
    const D3D8RuntimeUiPlacement& uiPlacement)
{
    return impl_ != nullptr &&
        impl_->PublishFrame(request, frame, uiPlacement);
}

bool D3D8SharedPresentationBridge::PublishGpuFrame(
    void* d3d8Device,
    const D3D8RuntimeRenderRequest& request,
    DWORD timeoutMs,
    const D3D8RuntimeUiPlacement& uiPlacement)
{
    return impl_ != nullptr &&
        impl_->PublishGpuFrame(
            d3d8Device,
            request,
            timeoutMs,
            uiPlacement);
}

bool D3D8SharedPresentationBridge::WaitForConsumption(
    LONG sequence,
    DWORD timeoutMs)
{
    return impl_ != nullptr &&
        impl_->WaitForConsumption(sequence, timeoutMs);
}

bool D3D8SharedPresentationBridge::WaitForPresentation(
    LONG sequence,
    DWORD timeoutMs)
{
    return impl_ != nullptr &&
        impl_->WaitForPresentation(sequence, timeoutMs);
}

void D3D8SharedPresentationBridge::PrepareForResourceRelease()
{
    if (impl_ != nullptr)
    {
        impl_->PrepareForResourceRelease();
    }
}

void D3D8SharedPresentationBridge::Shutdown()
{
    if (impl_ != nullptr)
    {
        impl_->Shutdown();
    }
}

bool D3D8SharedPresentationBridge::UsesGpuSharedTargets() const noexcept
{
    return impl_ != nullptr && impl_->gpuSharedTargets;
}

UINT D3D8SharedPresentationBridge::LeftWorldWidth() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.leftWorldWidth;
}

UINT D3D8SharedPresentationBridge::LeftWorldHeight() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.leftWorldHeight;
}

UINT D3D8SharedPresentationBridge::RightWorldWidth() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.rightWorldWidth;
}

UINT D3D8SharedPresentationBridge::RightWorldHeight() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.rightWorldHeight;
}

UINT D3D8SharedPresentationBridge::UiWidth() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.uiWidth;
}

UINT D3D8SharedPresentationBridge::UiHeight() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->requirements.uiHeight;
}

UINT D3D8SharedPresentationBridge::RuntimeUiWidth() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->runtimeUiWidth;
}

UINT D3D8SharedPresentationBridge::RuntimeUiHeight() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->runtimeUiHeight;
}

DWORD D3D8SharedPresentationBridge::WorldD3DFormat() const noexcept
{
    return UsesGpuSharedTargets()
        ? kD3DFormatA2B10G10R10
        : kD3DFormatA8R8G8B8;
}

DWORD D3D8SharedPresentationBridge::UiD3DFormat() const noexcept
{
    return UsesGpuSharedTargets()
        ? kD3DFormatA16B16G16R16F
        : kD3DFormatA8R8G8B8;
}

DXGI_FORMAT D3D8SharedPresentationBridge::Format() const noexcept
{
    return impl_ == nullptr ? DXGI_FORMAT_UNKNOWN : impl_->requirements.format;
}

bool BuildD3D8RuntimeStereoTransforms(
    const D3D8RuntimeRenderRequest& request,
    const D3D8RuntimeView& referenceHead,
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection)
{
    stereo::Matrix4 view = {};
    stereo::Matrix4 projection = {};
    std::memcpy(&view, &sourceView, sizeof(view));
    std::memcpy(&projection, &sourceProjection, sizeof(projection));
    const auto toPose = [](const D3D8RuntimeView& runtimeView)
    {
        return stereo::Pose{
            {
                runtimeView.positionX,
                runtimeView.positionY,
                runtimeView.positionZ},
            {
                runtimeView.orientationX,
                runtimeView.orientationY,
                runtimeView.orientationZ,
                runtimeView.orientationW}};
    };
    const auto toFov = [](const D3D8RuntimeView& runtimeView)
    {
        return stereo::FovTangents{
            std::tan(runtimeView.angleLeft),
            std::tan(runtimeView.angleRight),
            std::tan(runtimeView.angleUp),
            std::tan(runtimeView.angleDown)};
    };
    const auto pair = stereo::MakeRuntimePoseD3D8StereoPair(
        view,
        projection,
        toPose(referenceHead),
        toPose(request.views[0]),
        toPose(request.views[1]),
        toFov(request.views[0]),
        toFov(request.views[1]),
        1.0F);
    if (!pair.has_value())
    {
        return false;
    }
    std::memcpy(&leftView, &pair->leftView, sizeof(leftView));
    std::memcpy(&rightView, &pair->rightView, sizeof(rightView));
    std::memcpy(&leftProjection, &pair->leftProjection, sizeof(leftProjection));
    std::memcpy(&rightProjection, &pair->rightProjection, sizeof(rightProjection));
    return true;
}

bool BuildD3D8DiagnosticStereoTransforms(
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    float halfEyeOffset,
    float convergenceDistance,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection)
{
    stereo::Matrix4 view = {};
    stereo::Matrix4 projection = {};
    std::memcpy(&view, &sourceView, sizeof(view));
    std::memcpy(&projection, &sourceProjection, sizeof(projection));
    const auto pair = stereo::MakeDiagnosticD3D8StereoPair(
        view,
        projection,
        halfEyeOffset,
        convergenceDistance);
    if (!pair.has_value())
    {
        return false;
    }
    std::memcpy(&leftView, &pair->leftView, sizeof(leftView));
    std::memcpy(&rightView, &pair->rightView, sizeof(rightView));
    std::memcpy(&leftProjection, &pair->leftProjection, sizeof(leftProjection));
    std::memcpy(&rightProjection, &pair->rightProjection, sizeof(rightProjection));
    return true;
}

D3D8RuntimeView MakeD3D8RuntimeHeadReference(
    const D3D8RuntimeRenderRequest& request) noexcept
{
    if (request.headPoseValid)
    {
        return request.headPose;
    }

    D3D8RuntimeView reference = request.views[0];
    const auto toPose = [](const D3D8RuntimeView& view)
    {
        return stereo::Pose{
            {view.positionX, view.positionY, view.positionZ},
            {
                view.orientationX,
                view.orientationY,
                view.orientationZ,
                view.orientationW}};
    };
    const auto centre =
        stereo::ComputeCentreViewPose(toPose(request.views[0]), toPose(request.views[1]));
    if (centre.has_value())
    {
        reference.orientationX = centre->orientation.x;
        reference.orientationY = centre->orientation.y;
        reference.orientationZ = centre->orientation.z;
        reference.orientationW = centre->orientation.w;
        reference.positionX = centre->position.x;
        reference.positionY = centre->position.y;
        reference.positionZ = centre->position.z;
    }
    return reference;
}

bool BuildD3D8RuntimeRotationOnlyTransforms(
    const D3D8RuntimeRenderRequest& request,
    const D3D8RuntimeView& referenceHead,
    const d3d8probe::D3DMatrix& sourceView,
    const d3d8probe::D3DMatrix& sourceProjection,
    d3d8probe::D3DMatrix& leftView,
    d3d8probe::D3DMatrix& rightView,
    d3d8probe::D3DMatrix& leftProjection,
    d3d8probe::D3DMatrix& rightProjection)
{
    D3D8RuntimeRenderRequest rotationOnly = request;
    for (D3D8RuntimeView& view : rotationOnly.views)
    {
        view.positionX = referenceHead.positionX;
        view.positionY = referenceHead.positionY;
        view.positionZ = referenceHead.positionZ;
    }
    return BuildD3D8RuntimeStereoTransforms(
        rotationOnly,
        referenceHead,
        sourceView,
        sourceProjection,
        leftView,
        rightView,
        leftProjection,
        rightProjection);
}
} // namespace bfvr
