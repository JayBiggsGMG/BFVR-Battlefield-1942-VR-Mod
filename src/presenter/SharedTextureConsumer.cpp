#include "presenter/SharedTextureConsumer.h"
#include "settings/UserSettings.h"

#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <iterator>

namespace
{
bool ReadPerformanceDiagnosticsEnabled()
{
    wchar_t value[16] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_DIAGNOSTICS",
        value,
        static_cast<DWORD>(std::size(value)));
    return !((length == 3 && _wcsicmp(value, L"off") == 0) ||
        (length == 1 && value[0] == L'0'));
}

bool IsSrgbFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

bool ReadWorldFxaaEnabled()
{
    wchar_t value[2] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"BFVR_OPENXR_FXAA",
        value,
        static_cast<DWORD>(std::size(value)));
    // The owner has accepted this quality path. Disable it only for a
    // deliberate measured A/B; it must not silently change visual quality.
    return !(length == 1 && value[0] == L'0');
}

struct WorldBloomConfiguration
{
    bool enabled = false;
    float threshold = 0.55F;
    float intensity = 0.35F;
};

float ReadEnvironmentFloat(
    const wchar_t* name,
    float fallback,
    float minimum,
    float maximum)
{
    wchar_t value[64] = {};
    const DWORD length = GetEnvironmentVariableW(
        name,
        value,
        static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value))
        return fallback;
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(value, &end);
    if (errno != 0 || end == value || end == nullptr || *end != L'\0' ||
        !std::isfinite(parsed))
    {
        return fallback;
    }
    return std::clamp(static_cast<float>(parsed), minimum, maximum);
}

WorldBloomConfiguration ReadWorldBloomConfiguration()
{
    WorldBloomConfiguration configuration;
    wchar_t enabled[2] = {};
    const DWORD enabledLength = GetEnvironmentVariableW(
        L"BFVR_OPENXR_BLOOM",
        enabled,
        static_cast<DWORD>(std::size(enabled)));
    // This is a deliberate visual experiment. Only an explicit 1 opts in.
    configuration.enabled = enabledLength == 1 && enabled[0] == L'1';
    configuration.threshold = ReadEnvironmentFloat(
        L"BFVR_OPENXR_BLOOM_THRESHOLD",
        configuration.threshold,
        0.0F,
        1.0F);
    configuration.intensity = ReadEnvironmentFloat(
        L"BFVR_OPENXR_BLOOM_INTENSITY",
        configuration.intensity,
        0.0F,
        2.0F);
    configuration.enabled = configuration.enabled &&
        configuration.intensity > 0.0F;
    return configuration;
}

bool IsConvertibleSharedFormat(
    DXGI_FORMAT source,
    DXGI_FORMAT destination)
{
    if (source == destination)
    {
        return true;
    }
    const bool supportedDestination =
        destination == DXGI_FORMAT_B8G8R8A8_UNORM ||
        destination == DXGI_FORMAT_R8G8B8A8_UNORM ||
        destination == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        destination == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if (!supportedDestination)
    {
        return false;
    }
    return source == DXGI_FORMAT_R10G10B10A2_UNORM ||
        source == DXGI_FORMAT_R16G16B16A16_FLOAT ||
        source == DXGI_FORMAT_B8G8R8A8_UNORM ||
        source == DXGI_FORMAT_R8G8B8A8_UNORM ||
        source == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
        source == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
} // namespace

namespace bfvr::shared
{
SharedTextureConsumer::~SharedTextureConsumer()
{
    Shutdown();
}

bool SharedTextureConsumer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const SharedTextureDescription* descriptions,
    std::size_t count,
    const SharedTextureDescription* depthDescriptions,
    std::size_t depthCount,
    DepthEncoding depthEncoding,
    DWORD producerFlags,
    const PresentationRequirements& destinationRequirements,
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (device == nullptr || context == nullptr || descriptions == nullptr ||
        count < textures_.size())
    {
        WriteLog(L"Shared texture consumer received an invalid device or descriptor set.");
        return false;
    }

    ID3D11Device1* device1 = nullptr;
    HRESULT result = device->QueryInterface(
        __uuidof(ID3D11Device1),
        reinterpret_cast<void**>(&device1));
    if (FAILED(result) || device1 == nullptr)
    {
        WriteLog(
            L"Shared texture consumer requires ID3D11Device1 (HRESULT=0x%08lX).",
            static_cast<unsigned long>(result));
        return false;
    }

    device_ = device;
    device_->AddRef();
    context_ = context;
    context_->AddRef();
    collectPerformanceDiagnostics_ = ReadPerformanceDiagnosticsEnabled();
    const auto& userSettingsRuntime =
        settings::ProcessUserSettingsRuntime();
    if (userSettingsRuntime.IsReady())
    {
        const settings::UserSettingsValues values =
            settings::DecodeUserSettings(userSettingsRuntime.Current());
        worldFxaaEnabled_ = values.fxaaEnabled;
        worldFxaaSharpeningStrength_ =
            static_cast<float>(values.fxaaSharpeningPercent) / 100.0F;
        worldBloomEnabled_ = values.bloomEnabled;
        worldBloomThreshold_ =
            static_cast<float>(values.bloomThresholdPercent) / 100.0F;
        worldBloomIntensity_ =
            static_cast<float>(values.bloomIntensityPercent) / 100.0F;
        ambientOcclusionRadiusMeters_ = static_cast<float>(
            values.ambientOcclusionRadiusCentimeters) / 100.0F;
        ambientOcclusionIntensity_ = static_cast<float>(
            values.ambientOcclusionStrengthPercent) / 100.0F;
    }
    else
    {
        worldFxaaEnabled_ = ReadWorldFxaaEnabled();
        const WorldBloomConfiguration bloom = ReadWorldBloomConfiguration();
        worldBloomEnabled_ = bloom.enabled;
        worldBloomThreshold_ = bloom.threshold;
        worldBloomIntensity_ = bloom.intensity;
    }
    screenSpaceGlobalIlluminationIntensity_ = ReadEnvironmentFloat(
        L"BFVR_OPENXR_SSGI_INTENSITY",
        0.65F,
        0.0F,
        2.0F);
    screenSpaceGlobalIlluminationDebugMode_ = ReadEnvironmentFloat(
        L"BFVR_OPENXR_SSGI_DEBUG",
        0.0F,
        0.0F,
        2.0F);
    waterReflectionIntensity_ = ReadEnvironmentFloat(
        L"BFVR_OPENXR_WATER_SSR_INTENSITY",
        1.0F,
        0.0F,
        2.0F);
    const bool ambientOcclusionRequested =
        (producerFlags & kProducerFlagAmbientOcclusionRequested) != 0;
    const bool screenSpaceGlobalIlluminationRequested =
        (producerFlags &
            kProducerFlagScreenSpaceGlobalIlluminationRequested) != 0;
    const bool waterReflectionsRequested =
        (producerFlags & kProducerFlagWaterReflectionsRequested) != 0;
    const std::array<UINT, kTextureCount> destinationWidths = {
        destinationRequirements.leftWorldWidth,
        destinationRequirements.rightWorldWidth,
        destinationRequirements.uiWidth};
    const std::array<UINT, kTextureCount> destinationHeights = {
        destinationRequirements.leftWorldHeight,
        destinationRequirements.rightWorldHeight,
        destinationRequirements.uiHeight};
    const DXGI_FORMAT destinationFormat =
        static_cast<DXGI_FORMAT>(destinationRequirements.format);
    bool opened = destinationFormat != DXGI_FORMAT_UNKNOWN;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        requiresLegacyCompletionWait_ =
            requiresLegacyCompletionWait_ ||
            static_cast<SharedTextureTransport>(
                descriptions[index].transport) ==
                SharedTextureTransport::D3D9LegacyHandle;
        opened = opened &&
            OpenTexture(
                device1,
                index,
                descriptions[index],
                destinationWidths[index],
                destinationHeights[index],
                destinationFormat);
        if (!opened)
        {
            break;
        }
    }
    bool depthOpened =
        depthDescriptions != nullptr &&
        depthCount == depthTextures_.size() &&
        depthEncoding == DepthEncoding::PackedDeviceDepthBgra8;
    if (depthOpened)
    {
        for (std::size_t index = 0; index < depthTextures_.size(); ++index)
        {
            depthOpened = OpenDepthTexture(
                device1,
                index,
                depthDescriptions[index]);
            if (!depthOpened)
                break;
        }
    }
    device1->Release();
    if (!opened)
    {
        Shutdown();
        return false;
    }
    if (depthOpened && ambientOcclusionRequested && ambientOcclusion_.Initialize(
            device_,
            context_,
            logCallback_,
            logContext_,
            collectPerformanceDiagnostics_))
    {
        ambientOcclusion_.SetViewRadiusMeters(
            ambientOcclusionRadiusMeters_);
        ambientOcclusionEnabled_ = true;
    }
    if (depthOpened && screenSpaceGlobalIlluminationRequested &&
        screenSpaceGlobalIllumination_.Initialize(
            device_,
            context_,
            logCallback_,
            logContext_))
    {
        screenSpaceGlobalIlluminationEnabled_ = true;
    }
    if (depthOpened && waterReflectionsRequested && waterReflection_.Initialize(
            device_,
            context_,
            logCallback_,
            logContext_))
    {
        waterReflectionsEnabled_ = true;
    }
    if (ambientOcclusionEnabled_ || screenSpaceGlobalIlluminationEnabled_ ||
        waterReflectionsEnabled_)
    {
        scalerRequired_ = true;
        for (std::size_t index = 0; index < depthTextures_.size(); ++index)
        {
            textures_[index].requiresScaling = true;
            requiresLegacyCompletionWait_ =
                requiresLegacyCompletionWait_ ||
                static_cast<SharedTextureTransport>(
                    depthDescriptions[index].transport) ==
                    SharedTextureTransport::D3D9LegacyHandle;
        }
    }
    else if (depthOpened || depthCount != 0 || depthEncoding != DepthEncoding::None)
    {
        for (DepthTexture& texture : depthTextures_)
            ReleaseDepthTexture(texture);
        WriteLog(
            L"Shared texture consumer could not enable any requested packed-depth stage; color/UI presentation remains active.");
    }
    bool scalerReady = !scalerRequired_ || scaler_.Initialize(
        device_,
        context_,
        logCallback_,
        logContext_,
        worldBloomEnabled_,
        ambientOcclusionEnabled_,
        screenSpaceGlobalIlluminationEnabled_,
        waterReflectionsEnabled_,
        collectPerformanceDiagnostics_);
    if (!scalerReady &&
        (ambientOcclusionEnabled_ || screenSpaceGlobalIlluminationEnabled_ ||
            waterReflectionsEnabled_))
    {
        WriteLog(
            L"Shared texture consumer could not initialize the depth-effect composite variant; retrying the mandatory color/UI scaler with AO, SSGI, and water SSR disabled.");
        ambientOcclusion_.Shutdown();
        screenSpaceGlobalIllumination_.Shutdown();
        waterReflection_.Shutdown();
        ambientOcclusionEnabled_ = false;
        screenSpaceGlobalIlluminationEnabled_ = false;
        waterReflectionsEnabled_ = false;
        for (DepthTexture& texture : depthTextures_)
            ReleaseDepthTexture(texture);
        scalerReady = scaler_.Initialize(
            device_,
            context_,
            logCallback_,
            logContext_,
            worldBloomEnabled_,
            false,
            false,
            false,
            collectPerformanceDiagnostics_);
    }
    if (!scalerReady)
    {
        Shutdown();
        return false;
    }
    if (waterReflectionsEnabled_)
    {
        WriteLog(
            L"Water-only per-eye SSR is active (fixed roughness, Fresnel, intensity=%.3f); frames without a valid water mask fall back to native water.",
            waterReflectionIntensity_);
    }
    if (screenSpaceGlobalIlluminationEnabled_)
    {
        WriteLog(
            L"Spatial SSGI is active (intensity=%.2f): each eye runs native-resolution four-tap directional-plus-contrast bounce and three depth/normal-aware denoise passes before world composition. Temporal history and Ref2 UI input are disabled.",
            screenSpaceGlobalIlluminationIntensity_);
        if (screenSpaceGlobalIlluminationDebugMode_ > 0.0F)
        {
            WriteLog(
                screenSpaceGlobalIlluminationDebugMode_ > 1.5F
                    ? L"SSGI diagnostic guide-coverage view is active: white means the packed-depth/normal guide survived native preparation and denoise; black means it did not."
                    : L"SSGI diagnostic radiance view is active: bounced radiance is exposed 32x, valid zero-radiance pixels are black, and invalid packed-depth/normal guide pixels are magenta.");
        }
    }
    if (requiresLegacyCompletionWait_)
    {
        D3D11_QUERY_DESC queryDescription = {};
        queryDescription.Query = D3D11_QUERY_EVENT;
        result = device_->CreateQuery(
            &queryDescription,
            &legacyCompletionQuery_);
        if (FAILED(result) || legacyCompletionQuery_ == nullptr)
        {
            WriteLog(
                L"Shared texture consumer could not create its legacy-handle GPU completion query (HRESULT=0x%08lX).",
                static_cast<unsigned long>(result));
            Shutdown();
            return false;
        }
    }

    // Failure here is deliberately non-fatal to the native menu and OpenXR
    // transport. The x86 producer will still publish a usable Ref2 texture,
    // while this renderer logs why the optional BFVR button is unavailable.
    mainMenuOverlay_.Initialize(
        device_,
        context_,
        logCallback_,
        logContext_);

    if (scalerRequired_)
    {
        if (worldBloomEnabled_)
        {
            if (ambientOcclusionEnabled_)
            {
                WriteLog(
                    L"Shared texture consumer opened the color and packed-depth resources; independent native-resolution AO plus world-only quarter-resolution bloom are active (AO intensity=%.2f, bloom threshold=%.2f intensity=%.2f). Ref2 UI remains outside both stages.",
                    ambientOcclusionIntensity_,
                    worldBloomThreshold_,
                    worldBloomIntensity_);
            }
            else
            {
                WriteLog(
                    worldFxaaEnabled_
                        ? L"Shared texture consumer opened all three x86-produced resources and enabled x64 world FXAA plus world-only quarter-resolution bloom (threshold=%.2f intensity=%.2f); Ref2 UI and AO are outside the bloom stage."
                        : L"Shared texture consumer opened all three x86-produced resources with world FXAA disabled and world-only quarter-resolution bloom active (threshold=%.2f intensity=%.2f); Ref2 UI and AO are outside the bloom stage.",
                    worldBloomThreshold_,
                    worldBloomIntensity_);
            }
        }
        else
        {
            if (screenSpaceGlobalIlluminationEnabled_)
            {
                WriteLog(
                    L"Shared texture consumer opened the three color resources plus two packed-depth resources; native-resolution four-tap spatial SSGI is active at intensity %.2f (AO=%d, worldFXAA=%d). Ref2 UI, bloom, and temporal history are outside SSGI.",
                    screenSpaceGlobalIlluminationIntensity_,
                    ambientOcclusionEnabled_ ? 1 : 0,
                    worldFxaaEnabled_ ? 1 : 0);
            }
            else
            {
                WriteLog(
                    ambientOcclusionEnabled_
                        ? L"Shared texture consumer opened the three color resources plus two packed-depth resources; native-resolution world AO is active at intensity %.2f, temporal history and bloom are disabled."
                        : worldFxaaEnabled_
                        ? L"Shared texture consumer opened all three x86-produced resources and enabled x64 aspect-fit conversion with world FXAA; AO and bloom are disabled."
                        : L"Shared texture consumer opened all three x86-produced resources and enabled x64 aspect-fit conversion with world FXAA disabled by BFVR_OPENXR_FXAA=0; AO and bloom are disabled.",
                    ambientOcclusionIntensity_);
            }
        }
    }
    else
    {
        WriteLog(L"Shared texture consumer opened all three x86-produced resources at their exact destination sizes.");
    }
    return true;
}

bool SharedTextureConsumer::ConsumeFrame(
    LONG frameOverlayFlags,
    bool depthValid,
    const SharedDepthFrameParameters* depthFrame,
    bool waterMaskValid)
{
    ApplySavedLiveSettings();
    if (context_ == nullptr)
    {
        return false;
    }

    std::array<bool, kTextureCount> acquired = {};
    std::array<bool, kDepthTextureCount> depthAcquired = {};
    const bool useDepth =
            (ambientOcclusionEnabled_ ||
             screenSpaceGlobalIlluminationEnabled_ ||
             (waterReflectionsEnabled_ && waterMaskValid)) &&
        depthValid && depthFrame != nullptr;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        if (textures_[index].keyedMutex == nullptr)
        {
            continue;
        }
        const HRESULT result = textures_[index].keyedMutex->AcquireSync(1, 500);
        if (FAILED(result))
        {
            WriteLog(
                L"Shared texture consumer could not acquire slot %zu (HRESULT=0x%08lX).",
                index,
                static_cast<unsigned long>(result));
            for (std::size_t releaseIndex = 0; releaseIndex < index; ++releaseIndex)
            {
                if (acquired[releaseIndex])
                {
                    textures_[releaseIndex].keyedMutex->ReleaseSync(0);
                }
            }
            return false;
        }
        acquired[index] = true;
    }

    if (useDepth)
    {
        for (std::size_t index = 0; index < depthTextures_.size(); ++index)
        {
            if (depthTextures_[index].keyedMutex == nullptr)
                continue;
            const HRESULT result =
                depthTextures_[index].keyedMutex->AcquireSync(1, 500);
            if (FAILED(result))
            {
                WriteLog(
                    L"Shared texture consumer could not acquire depth slot %zu (HRESULT=0x%08lX).",
                    index,
                    static_cast<unsigned long>(result));
                for (std::size_t releaseIndex = 0;
                     releaseIndex < index;
                     ++releaseIndex)
                {
                    if (depthAcquired[releaseIndex])
                    {
                        depthTextures_[releaseIndex].keyedMutex->ReleaseSync(0);
                    }
                }
                for (std::size_t releaseIndex = 0;
                     releaseIndex < textures_.size();
                     ++releaseIndex)
                {
                    if (acquired[releaseIndex])
                        textures_[releaseIndex].keyedMutex->ReleaseSync(0);
                }
                return false;
            }
            depthAcquired[index] = true;
        }
    }

    bool ssgiFrameStarted = false;
    bool applyScreenSpaceGlobalIllumination = false;
    if (useDepth && screenSpaceGlobalIlluminationEnabled_)
    {
        ssgiFrameStarted = screenSpaceGlobalIllumination_.BeginFrame();
        applyScreenSpaceGlobalIllumination = ssgiFrameStarted;
        for (std::size_t eye = 0;
             eye < depthTextures_.size() &&
                 applyScreenSpaceGlobalIllumination;
             ++eye)
        {
            applyScreenSpaceGlobalIllumination =
                screenSpaceGlobalIllumination_.BuildEye(
                    eye,
                    textures_[eye].sharedView,
                    textures_[eye].sourceAlreadyLinear,
                    depthTextures_[eye].sharedView,
                    depthTextures_[eye].width,
                    depthTextures_[eye].height,
                    depthFrame->projections[eye]);
        }
        screenSpaceGlobalIllumination_.EndFrame();
    }

    bool applyWaterReflections =
        useDepth && waterReflectionsEnabled_ && waterMaskValid;
    for (std::size_t eye = 0;
         eye < depthTextures_.size() && applyWaterReflections;
         ++eye)
    {
        applyWaterReflections = waterReflection_.BuildEye(
            eye,
            textures_[eye].sharedView,
            textures_[eye].sourceAlreadyLinear,
            depthTextures_[eye].sharedView,
            depthTextures_[eye].width,
            depthTextures_[eye].height,
            depthFrame->projections[eye]);
    }

    bool aoFrameStarted = false;
    bool applyAmbientOcclusion = false;
    if (useDepth && ambientOcclusionEnabled_)
    {
        aoFrameStarted = ambientOcclusion_.BeginFrame();
        applyAmbientOcclusion = aoFrameStarted;
        for (std::size_t eye = 0;
             eye < depthTextures_.size() && applyAmbientOcclusion;
             ++eye)
        {
            applyAmbientOcclusion = ambientOcclusion_.BuildEye(
                eye,
                depthTextures_[eye].sharedView,
                depthTextures_[eye].width,
                depthTextures_[eye].height,
                depthFrame->projections[eye]);
        }
    }

    const bool bloomFrameStarted =
        worldBloomEnabled_ && scaler_.BeginBloomFrame();
    bool copied = true;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        if (applyAmbientOcclusion && index == 0)
            ambientOcclusion_.BeginApplicationTiming();
        const Texture& texture = textures_[index];
        if (texture.requiresScaling)
        {
            ID3D11ShaderResourceView* const aoView =
                applyAmbientOcclusion && index < depthTextures_.size()
                ? ambientOcclusion_.GetEyeView(index)
                : nullptr;
            ID3D11ShaderResourceView* const waterReflectionView =
                applyWaterReflections && index < depthTextures_.size()
                ? waterReflection_.GetEyeView(index)
                : nullptr;
            ID3D11ShaderResourceView* const ssgiView =
                applyScreenSpaceGlobalIllumination &&
                    index < depthTextures_.size()
                ? screenSpaceGlobalIllumination_.GetEyeView(index)
                : nullptr;
            copied = scaler_.ScaleAspectFit(
                texture.sharedView,
                texture.sourceWidth,
                texture.sourceHeight,
                texture.localTarget,
                texture.destinationWidth,
                texture.destinationHeight,
                texture.transparentPadding,
                texture.sourceAlreadyLinear,
                texture.applyAntialiasing,
                worldFxaaSharpeningStrength_,
                aoView,
                aoView != nullptr ? ambientOcclusionIntensity_ : 0.0F,
                ssgiView,
                ssgiView != nullptr
                    ? screenSpaceGlobalIlluminationIntensity_
                    : 0.0F,
                ssgiView != nullptr
                    ? screenSpaceGlobalIlluminationDebugMode_
                    : 0.0F,
                waterReflectionView,
                waterReflectionView != nullptr
                    ? waterReflectionIntensity_
                    : 0.0F,
                texture.applyBloom,
                worldBloomThreshold_,
                worldBloomIntensity_) && copied;
        }
        else
        {
            context_->CopyResource(texture.local, texture.shared);
        }
        if (applyAmbientOcclusion &&
            index + 1 == depthTextures_.size())
        {
            ambientOcclusion_.EndApplicationTiming();
        }
        if (bloomFrameStarted && index + 1 == kDepthTextureCount)
            scaler_.EndBloomFrame();
    }
    if (aoFrameStarted)
        ambientOcclusion_.EndFrame();
    if (useDepth && screenSpaceGlobalIlluminationEnabled_ &&
        !applyScreenSpaceGlobalIllumination)
    {
        ++screenSpaceGlobalIlluminationFrameFailures_;
        if (screenSpaceGlobalIlluminationFrameFailures_ <= 3)
        {
            WriteLog(
                L"Shared texture consumer rejected SSGI inputs for frame failure %ld; presenting color/UI without SSGI.",
                screenSpaceGlobalIlluminationFrameFailures_);
        }
    }
    if (useDepth && ambientOcclusionEnabled_ && !applyAmbientOcclusion)
    {
        ++ambientOcclusionFrameFailures_;
        if (ambientOcclusionFrameFailures_ <= 3)
        {
            WriteLog(
                L"Shared texture consumer rejected AO inputs for frame failure %ld; presenting color/UI without AO.",
                ambientOcclusionFrameFailures_);
        }
    }
    if (useDepth && waterReflectionsEnabled_ && waterMaskValid &&
        !applyWaterReflections)
    {
        ++waterReflectionFrameFailures_;
        if (waterReflectionFrameFailures_ <= 3)
        {
            WriteLog(
                L"Shared texture consumer rejected water surface-reflection inputs for frame failure %ld; presenting color/UI without water reflections.",
                waterReflectionFrameFailures_);
        }
    }
    const Texture& uiTexture =
        textures_[static_cast<std::size_t>(TextureSlot::Ref2Ui)];
    const bool overlayVisible =
        (frameOverlayFlags & kFrameOverlayBackToGameVisible) != 0;
    const bool overlayHovered = overlayVisible &&
        (frameOverlayFlags & kFrameOverlayBackToGameHovered) != 0;
    if (mainMenuOverlay_.IsReady() && !mainMenuOverlay_.Composite(
            uiTexture.localTarget,
            uiTexture.sourceWidth,
            uiTexture.sourceHeight,
            uiTexture.destinationWidth,
            uiTexture.destinationHeight,
            overlayVisible,
            overlayHovered))
    {
        copied = false;
    }
    if (legacyCompletionQuery_ != nullptr)
    {
        context_->End(legacyCompletionQuery_);
    }
    context_->Flush();

    bool completed = true;
    if (legacyCompletionQuery_ != nullptr)
    {
        completed = false;
        const DWORD waitStarted = GetTickCount();
        while (GetTickCount() - waitStarted < 1000)
        {
            const HRESULT result =
                context_->GetData(
                    legacyCompletionQuery_,
                    nullptr,
                    0,
                    0);
            if (result == S_OK)
            {
                completed = true;
                break;
            }
            if (FAILED(result))
            {
                WriteLog(
                    L"Shared texture consumer legacy-handle GPU completion query failed (HRESULT=0x%08lX).",
                    static_cast<unsigned long>(result));
                break;
            }
            Sleep(1);
        }
        if (!completed)
        {
            WriteLog(
                L"Shared texture consumer timed out waiting for legacy-handle GPU reads to complete.");
        }
    }
    if (aoFrameStarted)
        ambientOcclusion_.CollectFrameTimings();
    if (ssgiFrameStarted)
        screenSpaceGlobalIllumination_.CollectFrameTimings();
    if (bloomFrameStarted)
        scaler_.CollectBloomFrameTimings();

    bool released = true;
    for (std::size_t index = 0; index < depthTextures_.size(); ++index)
    {
        if (depthAcquired[index])
        {
            released = SUCCEEDED(
                depthTextures_[index].keyedMutex->ReleaseSync(0)) && released;
        }
    }
    for (Texture& texture : textures_)
    {
        if (texture.keyedMutex != nullptr)
        {
            released =
                SUCCEEDED(texture.keyedMutex->ReleaseSync(0)) && released;
        }
    }
    return copied && completed && released;
}

void SharedTextureConsumer::ApplySavedLiveSettings()
{
    const auto& runtime = settings::ProcessUserSettingsRuntime();
    if (!runtime.IsReady())
    {
        return;
    }
    const settings::UserSettingsValues values =
        settings::DecodeUserSettings(runtime.Current());
    if (worldFxaaEnabled_ != values.fxaaEnabled)
    {
        worldFxaaEnabled_ = values.fxaaEnabled;
        for (std::size_t index = 0; index < textures_.size(); ++index)
        {
            textures_[index].applyAntialiasing =
                worldFxaaEnabled_ &&
                index != static_cast<std::size_t>(TextureSlot::Ref2Ui);
        }
        WriteLog(
            L"Saved VR Settings applied world FXAA=%d without changing the Ref2 UI path.",
            worldFxaaEnabled_ ? 1 : 0);
    }
    const float fxaaSharpeningStrength =
        static_cast<float>(values.fxaaSharpeningPercent) / 100.0F;
    if (worldFxaaSharpeningStrength_ != fxaaSharpeningStrength)
    {
        worldFxaaSharpeningStrength_ = fxaaSharpeningStrength;
        WriteLog(
            L"Saved VR Settings applied FXAA sharpening %.2f without changing the Ref2 UI path.",
            fxaaSharpeningStrength);
    }
    const float aoRadius = static_cast<float>(
        values.ambientOcclusionRadiusCentimeters) / 100.0F;
    if (ambientOcclusionRadiusMeters_ != aoRadius)
    {
        ambientOcclusionRadiusMeters_ = aoRadius;
        ambientOcclusion_.SetViewRadiusMeters(aoRadius);
        WriteLog(
            L"Saved VR Settings applied AO radius %.2f m.",
            aoRadius);
    }
    const float aoIntensity = static_cast<float>(
        values.ambientOcclusionStrengthPercent) / 100.0F;
    if (ambientOcclusionIntensity_ != aoIntensity)
    {
        ambientOcclusionIntensity_ = aoIntensity;
        WriteLog(
            L"Saved VR Settings applied AO strength %.2f.",
            aoIntensity);
    }
    const float bloomThreshold = static_cast<float>(
        values.bloomThresholdPercent) / 100.0F;
    const float bloomIntensity = static_cast<float>(
        values.bloomIntensityPercent) / 100.0F;
    if (worldBloomThreshold_ != bloomThreshold ||
        worldBloomIntensity_ != bloomIntensity)
    {
        worldBloomThreshold_ = bloomThreshold;
        worldBloomIntensity_ = bloomIntensity;
        WriteLog(
            L"Saved VR Settings applied bloom threshold %.2f and intensity %.2f; the startup bloom enable state is unchanged.",
            bloomThreshold,
            bloomIntensity);
    }
}

OpenXRPresentationTextures SharedTextureConsumer::GetLocalTextures() const noexcept
{
    OpenXRPresentationTextures result = {};
    result.leftWorld = textures_[static_cast<std::size_t>(TextureSlot::LeftWorld)].local;
    result.rightWorld = textures_[static_cast<std::size_t>(TextureSlot::RightWorld)].local;
    result.ref2Ui = textures_[static_cast<std::size_t>(TextureSlot::Ref2Ui)].local;
    return result;
}

bool SharedTextureConsumer::ReadCenterPixels(DWORD* pixels, std::size_t count)
{
    if (pixels == nullptr || count < textures_.size())
    {
        return false;
    }
    bool read = true;
    for (std::size_t index = 0; index < textures_.size(); ++index)
    {
        read = ReadCenterPixel(textures_[index], pixels[index]) && read;
    }
    return read;
}

void SharedTextureConsumer::Shutdown()
{
    mainMenuOverlay_.Shutdown();
    ambientOcclusion_.Shutdown();
    screenSpaceGlobalIllumination_.Shutdown();
    waterReflection_.Shutdown();
    scaler_.Shutdown();
    scalerRequired_ = false;
    requiresLegacyCompletionWait_ = false;
    collectPerformanceDiagnostics_ = true;
    worldFxaaEnabled_ = true;
    worldFxaaSharpeningStrength_ = 0.25F;
    worldBloomEnabled_ = false;
    ambientOcclusionEnabled_ = false;
    screenSpaceGlobalIlluminationEnabled_ = false;
    waterReflectionsEnabled_ = false;
    ambientOcclusionIntensity_ = 1.0F;
    ambientOcclusionRadiusMeters_ = 0.60F;
    ambientOcclusionFrameFailures_ = 0;
    screenSpaceGlobalIlluminationIntensity_ = 0.65F;
    screenSpaceGlobalIlluminationDebugMode_ = 0.0F;
    screenSpaceGlobalIlluminationFrameFailures_ = 0;
    waterReflectionIntensity_ = 1.0F;
    waterReflectionFrameFailures_ = 0;
    worldBloomThreshold_ = 0.55F;
    worldBloomIntensity_ = 0.35F;
    for (Texture& texture : textures_)
    {
        ReleaseTexture(texture);
    }
    for (DepthTexture& texture : depthTextures_)
    {
        ReleaseDepthTexture(texture);
    }
    if (legacyCompletionQuery_ != nullptr)
    {
        legacyCompletionQuery_->Release();
        legacyCompletionQuery_ = nullptr;
    }
    if (context_ != nullptr)
    {
        context_->Release();
        context_ = nullptr;
    }
    if (device_ != nullptr)
    {
        device_->Release();
        device_ = nullptr;
    }
}

bool SharedTextureConsumer::OpenTexture(
    ID3D11Device1* device,
    std::size_t index,
    const SharedTextureDescription& description,
    UINT destinationWidth,
    UINT destinationHeight,
    DXGI_FORMAT destinationFormat)
{
    if (device == nullptr || index >= textures_.size() ||
        description.width == 0 || description.height == 0 ||
        destinationWidth == 0 || destinationHeight == 0 ||
        destinationFormat == DXGI_FORMAT_UNKNOWN ||
        description.format == DXGI_FORMAT_UNKNOWN)
    {
        return false;
    }

    Texture& texture = textures_[index];
    const SharedTextureTransport transport =
        static_cast<SharedTextureTransport>(description.transport);
    HRESULT result = E_INVALIDARG;
    if (transport == SharedTextureTransport::NamedNtHandle &&
        description.name[0] != L'\0')
    {
        result = device->OpenSharedResourceByName(
            description.name,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&texture.shared));
        if (SUCCEEDED(result))
        {
            result = texture.shared->QueryInterface(
                __uuidof(IDXGIKeyedMutex),
                reinterpret_cast<void**>(&texture.keyedMutex));
        }
    }
    else if (transport == SharedTextureTransport::D3D9LegacyHandle)
    {
        const HANDLE sharedHandle = LoadLegacySharedHandle(description);
        result = sharedHandle == nullptr
            ? E_INVALIDARG
            : device_->OpenSharedResource(
                sharedHandle,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture.shared));
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    if (SUCCEEDED(result))
    {
        texture.shared->GetDesc(&sourceDescription);
        if (sourceDescription.Width != description.width ||
            sourceDescription.Height != description.height ||
            sourceDescription.Format != static_cast<DXGI_FORMAT>(description.format) ||
            sourceDescription.SampleDesc.Count != 1 ||
            sourceDescription.ArraySize != 1)
        {
            result = E_INVALIDARG;
        }
    }
    const DXGI_FORMAT sourceFormat = sourceDescription.Format;
    if (SUCCEEDED(result))
    {
        if (!IsConvertibleSharedFormat(
                sourceFormat,
                destinationFormat))
        {
            result = E_INVALIDARG;
        }
    }
    if (SUCCEEDED(result))
    {
        result = device_->CreateShaderResourceView(
            texture.shared,
            nullptr,
            &texture.sharedView);
    }
    if (SUCCEEDED(result))
    {
        sourceDescription.Usage = D3D11_USAGE_DEFAULT;
        sourceDescription.Width = destinationWidth;
        sourceDescription.Height = destinationHeight;
        sourceDescription.Format = destinationFormat;
        sourceDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE |
            D3D11_BIND_RENDER_TARGET;
        sourceDescription.CPUAccessFlags = 0;
        sourceDescription.MiscFlags = 0;
        result = device_->CreateTexture2D(
            &sourceDescription,
            nullptr,
            &texture.local);
    }
    if (SUCCEEDED(result))
    {
        result = device_->CreateRenderTargetView(
            texture.local,
            nullptr,
            &texture.localTarget);
    }
    if (FAILED(result) || texture.local == nullptr)
    {
        WriteLog(
            L"Shared texture consumer could not open slot %zu '%s' (HRESULT=0x%08lX).",
            index,
            description.name,
            static_cast<unsigned long>(result));
        ReleaseTexture(texture);
        return false;
    }
    texture.sourceWidth = description.width;
    texture.sourceHeight = description.height;
    texture.destinationWidth = destinationWidth;
    texture.destinationHeight = destinationHeight;
    texture.requiresScaling =
        texture.sourceWidth != texture.destinationWidth ||
        texture.sourceHeight != texture.destinationHeight ||
        sourceFormat != destinationFormat ||
        !IsSrgbFormat(destinationFormat);
    texture.transparentPadding =
        index == static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.sourceAlreadyLinear = IsSrgbFormat(sourceFormat);
    texture.applyAntialiasing =
        worldFxaaEnabled_ &&
        index != static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.applyBloom =
        worldBloomEnabled_ &&
        index != static_cast<std::size_t>(TextureSlot::Ref2Ui);
    texture.requiresScaling =
        texture.requiresScaling || texture.applyAntialiasing || texture.applyBloom;
    scalerRequired_ = scalerRequired_ || texture.requiresScaling;
    WriteLog(
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? L"Shared texture consumer opened legacy D3D9 slot %zu handle=%p sourceFormat=%u destinationFormat=%u."
            : L"Shared texture consumer opened named keyed-mutex slot %zu '%s' sourceFormat=%u destinationFormat=%u.",
        index,
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? LoadLegacySharedHandle(description)
            : description.name,
        static_cast<unsigned int>(sourceFormat),
        static_cast<unsigned int>(destinationFormat));
    return true;
}

bool SharedTextureConsumer::OpenDepthTexture(
    ID3D11Device1* device,
    std::size_t index,
    const SharedTextureDescription& description)
{
    if (device == nullptr || index >= depthTextures_.size() ||
        description.width == 0 || description.height == 0 ||
        static_cast<DXGI_FORMAT>(description.format) !=
            DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        return false;
    }

    DepthTexture& texture = depthTextures_[index];
    const SharedTextureTransport transport =
        static_cast<SharedTextureTransport>(description.transport);
    HRESULT result = E_INVALIDARG;
    if (transport == SharedTextureTransport::NamedNtHandle &&
        description.name[0] != L'\0')
    {
        result = device->OpenSharedResourceByName(
            description.name,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&texture.shared));
        if (SUCCEEDED(result))
        {
            result = texture.shared->QueryInterface(
                __uuidof(IDXGIKeyedMutex),
                reinterpret_cast<void**>(&texture.keyedMutex));
        }
    }
    else if (transport == SharedTextureTransport::D3D9LegacyHandle)
    {
        const HANDLE sharedHandle = LoadLegacySharedHandle(description);
        result = sharedHandle == nullptr
            ? E_INVALIDARG
            : device_->OpenSharedResource(
                sharedHandle,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture.shared));
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    if (SUCCEEDED(result))
    {
        texture.shared->GetDesc(&sourceDescription);
        if (sourceDescription.Width != description.width ||
            sourceDescription.Height != description.height ||
            sourceDescription.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            sourceDescription.SampleDesc.Count != 1 ||
            sourceDescription.ArraySize != 1)
        {
            result = E_INVALIDARG;
        }
    }
    if (SUCCEEDED(result))
    {
        result = device_->CreateShaderResourceView(
            texture.shared,
            nullptr,
            &texture.sharedView);
    }
    if (FAILED(result) || texture.sharedView == nullptr)
    {
        WriteLog(
            L"Shared texture consumer could not open optional packed-depth slot %zu (HRESULT=0x%08lX).",
            index,
            static_cast<unsigned long>(result));
        ReleaseDepthTexture(texture);
        return false;
    }
    texture.width = sourceDescription.Width;
    texture.height = sourceDescription.Height;
    WriteLog(
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? L"Shared texture consumer opened legacy D3D9 packed-depth slot %zu handle=%p at %ux%u."
            : L"Shared texture consumer opened named packed-depth slot %zu '%s' at %ux%u.",
        index,
        transport == SharedTextureTransport::D3D9LegacyHandle
            ? LoadLegacySharedHandle(description)
            : description.name,
        texture.width,
        texture.height);
    return true;
}

bool SharedTextureConsumer::ReadCenterPixel(const Texture& texture, DWORD& pixel)
{
    if (device_ == nullptr || context_ == nullptr || texture.local == nullptr)
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC sourceDescription = {};
    texture.local->GetDesc(&sourceDescription);
    D3D11_TEXTURE2D_DESC stagingDescription = {};
    stagingDescription.Width = 1;
    stagingDescription.Height = 1;
    stagingDescription.MipLevels = 1;
    stagingDescription.ArraySize = 1;
    stagingDescription.Format = sourceDescription.Format;
    stagingDescription.SampleDesc.Count = 1;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;
    HRESULT result = device_->CreateTexture2D(
        &stagingDescription,
        nullptr,
        &staging);
    if (FAILED(result) || staging == nullptr)
    {
        return false;
    }

    const UINT centerX = sourceDescription.Width / 2;
    const UINT centerY = sourceDescription.Height / 2;
    const D3D11_BOX sourceBox = {
        centerX,
        centerY,
        0,
        centerX + 1,
        centerY + 1,
        1};
    context_->CopySubresourceRegion(staging, 0, 0, 0, 0, texture.local, 0, &sourceBox);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(result) && mapped.pData != nullptr && mapped.RowPitch >= sizeof(DWORD))
    {
        pixel = *static_cast<const DWORD*>(mapped.pData);
        context_->Unmap(staging, 0);
    }
    staging->Release();
    return SUCCEEDED(result);
}

void SharedTextureConsumer::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr)
    {
        return;
    }
    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message);
}

void SharedTextureConsumer::ReleaseTexture(Texture& texture)
{
    if (texture.localTarget != nullptr)
    {
        texture.localTarget->Release();
        texture.localTarget = nullptr;
    }
    if (texture.local != nullptr)
    {
        texture.local->Release();
        texture.local = nullptr;
    }
    if (texture.keyedMutex != nullptr)
    {
        texture.keyedMutex->Release();
        texture.keyedMutex = nullptr;
    }
    if (texture.sharedView != nullptr)
    {
        texture.sharedView->Release();
        texture.sharedView = nullptr;
    }
    if (texture.shared != nullptr)
    {
        texture.shared->Release();
        texture.shared = nullptr;
    }
    texture.sourceWidth = 0;
    texture.sourceHeight = 0;
    texture.destinationWidth = 0;
    texture.destinationHeight = 0;
    texture.requiresScaling = false;
    texture.transparentPadding = false;
    texture.sourceAlreadyLinear = false;
    texture.applyAntialiasing = false;
    texture.applyBloom = false;
}

void SharedTextureConsumer::ReleaseDepthTexture(DepthTexture& texture)
{
    if (texture.keyedMutex != nullptr)
    {
        texture.keyedMutex->Release();
        texture.keyedMutex = nullptr;
    }
    if (texture.sharedView != nullptr)
    {
        texture.sharedView->Release();
        texture.sharedView = nullptr;
    }
    if (texture.shared != nullptr)
    {
        texture.shared->Release();
        texture.shared = nullptr;
    }
    texture.width = 0;
    texture.height = 0;
}
} // namespace bfvr::shared
