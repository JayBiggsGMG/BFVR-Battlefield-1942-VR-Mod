#include "openxr/OpenXRPresentation.h"
#include "openxr/OpenXRHapticOutput.h"
#include <windows.h>
#include <dxgi1_2.h>
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "openxr/OpenXRApiVersion.h"
#include "openxr/OpenXRComfortVignette.h"
#include "openxr/OpenXRControllerBindingPolicy.h"
#include "openxr/OpenXRControllerShortcutPolicy.h"
#include "openxr/OpenXRQuickMenu.h"
#include "openxr/OpenXRPresentationSupport.h"
#include "openxr/OpenXRScopeOverlayLayer.h"
#include "openxr/OpenXRTrackingBasis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace
{
constexpr float kMinimumUiDistanceMeters = 0.1F;
constexpr float kMinimumUiWidthMeters = 0.1F;
constexpr float kMinimumCylinderAngleRadians = 0.1F;
constexpr float kMaximumCylinderAngleRadians = 6.20F;
} // namespace

namespace bfvr
{
class OpenXRPresentation::Impl
{
public:
    struct Swapchain
    {
        XrSwapchain handle = XR_NULL_HANDLE;
        UINT width = 0;
        UINT height = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
    };

    struct Api
    {
        PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
        PFN_xrCreateInstance createInstance = nullptr;
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrGetInstanceProperties getInstanceProperties = nullptr;
        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrStringToPath stringToPath = nullptr;
        PFN_xrPathToString pathToString = nullptr;
        PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements = nullptr;
        PFN_xrCreateSession createSession = nullptr;
        PFN_xrDestroySession destroySession = nullptr;
        PFN_xrPollEvent pollEvent = nullptr;
        PFN_xrBeginSession beginSession = nullptr;
        PFN_xrEndSession endSession = nullptr;
        PFN_xrRequestExitSession requestExitSession = nullptr;
        PFN_xrEnumerateViewConfigurationViews enumerateViewConfigurationViews = nullptr;
        PFN_xrEnumerateEnvironmentBlendModes enumerateEnvironmentBlendModes = nullptr;
        PFN_xrCreateReferenceSpace createReferenceSpace = nullptr;
        PFN_xrDestroySpace destroySpace = nullptr;
        PFN_xrCreateSwapchain createSwapchain = nullptr;
        PFN_xrDestroySwapchain destroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats = nullptr;
        PFN_xrEnumerateSwapchainImages enumerateSwapchainImages = nullptr;
        PFN_xrAcquireSwapchainImage acquireSwapchainImage = nullptr;
        PFN_xrWaitSwapchainImage waitSwapchainImage = nullptr;
        PFN_xrReleaseSwapchainImage releaseSwapchainImage = nullptr;
        PFN_xrWaitFrame waitFrame = nullptr;
        PFN_xrBeginFrame beginFrame = nullptr;
        PFN_xrEndFrame endFrame = nullptr;
        PFN_xrLocateViews locateViews = nullptr;
        PFN_xrCreateActionSet createActionSet = nullptr;
        PFN_xrDestroyActionSet destroyActionSet = nullptr;
        PFN_xrCreateAction createAction = nullptr;
        PFN_xrSuggestInteractionProfileBindings suggestInteractionProfileBindings = nullptr;
        PFN_xrAttachSessionActionSets attachSessionActionSets = nullptr;
        PFN_xrSyncActions syncActions = nullptr;
        PFN_xrGetActionStatePose getActionStatePose = nullptr;
        PFN_xrGetActionStateFloat getActionStateFloat = nullptr;
        PFN_xrGetActionStateVector2f getActionStateVector2f = nullptr;
        PFN_xrGetActionStateBoolean getActionStateBoolean = nullptr;
        PFN_xrCreateActionSpace createActionSpace = nullptr;
        PFN_xrLocateSpace locateSpace = nullptr;
        PFN_xrGetCurrentInteractionProfile getCurrentInteractionProfile = nullptr;
        PFN_xrApplyHapticFeedback applyHapticFeedback = nullptr;
    };
    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr)
        {
            return;
        }
        wchar_t message[1024] = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        logCallback(logContext, message);
    }
    template <typename T>
    bool ResolveGlobal(const char* name, T& function)
    {
        if (api.getInstanceProcAddr == nullptr)
        {
            return false;
        }
        PFN_xrVoidFunction rawFunction = nullptr;
        const XrResult result = api.getInstanceProcAddr(XR_NULL_HANDLE, name, &rawFunction);
        if (XR_FAILED(result) || rawFunction == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not resolve global %S (%s, %ld).",
                name,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        function = reinterpret_cast<T>(rawFunction);
        return true;
    }
    template <typename T>
    bool ResolveInstance(const char* name, T& function)
    {
        if (api.getInstanceProcAddr == nullptr || instance == XR_NULL_HANDLE)
        {
            return false;
        }
        PFN_xrVoidFunction rawFunction = nullptr;
        const XrResult result = api.getInstanceProcAddr(instance, name, &rawFunction);
        if (XR_FAILED(result) || rawFunction == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not resolve instance function %S (%s, %ld).",
                name,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        function = reinterpret_cast<T>(rawFunction);
        return true;
    }

    bool LoadLoader(const wchar_t* payloadDirectory)
    {
        const std::wstring loaderPath = BuildOpenXRLoaderPath(payloadDirectory);
        if (loaderPath.empty())
        {
            WriteLog(L"OpenXR presentation skipped because the BFVR payload directory is unavailable.");
            return false;
        }

        loader = LoadLibraryW(loaderPath.c_str());
        if (loader == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not load the pinned loader from %s (error %lu).",
                loaderPath.c_str(),
                GetLastError());
            return false;
        }

        api.getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
            GetProcAddress(loader, "xrGetInstanceProcAddr"));
        if (api.getInstanceProcAddr == nullptr ||
            !ResolveGlobal("xrCreateInstance", api.createInstance))
        {
            WriteLog(L"OpenXR presentation loader does not expose its required entry points.");
            return false;
        }
        return true;
    }

    bool EnumerateExtensions(
        bool& d3d11Available,
        bool& cylinderAvailable,
        bool& localFloorExtensionAvailable)
    {
        d3d11Available = false;
        cylinderAvailable = false;
        localFloorExtensionAvailable = false;
        PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions = nullptr;
        if (!ResolveGlobal("xrEnumerateInstanceExtensionProperties", enumerateExtensions))
        {
            return false;
        }

        uint32_t extensionCount = 0;
        XrResult result = enumerateExtensions(nullptr, 0, &extensionCount, nullptr);
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation extension-count query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        std::vector<XrExtensionProperties> extensions(extensionCount);
        for (XrExtensionProperties& extension : extensions)
        {
            extension.type = XR_TYPE_EXTENSION_PROPERTIES;
            extension.next = nullptr;
        }
        result = enumerateExtensions(nullptr, extensionCount, &extensionCount, extensions.data());
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation extension-list query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        for (const XrExtensionProperties& extension : extensions)
        {
            d3d11Available = d3d11Available ||
                std::strcmp(extension.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
            cylinderAvailable = cylinderAvailable ||
                std::strcmp(
                    extension.extensionName,
                    XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) == 0;
            localFloorExtensionAvailable = localFloorExtensionAvailable ||
                std::strcmp(
                    extension.extensionName,
                    XR_EXT_LOCAL_FLOOR_EXTENSION_NAME) == 0;
        }
        WriteLog(
            L"OpenXR presentation runtime exposes %u extensions; D3D11=%s cylinderLayer=%s localFloor=%s.",
            extensionCount,
            d3d11Available ? L"available" : L"unavailable",
            cylinderAvailable ? L"available" : L"unavailable",
            localFloorExtensionAvailable ? L"available" : L"unavailable");
        return true;
    }

    XrInstanceCreateInfo BuildInstanceCreateInfo(
        const std::vector<const char*>& enabledExtensions) const
    {
        XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(
            createInfo.applicationInfo.applicationName,
            "Battlefield 1942 VR");
        createInfo.applicationInfo.applicationVersion = 1;
        strcpy_s(createInfo.applicationInfo.engineName, "BFVR");
        createInfo.applicationInfo.engineVersion = 1;
        createInfo.applicationInfo.apiVersion =
            kRequestedOpenXRApiVersion;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        createInfo.enabledExtensionNames = enabledExtensions.empty()
            ? nullptr
            : enabledExtensions.data();
        return createInfo;
    }

    bool CreateBaselineInstanceAndSystem()
    {
        const std::vector<const char*> noExtensions;
        const XrInstanceCreateInfo createInfo = BuildInstanceCreateInfo(noExtensions);
        XrInstance baselineInstance = XR_NULL_HANDLE;
        const XrResult createResult = api.createInstance(&createInfo, &baselineInstance);
        if (XR_FAILED(createResult) || baselineInstance == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR baseline instance creation with requested API %u.%u.%u failed (%s, %ld); flat fallback remains active.",
                static_cast<unsigned>(XR_VERSION_MAJOR(
                    kRequestedOpenXRApiVersion)),
                static_cast<unsigned>(XR_VERSION_MINOR(
                    kRequestedOpenXRApiVersion)),
                static_cast<unsigned>(XR_VERSION_PATCH(
                    kRequestedOpenXRApiVersion)),
                DescribeOpenXRResult(createResult),
                static_cast<long>(createResult));
            return false;
        }
        WriteLog(
            L"OpenXR baseline instance created with requested API %u.%u.%u.",
            static_cast<unsigned>(XR_VERSION_MAJOR(
                kRequestedOpenXRApiVersion)),
            static_cast<unsigned>(XR_VERSION_MINOR(
                kRequestedOpenXRApiVersion)),
            static_cast<unsigned>(XR_VERSION_PATCH(
                kRequestedOpenXRApiVersion)));

        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrDestroyInstance destroyInstance = nullptr;
        const bool functionsResolved =
            ResolveInstanceFor(baselineInstance, "xrGetSystem", getSystem) &&
            ResolveInstanceFor(baselineInstance, "xrDestroyInstance", destroyInstance);
        if (!functionsResolved)
        {
            if (destroyInstance != nullptr)
            {
                destroyInstance(baselineInstance);
            }
            return false;
        }

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId baselineSystemId = XR_NULL_SYSTEM_ID;
        const XrResult systemResult = getSystem(baselineInstance, &systemInfo, &baselineSystemId);
        const XrResult destroyResult = destroyInstance(baselineInstance);
        if (XR_FAILED(destroyResult))
        {
            WriteLog(
                L"OpenXR baseline instance destruction failed (%s, %ld).",
                DescribeOpenXRResult(destroyResult),
                static_cast<long>(destroyResult));
        }
        if (XR_FAILED(systemResult) || baselineSystemId == XR_NULL_SYSTEM_ID)
        {
            WriteLog(
                L"OpenXR HMD system query failed (%s, %ld); a physical headset/runtime is required before session creation.",
                DescribeOpenXRResult(systemResult),
                static_cast<long>(systemResult));
            return false;
        }

        WriteLog(
            L"OpenXR HMD system is available (id=%llu); proceeding to the D3D11 adapter/session path.",
            static_cast<unsigned long long>(baselineSystemId));
        return true;
    }

    template <typename T>
    bool ResolveInstanceFor(XrInstance targetInstance, const char* name, T& function)
    {
        if (api.getInstanceProcAddr == nullptr || targetInstance == XR_NULL_HANDLE)
        {
            return false;
        }

        PFN_xrVoidFunction rawFunction = nullptr;
        const XrResult result = api.getInstanceProcAddr(targetInstance, name, &rawFunction);
        if (XR_FAILED(result) || rawFunction == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not resolve %S on its temporary instance (%s, %ld).",
                name,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        function = reinterpret_cast<T>(rawFunction);
        return true;
    }

    bool ResolvePresentationFunctions()
    {
        return
            ResolveInstance("xrDestroyInstance", api.destroyInstance) &&
            ResolveInstance("xrGetInstanceProperties", api.getInstanceProperties) &&
            ResolveInstance("xrGetSystem", api.getSystem) &&
            ResolveInstance("xrStringToPath", api.stringToPath) &&
            ResolveInstance("xrPathToString", api.pathToString) &&
            ResolveInstance(
                "xrGetD3D11GraphicsRequirementsKHR",
                api.getD3D11GraphicsRequirements) &&
            ResolveInstance("xrCreateSession", api.createSession) &&
            ResolveInstance("xrDestroySession", api.destroySession) &&
            ResolveInstance("xrPollEvent", api.pollEvent) &&
            ResolveInstance("xrBeginSession", api.beginSession) &&
            ResolveInstance("xrEndSession", api.endSession) &&
            ResolveInstance("xrRequestExitSession", api.requestExitSession) &&
            ResolveInstance(
                "xrEnumerateViewConfigurationViews",
                api.enumerateViewConfigurationViews) &&
            ResolveInstance(
                "xrEnumerateEnvironmentBlendModes",
                api.enumerateEnvironmentBlendModes) &&
            ResolveInstance("xrCreateReferenceSpace", api.createReferenceSpace) &&
            ResolveInstance("xrDestroySpace", api.destroySpace) &&
            ResolveInstance("xrCreateSwapchain", api.createSwapchain) &&
            ResolveInstance("xrDestroySwapchain", api.destroySwapchain) &&
            ResolveInstance("xrEnumerateSwapchainFormats", api.enumerateSwapchainFormats) &&
            ResolveInstance("xrEnumerateSwapchainImages", api.enumerateSwapchainImages) &&
            ResolveInstance("xrAcquireSwapchainImage", api.acquireSwapchainImage) &&
            ResolveInstance("xrWaitSwapchainImage", api.waitSwapchainImage) &&
            ResolveInstance("xrReleaseSwapchainImage", api.releaseSwapchainImage) &&
            ResolveInstance("xrWaitFrame", api.waitFrame) &&
            ResolveInstance("xrBeginFrame", api.beginFrame) &&
            ResolveInstance("xrEndFrame", api.endFrame) &&
            ResolveInstance("xrLocateViews", api.locateViews) &&
            ResolveInstance("xrCreateActionSet", api.createActionSet) &&
            ResolveInstance("xrDestroyActionSet", api.destroyActionSet) &&
            ResolveInstance("xrCreateAction", api.createAction) &&
            ResolveInstance(
                "xrSuggestInteractionProfileBindings",
                api.suggestInteractionProfileBindings) &&
            ResolveInstance(
                "xrAttachSessionActionSets",
                api.attachSessionActionSets) &&
            ResolveInstance("xrSyncActions", api.syncActions) &&
            ResolveInstance("xrGetActionStatePose", api.getActionStatePose) &&
            ResolveInstance("xrGetActionStateFloat", api.getActionStateFloat) &&
            ResolveInstance(
                "xrGetActionStateVector2f",
                api.getActionStateVector2f) &&
            ResolveInstance(
                "xrGetActionStateBoolean",
                api.getActionStateBoolean) &&
            ResolveInstance("xrCreateActionSpace", api.createActionSpace) &&
            ResolveInstance("xrLocateSpace", api.locateSpace) &&
            ResolveInstance(
                "xrGetCurrentInteractionProfile",
                api.getCurrentInteractionProfile) &&
            ResolveInstance("xrApplyHapticFeedback", api.applyHapticFeedback);
    }

    bool CreateRuntimeAdapterDevice(const XrGraphicsRequirementsD3D11KHR& requirements)
    {
        IDXGIFactory1* factory = nullptr;
        HRESULT factoryResult = CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory));
        if (FAILED(factoryResult) || factory == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not create a DXGI factory (HRESULT=0x%08lX).",
                static_cast<unsigned long>(factoryResult));
            return false;
        }

        IDXGIAdapter1* selectedAdapter = nullptr;
        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            IDXGIAdapter1* candidate = nullptr;
            const HRESULT enumerateResult = factory->EnumAdapters1(adapterIndex, &candidate);
            if (enumerateResult == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (FAILED(enumerateResult) || candidate == nullptr)
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 description = {};
            const HRESULT descriptionResult = candidate->GetDesc1(&description);
            if (SUCCEEDED(descriptionResult) &&
                EqualLuid(description.AdapterLuid, requirements.adapterLuid))
            {
                selectedAdapter = candidate;
                break;
            }
            candidate->Release();
        }
        factory->Release();

        if (selectedAdapter == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not find the runtime-selected adapter LUID=%08lX:%08lX; it will not substitute another adapter.",
                static_cast<unsigned long>(requirements.adapterLuid.HighPart),
                static_cast<unsigned long>(requirements.adapterLuid.LowPart));
            return false;
        }

        constexpr std::array<D3D_FEATURE_LEVEL, 6> supportedFeatureLevels = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
        std::vector<D3D_FEATURE_LEVEL> requestedFeatureLevels;
        for (const D3D_FEATURE_LEVEL featureLevel : supportedFeatureLevels)
        {
            if (featureLevel >= requirements.minFeatureLevel)
            {
                requestedFeatureLevels.push_back(featureLevel);
            }
        }
        if (requestedFeatureLevels.empty())
        {
            selectedAdapter->Release();
            WriteLog(
                L"OpenXR presentation has no D3D11 feature level at or above the runtime requirement 0x%04X.",
                static_cast<unsigned int>(requirements.minFeatureLevel));
            return false;
        }

        const HRESULT deviceResult = D3D11CreateDevice(
            selectedAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            requestedFeatureLevels.data(),
            static_cast<UINT>(requestedFeatureLevels.size()),
            D3D11_SDK_VERSION,
            &device,
            &deviceFeatureLevel,
            &context);
        selectedAdapter->Release();
        if (FAILED(deviceResult) || device == nullptr || context == nullptr)
        {
            WriteLog(
                L"OpenXR presentation could not create a D3D11 device on the runtime-selected adapter (HRESULT=0x%08lX).",
                static_cast<unsigned long>(deviceResult));
            return false;
        }
        if (deviceFeatureLevel < requirements.minFeatureLevel)
        {
            WriteLog(
                L"OpenXR presentation device feature level 0x%04X is below the runtime requirement 0x%04X.",
                static_cast<unsigned int>(deviceFeatureLevel),
                static_cast<unsigned int>(requirements.minFeatureLevel));
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* deviceAdapter = nullptr;
        DXGI_ADAPTER_DESC deviceAdapterDescription = {};
        const HRESULT dxgiDeviceResult = device->QueryInterface(
            __uuidof(IDXGIDevice),
            reinterpret_cast<void**>(&dxgiDevice));
        const HRESULT deviceAdapterResult = SUCCEEDED(dxgiDeviceResult)
            ? dxgiDevice->GetAdapter(&deviceAdapter)
            : E_FAIL;
        const HRESULT deviceAdapterDescriptionResult =
            SUCCEEDED(deviceAdapterResult) && deviceAdapter != nullptr
            ? deviceAdapter->GetDesc(&deviceAdapterDescription)
            : E_FAIL;
        if (deviceAdapter != nullptr)
        {
            deviceAdapter->Release();
        }
        if (dxgiDevice != nullptr)
        {
            dxgiDevice->Release();
        }
        if (FAILED(deviceAdapterDescriptionResult) ||
            !EqualLuid(deviceAdapterDescription.AdapterLuid, requirements.adapterLuid))
        {
            WriteLog(
                L"OpenXR presentation device-adapter verification failed (device=0x%08lX adapter=0x%08lX desc=0x%08lX); it will not create a session.",
                static_cast<unsigned long>(dxgiDeviceResult),
                static_cast<unsigned long>(deviceAdapterResult),
                static_cast<unsigned long>(deviceAdapterDescriptionResult));
            return false;
        }

        WriteLog(
            L"OpenXR presentation created D3D11 device=%p on verified adapter '%s' LUID=%08lX:%08lX at feature level 0x%04X.",
            device,
            deviceAdapterDescription.Description,
            static_cast<unsigned long>(deviceAdapterDescription.AdapterLuid.HighPart),
            static_cast<unsigned long>(deviceAdapterDescription.AdapterLuid.LowPart),
            static_cast<unsigned int>(deviceFeatureLevel));
        textureRequirements.deviceFeatureLevel = deviceFeatureLevel;
        return true;
    }

    bool ChooseSwapchainFormat()
    {
        uint32_t formatCount = 0;
        XrResult result = api.enumerateSwapchainFormats(session, 0, &formatCount, nullptr);
        if (XR_FAILED(result) || formatCount == 0)
        {
            WriteLog(
                L"OpenXR presentation swapchain-format count query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        std::vector<int64_t> formats(formatCount);
        result = api.enumerateSwapchainFormats(
            session,
            formatCount,
            &formatCount,
            formats.data());
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation swapchain-format query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        // BF1942's legacy backbuffer contains display-encoded color values.
        // Prefer an sRGB swapchain so the OpenXR compositor linearizes those
        // values as required by the specification. The scaler supplies
        // explicit linear values for the UNORM fallback.
        constexpr std::array<DXGI_FORMAT, 4> preferredFormats = {
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM};
        for (const DXGI_FORMAT preferredFormat : preferredFormats)
        {
            const int64_t value = static_cast<int64_t>(preferredFormat);
            if (std::find(formats.begin(), formats.end(), value) != formats.end())
            {
                swapchainFormat = preferredFormat;
                WriteLog(
                    L"OpenXR presentation selected swapchain format %d.",
                    static_cast<int>(swapchainFormat));
                return true;
            }
        }

        WriteLog(
            L"OpenXR presentation requires an sRGB or UNORM BGRA/RGBA 8-bit swapchain format, none of which the runtime exposes.");
        return false;
    }

    bool CreateSwapchain(Swapchain& swapchain, UINT width, UINT height)
    {
        XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.format = static_cast<int64_t>(swapchainFormat);
        createInfo.sampleCount = 1;
        createInfo.width = width;
        createInfo.height = height;
        createInfo.faceCount = 1;
        createInfo.arraySize = 1;
        createInfo.mipCount = 1;
        XrResult result = api.createSwapchain(session, &createInfo, &swapchain.handle);
        if (XR_FAILED(result) || swapchain.handle == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR presentation swapchain creation failed for %ux%u (%s, %ld).",
                width,
                height,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        uint32_t imageCount = 0;
        result = api.enumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr);
        if (XR_FAILED(result) || imageCount == 0)
        {
            WriteLog(
                L"OpenXR presentation swapchain-image count query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        swapchain.images.resize(imageCount);
        for (XrSwapchainImageD3D11KHR& image : swapchain.images)
        {
            image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            image.next = nullptr;
        }
        result = api.enumerateSwapchainImages(
            swapchain.handle,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.images.data()));
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation swapchain-image enumeration failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        swapchain.width = width;
        swapchain.height = height;
        return true;
    }

    bool StringToPath(const char* text, XrPath& path)
    {
        path = XR_NULL_PATH;
        const XrResult result = api.stringToPath(instance, text, &path);
        if (XR_FAILED(result) || path == XR_NULL_PATH)
        {
            WriteLog(
                L"OpenXR controller input could not resolve path %S (%s, %ld).",
                text,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        return true;
    }

    bool CreateControllerAction(
        XrAction& action,
        const char* actionName,
        const char* localizedActionName,
        XrActionType actionType)
    {
        XrActionCreateInfo createInfo{XR_TYPE_ACTION_CREATE_INFO};
        strcpy_s(createInfo.actionName, actionName);
        strcpy_s(createInfo.localizedActionName, localizedActionName);
        createInfo.actionType = actionType;
        createInfo.countSubactionPaths =
            static_cast<uint32_t>(controllerUserPaths.size());
        createInfo.subactionPaths = controllerUserPaths.data();
        const XrResult result = api.createAction(
            controllerActionSet,
            &createInfo,
            &action);
        if (XR_FAILED(result) || action == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR controller input could not create action %S (%s, %ld).",
                actionName,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        return true;
    }

    bool AddSuggestedBinding(
        std::vector<XrActionSuggestedBinding>& bindings,
        XrAction action,
        const char* bindingPath)
    {
        XrPath path = XR_NULL_PATH;
        if (!StringToPath(bindingPath, path))
        {
            return false;
        }
        XrActionSuggestedBinding binding = {};
        binding.action = action;
        binding.binding = path;
        bindings.push_back(binding);
        return true;
    }

    XrAction ControllerAction(OpenXRControllerAction action) const noexcept
    {
        switch (action)
        {
        case OpenXRControllerAction::AimPose:
            return controllerAimAction;
        case OpenXRControllerAction::GripPose:
            return controllerGripAction;
        case OpenXRControllerAction::Trigger:
            return controllerTriggerAction;
        case OpenXRControllerAction::Squeeze:
            return controllerSqueezeAction;
        case OpenXRControllerAction::MovementTurnAxis:
            return controllerThumbstickAction;
        case OpenXRControllerAction::AxisClick:
            return controllerThumbstickClickAction;
        case OpenXRControllerAction::PrimaryFace:
            return controllerPrimaryAction;
        case OpenXRControllerAction::SecondaryFace:
            return controllerSecondaryAction;
        case OpenXRControllerAction::Menu:
            return controllerMenuAction;
        case OpenXRControllerAction::Haptic:
            return controllerHapticAction;
        }
        return XR_NULL_HANDLE;
    }

    void SuggestControllerBindings(
        const char* interactionProfile,
        const std::vector<XrActionSuggestedBinding>& bindings)
    {
        if (bindings.empty())
        {
            return;
        }
        XrPath profilePath = XR_NULL_PATH;
        if (!StringToPath(interactionProfile, profilePath))
        {
            return;
        }
        XrInteractionProfileSuggestedBinding suggestion{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile = profilePath;
        suggestion.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggestion.suggestedBindings = bindings.data();
        const XrResult result = api.suggestInteractionProfileBindings(
            instance,
            &suggestion);
        if (XR_SUCCEEDED(result))
        {
            WriteLog(
                L"OpenXR controller input suggested %u bindings for %S.",
                static_cast<unsigned int>(bindings.size()),
                interactionProfile);
        }
        else
        {
            WriteLog(
                L"OpenXR controller input could not suggest bindings for %S (%s, %ld).",
                interactionProfile,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
        }
    }

    void SuggestControllerBindingProfiles()
    {
        for (const OpenXRControllerProfileBindings& profile :
             OpenXRControllerBindingProfiles())
        {
            std::vector<XrActionSuggestedBinding> bindings;
            bindings.reserve(profile.bindings.size());
            for (const OpenXRControllerBindingSeed& seed : profile.bindings)
            {
                const XrAction action = ControllerAction(seed.action);
                const char* const hand =
                    seed.hand == OpenXRControllerHand::Left
                    ? "left"
                    : "right";
                if (action == XR_NULL_HANDLE ||
                    seed.componentPath == nullptr)
                {
                    continue;
                }
                const std::string bindingPath =
                    std::string("/user/hand/") + hand +
                    seed.componentPath;
                (void)AddSuggestedBinding(
                    bindings,
                    action,
                    bindingPath.c_str());
            }
            SuggestControllerBindings(
                profile.interactionProfile,
                bindings);
        }
    }

    bool CreateControllerPoseSpace(
        XrAction action,
        XrPath handPath,
        XrSpace& space,
        const wchar_t* label)
    {
        XrActionSpaceCreateInfo createInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        createInfo.action = action;
        createInfo.subactionPath = handPath;
        createInfo.poseInActionSpace.orientation.w = 1.0F;
        const XrResult result = api.createActionSpace(session, &createInfo, &space);
        if (XR_FAILED(result) || space == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR controller input could not create the %s action space (%s, %ld).",
                label,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        return true;
    }

    void DestroyControllerInput()
    {
        for (XrSpace& space : controllerAimSpaces)
        {
            if (space != XR_NULL_HANDLE && api.destroySpace != nullptr)
            {
                api.destroySpace(space);
            }
            space = XR_NULL_HANDLE;
        }
        for (XrSpace& space : controllerGripSpaces)
        {
            if (space != XR_NULL_HANDLE && api.destroySpace != nullptr)
            {
                api.destroySpace(space);
            }
            space = XR_NULL_HANDLE;
        }
        if (controllerActionSet != XR_NULL_HANDLE &&
            api.destroyActionSet != nullptr)
        {
            api.destroyActionSet(controllerActionSet);
        }
        controllerActionSet = XR_NULL_HANDLE;
        controllerAimAction = XR_NULL_HANDLE;
        controllerGripAction = XR_NULL_HANDLE;
        controllerTriggerAction = XR_NULL_HANDLE;
        controllerSqueezeAction = XR_NULL_HANDLE;
        controllerThumbstickAction = XR_NULL_HANDLE;
        controllerThumbstickClickAction = XR_NULL_HANDLE;
        controllerPrimaryAction = XR_NULL_HANDLE;
        controllerSecondaryAction = XR_NULL_HANDLE;
        controllerMenuAction = XR_NULL_HANDLE;
        controllerHapticAction = XR_NULL_HANDLE;
        controllerInputAttached = false;
        controllerUserPaths = {};
        controllerInteractionProfiles = {};
        lastControllerSyncResult = XR_SUCCESS;
    }

    bool InitializeControllerInput()
    {
        DestroyControllerInput();
        if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE ||
            trackingBasis.ApplicationSpace() == XR_NULL_HANDLE)
        {
            return false;
        }

        if (!StringToPath("/user/hand/left", controllerUserPaths[0]) ||
            !StringToPath("/user/hand/right", controllerUserPaths[1]))
        {
            return false;
        }
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(actionSetInfo.actionSetName, "bfvr_controller");
        strcpy_s(actionSetInfo.localizedActionSetName, "BFVR controller input");
        actionSetInfo.priority = 0;
        XrResult result = api.createActionSet(
            instance,
            &actionSetInfo,
            &controllerActionSet);
        if (XR_FAILED(result) || controllerActionSet == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR controller input could not create its action set (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            DestroyControllerInput();
            return false;
        }

        const bool actionsCreated =
            CreateControllerAction(
                controllerAimAction,
                "bfvr_aim_pose",
                "BFVR aim pose",
                XR_ACTION_TYPE_POSE_INPUT) &&
            CreateControllerAction(
                controllerGripAction,
                "bfvr_grip_pose",
                "BFVR grip pose",
                XR_ACTION_TYPE_POSE_INPUT) &&
            CreateControllerAction(
                controllerTriggerAction,
                "bfvr_trigger",
                "Trigger (left use / right fire)",
                XR_ACTION_TYPE_FLOAT_INPUT) &&
            CreateControllerAction(
                controllerSqueezeAction,
                "bfvr_squeeze",
                "Squeeze (left support / right alt-fire)",
                XR_ACTION_TYPE_FLOAT_INPUT) &&
            CreateControllerAction(
                controllerThumbstickAction,
                "bfvr_thumbstick",
                "Stick/trackpad (left move / right turn-aim)",
                XR_ACTION_TYPE_VECTOR2F_INPUT) &&
            CreateControllerAction(
                controllerThumbstickClickAction,
                "bfvr_thumbstick_click",
                "Stick/trackpad click (context action)",
                XR_ACTION_TYPE_BOOLEAN_INPUT) &&
            CreateControllerAction(
                controllerPrimaryAction,
                "bfvr_primary",
                "Primary face (left prone / right Quick Menu)",
                XR_ACTION_TYPE_BOOLEAN_INPUT) &&
            CreateControllerAction(
                controllerSecondaryAction,
                "bfvr_secondary",
                "Secondary face (left scoreboard / right reload)",
                XR_ACTION_TYPE_BOOLEAN_INPUT) &&
            CreateControllerAction(
                controllerMenuAction,
                "bfvr_menu",
                "Map toggle",
                XR_ACTION_TYPE_BOOLEAN_INPUT) &&
            CreateControllerAction(
                controllerHapticAction,
                "bfvr_haptic",
                "Controller haptics",
                XR_ACTION_TYPE_VIBRATION_OUTPUT);
        if (!actionsCreated)
        {
            DestroyControllerInput();
            return false;
        }

        SuggestControllerBindingProfiles();

        XrSessionActionSetsAttachInfo attachInfo{
            XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &controllerActionSet;
        result = api.attachSessionActionSets(session, &attachInfo);
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR controller input could not attach its action set (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            DestroyControllerInput();
            return false;
        }

        const bool spacesCreated =
            CreateControllerPoseSpace(
                controllerAimAction,
                controllerUserPaths[0],
                controllerAimSpaces[0],
                L"left aim") &&
            CreateControllerPoseSpace(
                controllerAimAction,
                controllerUserPaths[1],
                controllerAimSpaces[1],
                L"right aim") &&
            CreateControllerPoseSpace(
                controllerGripAction,
                controllerUserPaths[0],
                controllerGripSpaces[0],
                L"left grip") &&
            CreateControllerPoseSpace(
                controllerGripAction,
                controllerUserPaths[1],
                controllerGripSpaces[1],
                L"right grip");
        if (!spacesCreated)
        {
            DestroyControllerInput();
            return false;
        }

        controllerInputAttached = true;
        WriteLog(
            L"OpenXR attached a two-hand controller action set with per-hand vibration output. The x64 presenter samples input and applies gated hover, accepted-shot, and death pulses; the x86 client independently accepts fresh focused samples and may translate them into the current local native PlayerInput frame.");
        return true;
    }

    void SamplePoseAction(
        XrAction action,
        XrPath handPath,
        XrSpace actionSpace,
        XrTime displayTime,
        bool& active,
        bool& positionValid,
        bool& orientationValid,
        bool& positionTracked,
        bool& orientationTracked,
        OpenXRPresentationPose& pose)
    {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPath;
        XrActionStatePose actionState{XR_TYPE_ACTION_STATE_POSE};
        const XrResult stateResult = api.getActionStatePose(
            session,
            &getInfo,
            &actionState);
        active = XR_SUCCEEDED(stateResult) && actionState.isActive != XR_FALSE;
        if (!active || actionSpace == XR_NULL_HANDLE)
        {
            return;
        }
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const XrResult locationResult = api.locateSpace(
            actionSpace,
            trackingBasis.ApplicationSpace(),
            displayTime,
            &location);
        if (XR_FAILED(locationResult))
        {
            return;
        }
        positionValid =
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        orientationValid =
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        positionTracked =
            (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
        orientationTracked =
            (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
        if (positionValid)
        {
            pose.positionX = location.pose.position.x;
            pose.positionY = location.pose.position.y;
            pose.positionZ = location.pose.position.z;
        }
        if (orientationValid)
        {
            pose.orientationX = location.pose.orientation.x;
            pose.orientationY = location.pose.orientation.y;
            pose.orientationZ = location.pose.orientation.z;
            pose.orientationW = location.pose.orientation.w;
        }
    }

    void SampleFloatAction(
        XrAction action,
        XrPath handPath,
        bool& active,
        float& value)
    {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPath;
        XrActionStateFloat actionState{XR_TYPE_ACTION_STATE_FLOAT};
        const XrResult result = api.getActionStateFloat(session, &getInfo, &actionState);
        active = XR_SUCCEEDED(result) && actionState.isActive != XR_FALSE &&
            std::isfinite(actionState.currentState);
        value = active ? std::clamp(actionState.currentState, 0.0F, 1.0F) : 0.0F;
    }

    void SampleThumbstickAction(
        XrPath handPath,
        OpenXRControllerHandState& hand)
    {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = controllerThumbstickAction;
        getInfo.subactionPath = handPath;
        XrActionStateVector2f actionState{XR_TYPE_ACTION_STATE_VECTOR2F};
        const XrResult result = api.getActionStateVector2f(
            session,
            &getInfo,
            &actionState);
        hand.thumbstickActive =
            XR_SUCCEEDED(result) && actionState.isActive != XR_FALSE &&
            std::isfinite(actionState.currentState.x) &&
            std::isfinite(actionState.currentState.y);
        if (hand.thumbstickActive)
        {
            hand.thumbstickX = std::clamp(actionState.currentState.x, -1.0F, 1.0F);
            hand.thumbstickY = std::clamp(actionState.currentState.y, -1.0F, 1.0F);
        }
    }

    bool SampleBooleanAction(XrAction action, XrPath handPath)
    {
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = handPath;
        XrActionStateBoolean actionState{XR_TYPE_ACTION_STATE_BOOLEAN};
        const XrResult result = api.getActionStateBoolean(session, &getInfo, &actionState);
        return XR_SUCCEEDED(result) && actionState.isActive != XR_FALSE &&
            actionState.currentState != XR_FALSE;
    }

    void LogInteractionProfile(std::size_t hand)
    {
        XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
        const XrResult result = api.getCurrentInteractionProfile(
            session,
            controllerUserPaths[hand],
            &profile);
        if (XR_FAILED(result) ||
            controllerInteractionProfiles[hand] == profile.interactionProfile)
        {
            return;
        }
        controllerInteractionProfiles[hand] = profile.interactionProfile;
        if (profile.interactionProfile == XR_NULL_PATH)
        {
            WriteLog(
                L"OpenXR controller input has no active interaction profile for the %s hand.",
                hand == 0 ? L"left" : L"right");
            return;
        }
        uint32_t pathLength = 0;
        if (XR_FAILED(api.pathToString(
                instance,
                profile.interactionProfile,
                0,
                &pathLength,
                nullptr)) ||
            pathLength == 0)
        {
            return;
        }
        std::vector<char> path(pathLength);
        if (XR_SUCCEEDED(api.pathToString(
                instance,
                profile.interactionProfile,
                pathLength,
                &pathLength,
                path.data())))
        {
            WriteLog(
                L"OpenXR controller input selected %S for the %s hand.",
                path.data(),
                hand == 0 ? L"left" : L"right");
        }
    }

    void SampleControllerInput(
        XrTime displayTime,
        OpenXRControllerInputState& output)
    {
        output = {};
        output.predictedDisplayTime = static_cast<std::int64_t>(displayTime);
        output.sessionFocused = sessionState == XR_SESSION_STATE_FOCUSED;
        if (!output.sessionFocused || !controllerInputAttached)
        {
            return;
        }

        const XrActiveActionSet activeActionSet = {
            controllerActionSet,
            XR_NULL_PATH};
        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeActionSet;
        const XrResult syncResult = api.syncActions(session, &syncInfo);
        if (XR_FAILED(syncResult))
        {
            if (syncResult != lastControllerSyncResult)
            {
                WriteLog(
                    L"OpenXR controller input action sync failed (%s, %ld); the sample is invalid and ignored.",
                    DescribeOpenXRResult(syncResult),
                    static_cast<long>(syncResult));
            }
            lastControllerSyncResult = syncResult;
            return;
        }
        lastControllerSyncResult = XR_SUCCESS;

        for (std::size_t hand = 0; hand < output.hands.size(); ++hand)
        {
            OpenXRControllerHandState& destination = output.hands[hand];
            const XrPath handPath = controllerUserPaths[hand];
            SamplePoseAction(
                controllerAimAction,
                handPath,
                controllerAimSpaces[hand],
                displayTime,
                destination.aimActive,
                destination.aimPositionValid,
                destination.aimOrientationValid,
                destination.aimPositionTracked,
                destination.aimOrientationTracked,
                destination.aimPose);
            SamplePoseAction(
                controllerGripAction,
                handPath,
                controllerGripSpaces[hand],
                displayTime,
                destination.gripActive,
                destination.gripPositionValid,
                destination.gripOrientationValid,
                destination.gripPositionTracked,
                destination.gripOrientationTracked,
                destination.gripPose);
            SampleFloatAction(
                controllerTriggerAction,
                handPath,
                destination.triggerActive,
                destination.triggerValue);
            SampleFloatAction(
                controllerSqueezeAction,
                handPath,
                destination.squeezeActive,
                destination.squeezeValue);
            SampleThumbstickAction(handPath, destination);
            destination.thumbstickPressed =
                SampleBooleanAction(controllerThumbstickClickAction, handPath);
            destination.primaryPressed =
                SampleBooleanAction(controllerPrimaryAction, handPath);
            destination.secondaryPressed =
                SampleBooleanAction(controllerSecondaryAction, handPath);
            destination.menuPressed =
                SampleBooleanAction(controllerMenuAction, handPath);
            LogInteractionProfile(hand);
        }
    }

    bool CreateResources()
    {
        WriteLog(L"OpenXR presentation enumerating primary-stereo view configuration.");
        uint32_t viewCount = 0;
        XrResult result = api.enumerateViewConfigurationViews(
            instance,
            systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0,
            &viewCount,
            nullptr);
        if (XR_FAILED(result) || viewCount != eyeSwapchains.size())
        {
            WriteLog(
                L"OpenXR presentation expected exactly two primary-stereo views, received %u (%s, %ld).",
                viewCount,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        std::vector<XrViewConfigurationView> views(viewCount);
        for (XrViewConfigurationView& view : views)
        {
            view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
            view.next = nullptr;
        }
        result = api.enumerateViewConfigurationViews(
            instance,
            systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            viewCount,
            &viewCount,
            views.data());
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation view-configuration query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        if (!ChooseSwapchainFormat() ||
            !CreateSwapchain(
                eyeSwapchains[0],
                views[0].recommendedImageRectWidth,
                views[0].recommendedImageRectHeight) ||
            !CreateSwapchain(
                eyeSwapchains[1],
                views[1].recommendedImageRectWidth,
                views[1].recommendedImageRectHeight) ||
            !CreateSwapchain(
                uiSwapchain,
                views[0].recommendedImageRectWidth,
                views[0].recommendedImageRectHeight))
        {
            return false;
        }

        if (!trackingBasis.Initialize(
                session,
                {api.createReferenceSpace, api.destroySpace, api.locateSpace},
                localFloorAvailable,
                logCallback,
                logContext))
        {
            return false;
        }
        XrReferenceSpaceCreateInfo referenceSpaceInfo{
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        referenceSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        referenceSpaceInfo.poseInReferenceSpace.orientation.w = 1.0F;
        result = api.createReferenceSpace(session, &referenceSpaceInfo, &viewSpace);
        if (XR_FAILED(result) || viewSpace == XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR presentation could not create the required VIEW reference space (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        uint32_t blendModeCount = 0;
        result = api.enumerateEnvironmentBlendModes(
            instance,
            systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            0,
            &blendModeCount,
            nullptr);
        if (XR_FAILED(result) || blendModeCount == 0)
        {
            WriteLog(
                L"OpenXR presentation blend-mode count query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
        result = api.enumerateEnvironmentBlendModes(
            instance,
            systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            blendModeCount,
            &blendModeCount,
            blendModes.data());
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation blend-mode query failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        const auto opaqueMode = std::find(
            blendModes.begin(),
            blendModes.end(),
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        blendMode = opaqueMode != blendModes.end() ? *opaqueMode : blendModes.front();

        textureRequirements.leftWorldWidth = eyeSwapchains[0].width;
        textureRequirements.leftWorldHeight = eyeSwapchains[0].height;
        textureRequirements.rightWorldWidth = eyeSwapchains[1].width;
        textureRequirements.rightWorldHeight = eyeSwapchains[1].height;
        textureRequirements.uiWidth = uiSwapchain.width;
        textureRequirements.uiHeight = uiSwapchain.height;
        textureRequirements.format = swapchainFormat;
        WriteLog(
            L"OpenXR presentation created two world swapchains (%ux%u, %ux%u), one Ref2 UI swapchain (%ux%u), and LOCAL/VIEW reference spaces. UI mode=%s.",
            eyeSwapchains[0].width,
            eyeSwapchains[0].height,
            eyeSwapchains[1].width,
            eyeSwapchains[1].height,
            uiSwapchain.width,
            uiSwapchain.height,
            activeUiLayerMode == OpenXRUiLayerMode::Cylinder ? L"cylinder" : L"quad");
        return true;
    }

    bool HandleSessionState(XrSessionState state)
    {
        sessionState = state;
        switch (state)
        {
        case XR_SESSION_STATE_READY:
        {
            if (sessionRunning)
            {
                return true;
            }
            XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            const XrResult result = api.beginSession(session, &beginInfo);
            if (XR_FAILED(result))
            {
                WriteLog(
                    L"OpenXR presentation session begin failed (%s, %ld).",
                    DescribeOpenXRResult(result),
                    static_cast<long>(result));
                return false;
            }
            sessionRunning = true;
            WriteLog(L"OpenXR presentation session entered READY and began primary-stereo rendering.");
            return true;
        }
        case XR_SESSION_STATE_STOPPING:
            if (sessionRunning)
            {
                const XrResult result = api.endSession(session);
                if (XR_FAILED(result))
                {
                    WriteLog(
                        L"OpenXR presentation session end failed (%s, %ld).",
                        DescribeOpenXRResult(result),
                        static_cast<long>(result));
                    return false;
                }
                sessionRunning = false;
                WriteLog(L"OpenXR presentation session stopped; no more frames will be submitted until READY returns.");
            }
            return true;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            sessionRunning = false;
            terminalRuntimeState = true;
            WriteLog(
                L"OpenXR presentation received terminal session state %d; flat fallback remains active.",
                static_cast<int>(state));
            return false;
        default:
            return true;
        }
    }

    bool PollEvents()
    {
        if (!initialized || terminalRuntimeState)
        {
            return false;
        }

        for (;;)
        {
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            const XrResult result = api.pollEvent(instance, &event);
            if (result == XR_EVENT_UNAVAILABLE)
            {
                return true;
            }
            if (XR_FAILED(result))
            {
                WriteLog(
                    L"OpenXR presentation event poll failed (%s, %ld).",
                    DescribeOpenXRResult(result),
                    static_cast<long>(result));
                return false;
            }
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto* const stateChanged =
                    reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                WriteLog(
                    L"OpenXR presentation session state changed to %d.",
                    static_cast<int>(stateChanged->state));
                if (!HandleSessionState(stateChanged->state))
                {
                    return false;
                }
            }
        }
    }

    bool ValidateSourceTexture(
        ID3D11Texture2D* texture,
        const Swapchain& target,
        const wchar_t* label) const
    {
        if (texture == nullptr)
        {
            WriteLog(L"OpenXR presentation %s source texture is null.", label);
            return false;
        }
        D3D11_TEXTURE2D_DESC description = {};
        texture->GetDesc(&description);
        if (description.Width != target.width ||
            description.Height != target.height ||
            description.Format != swapchainFormat ||
            description.SampleDesc.Count != 1 ||
            description.ArraySize != 1)
        {
            WriteLog(
                L"OpenXR presentation %s source texture must be %ux%u format=%d single-sample; received %ux%u format=%d samples=%u array=%u.",
                label,
                target.width,
                target.height,
                static_cast<int>(swapchainFormat),
                description.Width,
                description.Height,
                static_cast<int>(description.Format),
                description.SampleDesc.Count,
                description.ArraySize);
            return false;
        }
        return true;
    }

    bool CopyToSwapchain(Swapchain& target, ID3D11Texture2D* source, const wchar_t* label)
    {
        if (!ValidateSourceTexture(source, target, label))
        {
            return false;
        }

        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult result = api.acquireSwapchainImage(target.handle, &acquireInfo, &imageIndex);
        if (XR_FAILED(result) || imageIndex >= target.images.size())
        {
            WriteLog(
                L"OpenXR presentation could not acquire its %s image (%s, %ld).",
                label,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        bool released = false;
        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        result = api.waitSwapchainImage(target.handle, &waitInfo);
        if (XR_SUCCEEDED(result) && target.images[imageIndex].texture != nullptr)
        {
            context->CopyResource(target.images[imageIndex].texture, source);
        }
        else
        {
            WriteLog(
                L"OpenXR presentation could not wait for its %s image (%s, %ld).",
                label,
                DescribeOpenXRResult(result),
                static_cast<long>(result));
        }

        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const XrResult releaseResult = api.releaseSwapchainImage(target.handle, &releaseInfo);
        released = XR_SUCCEEDED(releaseResult);
        if (!released)
        {
            WriteLog(
                L"OpenXR presentation could not release its %s image (%s, %ld).",
                label,
                DescribeOpenXRResult(releaseResult),
                static_cast<long>(releaseResult));
        }
        return XR_SUCCEEDED(result) && target.images[imageIndex].texture != nullptr && released;
    }

    bool BeginFrame(OpenXRPresentationFrameState& publicFrameState)
    {
        publicFrameState = {};
        publicFrameState.recenterForwardSequence =
            recenterForwardSequence;
        if (!initialized ||
            !sessionRunning ||
            terminalRuntimeState ||
            frameInProgress)
        {
            return false;
        }

        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        pendingFrameState = {XR_TYPE_FRAME_STATE};
        XrResult result = api.waitFrame(session, &waitInfo, &pendingFrameState);
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation frame wait failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }

        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        result = api.beginFrame(session, &beginInfo);
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation frame begin failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        frameInProgress = true;

        pendingViews = {};
        for (XrView& view : pendingViews)
        {
            view.type = XR_TYPE_VIEW;
            view.next = nullptr;
        }
        pendingViewsValid = false;
        publicFrameState.predictedDisplayTime =
            static_cast<std::int64_t>(pendingFrameState.predictedDisplayTime);
        publicFrameState.predictedDisplayPeriod =
            static_cast<std::int64_t>(pendingFrameState.predictedDisplayPeriod);
        publicFrameState.shouldRender = pendingFrameState.shouldRender != XR_FALSE;
        pendingHeadPoseValid = false;
        SampleControllerInput(
            pendingFrameState.predictedDisplayTime,
            publicFrameState.controllerInput);
        const OpenXRControllerShortcutOutput shortcuts =
            UpdateOpenXRControllerShortcuts(
                controllerShortcutState,
                {
                    publicFrameState.predictedDisplayTime,
                    publicFrameState.controllerInput.sessionFocused,
                    IsOpenXRMapActionPressed(
                        publicFrameState.controllerInput.hands[0].menuPressed,
                        publicFrameState.controllerInput.hands[1].menuPressed),
                    publicFrameState.controllerInput.hands[1].secondaryPressed});
        publicFrameState.mapToggleRequested =
            shortcuts.mapToggleRequested;
        controllerRecenterPending =
            controllerRecenterPending || shortcuts.recenterRequested;
        if (!publicFrameState.controllerInput.sessionFocused)
        {
            controllerRecenterPending = false;
        }
        if (!pendingFrameState.shouldRender)
        {
            UpdateQuickMenuAndHoverHaptics(publicFrameState);
            return true;
        }

        XrSpaceLocation headLocation{XR_TYPE_SPACE_LOCATION};
        const XrResult headResult = api.locateSpace(
            viewSpace,
            trackingBasis.ApplicationSpace(),
            pendingFrameState.predictedDisplayTime,
            &headLocation);
        constexpr XrSpaceLocationFlags kHeadValidFlags =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        constexpr XrSpaceLocationFlags kHeadTrackedFlags =
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        publicFrameState.headPoseValid =
            XR_SUCCEEDED(headResult) &&
            (headLocation.locationFlags & kHeadValidFlags) == kHeadValidFlags;
        publicFrameState.headPoseTracked =
            publicFrameState.headPoseValid &&
            (headLocation.locationFlags & kHeadTrackedFlags) == kHeadTrackedFlags;
        if (publicFrameState.headPoseValid)
        {
            pendingHeadPose = headLocation.pose;
            pendingHeadPoseValid = true;
            publicFrameState.headPose.orientationX =
                headLocation.pose.orientation.x;
            publicFrameState.headPose.orientationY =
                headLocation.pose.orientation.y;
            publicFrameState.headPose.orientationZ =
                headLocation.pose.orientation.z;
            publicFrameState.headPose.orientationW =
                headLocation.pose.orientation.w;
            publicFrameState.headPose.positionX = headLocation.pose.position.x;
            publicFrameState.headPose.positionY = headLocation.pose.position.y;
            publicFrameState.headPose.positionZ = headLocation.pose.position.z;
        }
        publicFrameState.standingHeightValid =
            trackingBasis.LocateStandingHeight(
                viewSpace,
                pendingFrameState.predictedDisplayTime,
                publicFrameState.standingHeightMeters);
        UpdateQuickMenuAndHoverHaptics(publicFrameState);
        const bool settingsRecenterRequested =
            quickMenu.TakeTrackingAction() ==
                OpenXRTrackingAction::RecenterForward;
        if (settingsRecenterRequested || controllerRecenterPending)
        {
            const bool recentered = publicFrameState.headPoseValid &&
                publicFrameState.headPoseTracked;
            if (settingsRecenterRequested)
            {
                quickMenu.SetForwardRecenterResult(recentered);
            }
            if (controllerRecenterPending)
            {
                WriteLog(
                    recentered
                        ? L"Right B 2.5-second hold requested a forward recenter."
                        : L"Right B 2.5-second hold could not recenter because head tracking was unavailable.");
                controllerRecenterPending = false;
            }
            if (recentered)
            {
                recenterForwardSequence = recenterForwardSequence == LONG_MAX
                    ? 1
                    : recenterForwardSequence + 1;
                publicFrameState.recenterForwardSequence =
                    recenterForwardSequence;
                quickMenu.OnTrackingSpaceChanged();
                UpdateQuickMenuAndHoverHaptics(publicFrameState);
            }
        }

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = pendingFrameState.predictedDisplayTime;
        locateInfo.space = trackingBasis.ApplicationSpace();
        result = api.locateViews(
            session,
            &locateInfo,
            &viewState,
            static_cast<uint32_t>(pendingViews.size()),
            &viewCount,
            pendingViews.data());
        pendingViewsValid =
            XR_SUCCEEDED(result) &&
            viewCount == pendingViews.size() &&
            (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        if (!pendingViewsValid)
        {
            WriteLog(
                L"OpenXR presentation view location did not produce two valid stereo poses (%s, %ld, count=%u, flags=0x%08lX).",
                DescribeOpenXRResult(result),
                static_cast<long>(result),
                viewCount,
                static_cast<unsigned long>(viewState.viewStateFlags));
            return true;
        }

        publicFrameState.viewsValid = true;
        for (std::size_t eye = 0; eye < pendingViews.size(); ++eye)
        {
            const XrView& source = pendingViews[eye];
            OpenXRPresentationView& destination = publicFrameState.views[eye];
            destination.pose.orientationX = source.pose.orientation.x;
            destination.pose.orientationY = source.pose.orientation.y;
            destination.pose.orientationZ = source.pose.orientation.z;
            destination.pose.orientationW = source.pose.orientation.w;
            destination.pose.positionX = source.pose.position.x;
            destination.pose.positionY = source.pose.position.y;
            destination.pose.positionZ = source.pose.position.z;
            destination.fov.angleLeft = source.fov.angleLeft;
            destination.fov.angleRight = source.fov.angleRight;
            destination.fov.angleUp = source.fov.angleUp;
            destination.fov.angleDown = source.fov.angleDown;
        }
        return true;
    }

#include "openxr/internal/OpenXRPresentationHaptics.inl"

    bool EndFrame(
        const OpenXRPresentationTextures& textures,
        OpenXRUiReferenceMode uiReferenceMode,
        const OpenXRPresentationPose* worldUiAnchor,
        OpenXRUiPresentationMode uiPresentationMode,
        OpenXRSwapchainContentMode swapchainContentMode)
    {
        if (!frameInProgress)
        {
            return false;
        }

        const bool renderingRequired =
            pendingFrameState.shouldRender != XR_FALSE;
        bool haveLayers = false;
        XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::array<XrCompositionLayerProjectionView, 2> projectionViews = {};
        XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        std::array<XrCompositionLayerQuad, 2> scopeQuadLayers = {};
        XrCompositionLayerCylinderKHR cylinderLayer{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
        std::array<const XrCompositionLayerBaseHeader*, 8> layers = {};
        uint32_t layerCount = 0;
        bool imagesReady = false;
        if (pendingFrameState.shouldRender && pendingViewsValid)
        {
            const bool updateSwapchainImages =
                swapchainContentMode == OpenXRSwapchainContentMode::Update ||
                !swapchainImagesValid;
            if (updateSwapchainImages)
            {
                imagesReady =
                    CopyToSwapchain(eyeSwapchains[0], textures.leftWorld, L"left-world") &&
                    CopyToSwapchain(eyeSwapchains[1], textures.rightWorld, L"right-world") &&
                    CopyToSwapchain(uiSwapchain, textures.ref2Ui, L"Ref2-UI");
                swapchainImagesValid = imagesReady;
            }
            else
            {
                // OpenXR 1.0 explicitly permits xrEndFrame without another
                // release. Each layer then references the last image released
                // from its swapchain. Keep new tracking/layer poses while
                // avoiding three duplicate full-resolution GPU copies.
                imagesReady = true;
                if (!swapchainReuseLogged)
                {
                    WriteLog(
                        L"OpenXR repeated-source presentation reuses the last released eye/UI swapchain images while refreshing tracking and layer poses.");
                    swapchainReuseLogged = true;
                }
            }
            if (imagesReady)
            {
                projectionLayer.space = trackingBasis.ApplicationSpace();
                projectionLayer.viewCount = static_cast<uint32_t>(projectionViews.size());
                projectionLayer.views = projectionViews.data();
                for (std::size_t eye = 0; eye < projectionViews.size(); ++eye)
                {
                    XrCompositionLayerProjectionView& projectionView = projectionViews[eye];
                    projectionView.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projectionView.pose = pendingViews[eye].pose;
                    projectionView.fov = pendingViews[eye].fov;
                    projectionView.subImage.swapchain = eyeSwapchains[eye].handle;
                    projectionView.subImage.imageRect.extent.width =
                        static_cast<int32_t>(eyeSwapchains[eye].width);
                    projectionView.subImage.imageRect.extent.height =
                        static_cast<int32_t>(eyeSwapchains[eye].height);
                }
                layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer);

                // Darken only the stereo world; all HUD/menu layers follow.
                if (pendingHeadPoseValid)
                {
                    const float deltaSeconds = pendingFrameState.predictedDisplayPeriod > 0
                        ? static_cast<float>(
                            pendingFrameState.predictedDisplayPeriod) * 1.0e-9F
                        : 1.0F / 90.0F;
                    layerCount += static_cast<std::uint32_t>(
                        comfortVignette.AppendLayers(
                            comfortVignetteTarget, deltaSeconds, viewSpace,
                            pendingHeadPose, pendingViews,
                            layers.data() + layerCount,
                            layers.size() - layerCount));
                }

                bool submittedEyeFillingScope = false;
                if (uiPresentationMode ==
                        OpenXRUiPresentationMode::EyeFillingScope &&
                    pendingHeadPoseValid)
                {
                    submittedEyeFillingScope =
                        BuildOpenXREyeFillingScopeLayers(
                            viewSpace,
                            uiSwapchain.handle,
                            uiSwapchain.width,
                            uiSwapchain.height,
                            pendingHeadPose,
                            pendingViews,
                            scopeQuadLayers);
                    if (submittedEyeFillingScope)
                    {
                        for (XrCompositionLayerQuad& scopeQuad :
                             scopeQuadLayers)
                        {
                            layers[layerCount++] = reinterpret_cast<
                                const XrCompositionLayerBaseHeader*>(
                                    &scopeQuad);
                        }
                    }
                }
                if (!scopePresentationInitialized ||
                    lastEyeFillingScopePresented != submittedEyeFillingScope)
                {
                    WriteLog(
                        submittedEyeFillingScope
                            ? L"Verified scope Ref2 UI promoted to centred eye-exclusive VIEW quads sized from the current OpenXR eye FOVs."
                            : L"Ref2 UI returned to the ordinary HUD/menu presentation policy.");
                }
                lastEyeFillingScopePresented = submittedEyeFillingScope;
                scopePresentationInitialized = true;

                if (!submittedEyeFillingScope)
                {
                    XrSpace uiSpace = viewSpace;
                    XrPosef uiPose = {};
                    uiPose.orientation.w = 1.0F;
                    uiPose.position.z = -configuration.uiDistanceMeters;
                    if (uiReferenceMode ==
                        OpenXRUiReferenceMode::WorldLocked)
                    {
                    if (worldUiAnchor != nullptr &&
                        IsFiniteUnitPose(*worldUiAnchor))
                    {
                        XrPosef anchor = {};
                        anchor.orientation = {
                            worldUiAnchor->orientationX,
                            worldUiAnchor->orientationY,
                            worldUiAnchor->orientationZ,
                            worldUiAnchor->orientationW};
                        anchor.position = {
                            worldUiAnchor->positionX,
                            worldUiAnchor->positionY,
                            worldUiAnchor->positionZ};
                        XrPosef anchorOffset = {};
                        anchorOffset.orientation.w = 1.0F;
                        anchorOffset.position.z =
                            -configuration.uiDistanceMeters;
                        worldLockedUiPose =
                            ComposeOpenXRPose(anchor, anchorOffset);
                        worldLockedUiPoseValid = true;
                    }
                    else if (pendingHeadPoseValid &&
                        (!worldLockedUiPoseValid ||
                         !uiReferenceModeInitialized ||
                         lastUiReferenceMode !=
                            OpenXRUiReferenceMode::WorldLocked))
                    {
                        XrPosef headLocalOffset = {};
                        headLocalOffset.orientation.w = 1.0F;
                        headLocalOffset.position.z =
                            -configuration.uiDistanceMeters;
                        worldLockedUiPose = ComposeOpenXRPose(
                            pendingHeadPose,
                            headLocalOffset);
                        worldLockedUiPoseValid = true;
                    }
                    if (worldLockedUiPoseValid)
                    {
                        uiSpace = trackingBasis.ApplicationSpace();
                        uiPose = worldLockedUiPose;
                    }
                    }
                    if (!uiReferenceModeInitialized ||
                        lastUiReferenceMode != uiReferenceMode)
                    {
                    WriteLog(
                        uiReferenceMode ==
                            OpenXRUiReferenceMode::WorldLocked
                            ? L"Ref2 UI changed to a latched LOCAL world-space menu panel."
                            : L"Ref2 UI changed to a VIEW-space gameplay HUD.");
                    }
                    lastUiReferenceMode = uiReferenceMode;
                    uiReferenceModeInitialized = true;

                    if (activeUiLayerMode == OpenXRUiLayerMode::Cylinder)
                    {
                    cylinderLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    cylinderLayer.space = uiSpace;
                    cylinderLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    cylinderLayer.subImage.swapchain = uiSwapchain.handle;
                    cylinderLayer.subImage.imageRect.extent.width =
                        static_cast<int32_t>(uiSwapchain.width);
                    cylinderLayer.subImage.imageRect.extent.height =
                        static_cast<int32_t>(uiSwapchain.height);
                    cylinderLayer.pose = uiPose;
                    cylinderLayer.radius = configuration.uiDistanceMeters;
                    cylinderLayer.centralAngle = configuration.uiCylinderCentralAngleRadians;
                    cylinderLayer.aspectRatio = static_cast<float>(uiSwapchain.width) /
                        static_cast<float>(uiSwapchain.height);
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinderLayer);
                    }
                    else
                    {
                    quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    quadLayer.space = uiSpace;
                    quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    quadLayer.subImage.swapchain = uiSwapchain.handle;
                    quadLayer.subImage.imageRect.extent.width = static_cast<int32_t>(uiSwapchain.width);
                    quadLayer.subImage.imageRect.extent.height = static_cast<int32_t>(uiSwapchain.height);
                    quadLayer.pose = uiPose;
                    quadLayer.size.width = configuration.uiWidthMeters;
                    quadLayer.size.height = configuration.uiWidthMeters *
                        static_cast<float>(uiSwapchain.height) /
                        static_cast<float>(uiSwapchain.width);
                    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quadLayer);
                    }
                }
                layerCount += static_cast<uint32_t>(
                    quickMenu.AppendLayers(
                        trackingBasis.ApplicationSpace(),
                        layers.data() + layerCount,
                        layers.size() - layerCount));
                haveLayers = true;
            }
        }

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = pendingFrameState.predictedDisplayTime;
        endInfo.environmentBlendMode = blendMode;
        endInfo.layerCount = haveLayers ? layerCount : 0;
        endInfo.layers = haveLayers ? layers.data() : nullptr;
        const XrResult result = api.endFrame(session, &endInfo);
        frameInProgress = false;
        pendingViewsValid = false;
        pendingHeadPoseValid = false;
        pendingFrameState = {};
        pendingViews = {};
        if (XR_FAILED(result))
        {
            WriteLog(
                L"OpenXR presentation frame end failed (%s, %ld).",
                DescribeOpenXRResult(result),
                static_cast<long>(result));
            return false;
        }
        return haveLayers || !renderingRequired;
    }

    bool SubmitFrame(
        const OpenXRPresentationTextures& textures,
        OpenXRUiReferenceMode uiReferenceMode,
        const OpenXRPresentationPose* worldUiAnchor,
        OpenXRUiPresentationMode uiPresentationMode)
    {
        OpenXRPresentationFrameState frameState = {};
        return BeginFrame(frameState) &&
            EndFrame(
                textures,
                uiReferenceMode,
                worldUiAnchor,
                uiPresentationMode,
                OpenXRSwapchainContentMode::Update);
    }

    void DestroySwapchain(Swapchain& swapchain)
    {
        if (swapchain.handle != XR_NULL_HANDLE && api.destroySwapchain != nullptr)
        {
            const XrResult result = api.destroySwapchain(swapchain.handle);
            if (XR_FAILED(result))
            {
                WriteLog(
                    L"OpenXR presentation swapchain destruction failed (%s, %ld).",
                    DescribeOpenXRResult(result),
                    static_cast<long>(result));
            }
        }
        swapchain = {};
    }

    void StopSessionForShutdown()
    {
        if (!sessionRunning || session == XR_NULL_HANDLE)
        {
            return;
        }
        if (api.requestExitSession == nullptr || api.pollEvent == nullptr)
        {
            WriteLog(
                L"OpenXR presentation cannot request a graceful session exit; destroying the session without an invalid direct end call.");
            sessionRunning = false;
            return;
        }

        const XrResult requestResult = api.requestExitSession(session);
        if (XR_FAILED(requestResult))
        {
            WriteLog(
                L"OpenXR presentation session exit request failed (%s, %ld); destroying the session without a direct end call.",
                DescribeOpenXRResult(requestResult),
                static_cast<long>(requestResult));
            sessionRunning = false;
            return;
        }
        WriteLog(L"OpenXR presentation requested session exit; awaiting STOPPING.");

        constexpr DWORD kStopTimeoutMs = 3000;
        const DWORD startedAt = GetTickCount();
        while (sessionRunning && GetTickCount() - startedAt < kStopTimeoutMs)
        {
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            const XrResult pollResult = api.pollEvent(instance, &event);
            if (pollResult == XR_EVENT_UNAVAILABLE)
            {
                Sleep(1);
                continue;
            }
            if (XR_FAILED(pollResult))
            {
                WriteLog(
                    L"OpenXR presentation shutdown event poll failed (%s, %ld).",
                    DescribeOpenXRResult(pollResult),
                    static_cast<long>(pollResult));
                break;
            }
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto* const stateChanged =
                    reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                WriteLog(
                    L"OpenXR presentation shutdown session state changed to %d.",
                    static_cast<int>(stateChanged->state));
                if (!HandleSessionState(stateChanged->state))
                {
                    break;
                }
            }
        }
        if (sessionRunning)
        {
            WriteLog(
                L"OpenXR presentation did not receive STOPPING within %lu ms; destroying the session without a direct end call.",
                static_cast<unsigned long>(kStopTimeoutMs));
            sessionRunning = false;
        }
    }

    void Shutdown()
    {
        if (frameInProgress && api.endFrame != nullptr)
        {
            XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
            endInfo.displayTime = pendingFrameState.predictedDisplayTime;
            endInfo.environmentBlendMode = blendMode;
            api.endFrame(session, &endInfo);
            frameInProgress = false;
        }
        StopSessionForShutdown();

        comfortVignette.Shutdown();
        quickMenu.Shutdown();
        DestroyControllerInput();

        if (viewSpace != XR_NULL_HANDLE && api.destroySpace != nullptr)
        {
            api.destroySpace(viewSpace);
        }
        viewSpace = XR_NULL_HANDLE;
        trackingBasis.Shutdown();
        DestroySwapchain(uiSwapchain);
        for (Swapchain& swapchain : eyeSwapchains)
        {
            DestroySwapchain(swapchain);
        }

        if (session != XR_NULL_HANDLE && api.destroySession != nullptr)
        {
            api.destroySession(session);
        }
        session = XR_NULL_HANDLE;
        if (context != nullptr)
        {
            context->Release();
            context = nullptr;
        }
        if (device != nullptr)
        {
            device->Release();
            device = nullptr;
        }
        if (instance != XR_NULL_HANDLE && api.destroyInstance != nullptr)
        {
            api.destroyInstance(instance);
        }
        instance = XR_NULL_HANDLE;
        systemId = XR_NULL_SYSTEM_ID;
        if (loader != nullptr)
        {
            FreeLibrary(loader);
            loader = nullptr;
        }
        api = {};
        initialized = false;
        terminalRuntimeState = false;
        sessionState = XR_SESSION_STATE_UNKNOWN;
        pendingFrameState = {};
        pendingViews = {};
        pendingViewsValid = false;
        pendingHeadPose = {};
        pendingHeadPoseValid = false;
        swapchainImagesValid = false;
        swapchainReuseLogged = false;
        recenterForwardSequence = 0;
        controllerShortcutState = {};
        controllerRecenterPending = false;
        lastHapticHoverTarget = 0;
        worldLockedUiPose = {};
        worldLockedUiPoseValid = false;
        uiReferenceModeInitialized = false;
        comfortVignetteTarget = 0.0F;
        textureRequirements = {};
    }

    HMODULE loader = nullptr;
    Api api = {};
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrActionSet controllerActionSet = XR_NULL_HANDLE;
    XrAction controllerAimAction = XR_NULL_HANDLE;
    XrAction controllerGripAction = XR_NULL_HANDLE;
    XrAction controllerTriggerAction = XR_NULL_HANDLE;
    XrAction controllerSqueezeAction = XR_NULL_HANDLE;
    XrAction controllerThumbstickAction = XR_NULL_HANDLE;
    XrAction controllerThumbstickClickAction = XR_NULL_HANDLE;
    XrAction controllerPrimaryAction = XR_NULL_HANDLE;
    XrAction controllerSecondaryAction = XR_NULL_HANDLE;
    XrAction controllerMenuAction = XR_NULL_HANDLE;
    XrAction controllerHapticAction = XR_NULL_HANDLE;
    std::array<XrPath, 2> controllerUserPaths = {};
    std::array<XrSpace, 2> controllerAimSpaces = {};
    std::array<XrSpace, 2> controllerGripSpaces = {};
    std::array<XrPath, 2> controllerInteractionProfiles = {};
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL deviceFeatureLevel = D3D_FEATURE_LEVEL_9_1;
    DXGI_FORMAT swapchainFormat = DXGI_FORMAT_UNKNOWN;
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::array<Swapchain, 2> eyeSwapchains = {};
    Swapchain uiSwapchain = {};
    OpenXRQuickMenu quickMenu = {};
    OpenXRComfortVignette comfortVignette = {};
    OpenXRTrackingBasis trackingBasis = {};
    OpenXRPresentationConfiguration configuration = {};
    OpenXRUiLayerMode activeUiLayerMode = OpenXRUiLayerMode::Quad;
    OpenXRPresentationTextureRequirements textureRequirements = {};
    OpenXRLogCallback logCallback = nullptr;
    void* logContext = nullptr;
    bool initialized = false;
    bool sessionRunning = false;
    bool controllerInputAttached = false;
    bool localFloorAvailable = false;
    bool terminalRuntimeState = false;
    bool frameInProgress = false;
    bool pendingViewsValid = false;
    bool pendingHeadPoseValid = false;
    bool swapchainImagesValid = false;
    bool swapchainReuseLogged = false;
    LONG recenterForwardSequence = 0;
    OpenXRControllerShortcutState controllerShortcutState = {};
    bool controllerRecenterPending = false;
    std::uint64_t lastHapticHoverTarget = 0;
    bool worldLockedUiPoseValid = false;
    bool uiReferenceModeInitialized = false;
    XrFrameState pendingFrameState = {};
    std::array<XrView, 2> pendingViews = {};
    XrPosef pendingHeadPose = {};
    XrPosef worldLockedUiPose = {};
    OpenXRUiReferenceMode lastUiReferenceMode =
        OpenXRUiReferenceMode::WorldLocked;
    bool lastEyeFillingScopePresented = false;
    bool scopePresentationInitialized = false;
    float comfortVignetteTarget = 0.0F;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    XrResult lastControllerSyncResult = XR_SUCCESS;
};

OpenXRPresentation::OpenXRPresentation()
    : impl_(std::make_unique<Impl>())
{}

OpenXRPresentation::~OpenXRPresentation()
{
    Shutdown();
}

bool OpenXRPresentation::Initialize(
    const wchar_t* payloadDirectory,
    const OpenXRPresentationConfiguration& configuration,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    impl_->configuration = configuration;
    impl_->logCallback = logCallback;
    impl_->logContext = logContext;
    if (!IsFiniteInRange(
            configuration.uiDistanceMeters,
            kMinimumUiDistanceMeters,
            10.0F) ||
        !IsFiniteInRange(configuration.uiWidthMeters, kMinimumUiWidthMeters, 10.0F) ||
        !IsFiniteInRange(
            configuration.uiCylinderCentralAngleRadians,
            kMinimumCylinderAngleRadians,
            kMaximumCylinderAngleRadians))
    {
        impl_->WriteLog(L"OpenXR presentation configuration has an invalid UI distance, width, or cylinder angle.");
        return false;
    }
    if (!impl_->LoadLoader(payloadDirectory))
    {
        Shutdown();
        return false;
    }

    bool d3d11Available = false;
    bool cylinderAvailable = false;
    if (!impl_->EnumerateExtensions(
            d3d11Available,
            cylinderAvailable,
            impl_->localFloorAvailable) ||
        !d3d11Available)
    {
        impl_->WriteLog(L"OpenXR presentation requires XR_KHR_D3D11_enable; flat fallback remains active.");
        Shutdown();
        return false;
    }

    impl_->activeUiLayerMode = configuration.uiLayerMode;
    if (impl_->activeUiLayerMode == OpenXRUiLayerMode::Cylinder && !cylinderAvailable)
    {
        impl_->activeUiLayerMode = OpenXRUiLayerMode::Quad;
        impl_->WriteLog(L"OpenXR runtime has no cylinder-layer extension; safely falling back to the configured quad UI layer.");
    }

    if (!configuration.diagnosticDirectD3D11Instance &&
        !impl_->CreateBaselineInstanceAndSystem())
    {
        Shutdown();
        return false;
    }

    std::vector<const char*> enabledExtensions = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    if (impl_->activeUiLayerMode == OpenXRUiLayerMode::Cylinder)
    {
        enabledExtensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    }
    if (impl_->localFloorAvailable)
    {
        enabledExtensions.push_back(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
    }
    const XrInstanceCreateInfo createInfo = impl_->BuildInstanceCreateInfo(enabledExtensions);
    if (configuration.diagnosticDirectD3D11Instance)
    {
        impl_->WriteLog(L"OpenXR presentation control: creating the D3D11 instance directly without a prior baseline instance.");
    }
    XrResult result = impl_->api.createInstance(&createInfo, &impl_->instance);
    if (XR_FAILED(result) || impl_->instance == XR_NULL_HANDLE)
    {
        impl_->WriteLog(
            L"OpenXR D3D11 presentation instance creation failed (%s, %ld); flat fallback remains active.",
            DescribeOpenXRResult(result),
            static_cast<long>(result));
        Shutdown();
        return false;
    }
    if (!impl_->ResolvePresentationFunctions())
    {
        Shutdown();
        return false;
    }

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    result = impl_->api.getInstanceProperties(
        impl_->instance,
        &instanceProperties);
    if (XR_SUCCEEDED(result))
    {
        impl_->WriteLog(
            L"OpenXR active runtime is '%S' version %u.%u.%u.",
            instanceProperties.runtimeName,
            static_cast<unsigned>(XR_VERSION_MAJOR(instanceProperties.runtimeVersion)),
            static_cast<unsigned>(XR_VERSION_MINOR(instanceProperties.runtimeVersion)),
            static_cast<unsigned>(XR_VERSION_PATCH(instanceProperties.runtimeVersion)));
    }
    else
    {
        impl_->WriteLog(
            L"OpenXR active-runtime identity query failed (%s, %ld); presentation will continue.",
            DescribeOpenXRResult(result),
            static_cast<long>(result));
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = impl_->api.getSystem(impl_->instance, &systemInfo, &impl_->systemId);
    if (XR_FAILED(result) || impl_->systemId == XR_NULL_SYSTEM_ID)
    {
        impl_->WriteLog(
            L"OpenXR D3D11 presentation system query failed (%s, %ld); flat fallback remains active.",
            DescribeOpenXRResult(result),
            static_cast<long>(result));
        Shutdown();
        return false;
    }

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    result = impl_->api.getD3D11GraphicsRequirements(
        impl_->instance,
        impl_->systemId,
        &requirements);
    if (XR_FAILED(result))
    {
        impl_->WriteLog(
            L"OpenXR D3D11 graphics-requirements query failed (%s, %ld); no substitute device will be created.",
            DescribeOpenXRResult(result),
            static_cast<long>(result));
        Shutdown();
        return false;
    }
    impl_->WriteLog(
        L"OpenXR D3D11 graphics requirements: adapterLuid=%08lX:%08lX minimumFeatureLevel=0x%04X.",
        static_cast<unsigned long>(requirements.adapterLuid.HighPart),
        static_cast<unsigned long>(requirements.adapterLuid.LowPart),
        static_cast<unsigned int>(requirements.minFeatureLevel));
    impl_->textureRequirements.adapterLuid = requirements.adapterLuid;
    impl_->textureRequirements.minimumFeatureLevel = requirements.minFeatureLevel;
    if (!impl_->CreateRuntimeAdapterDevice(requirements))
    {
        Shutdown();
        return false;
    }

    XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    graphicsBinding.device = impl_->device;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = configuration.diagnosticOmitGraphicsBinding
        ? nullptr
        : &graphicsBinding;
    sessionInfo.systemId = impl_->systemId;
    impl_->WriteLog(
        configuration.diagnosticOmitGraphicsBinding
            ? L"OpenXR presentation control: creating a deliberately unbound session to test runtime validation."
            : L"OpenXR presentation creating a D3D11 graphics-bound session.");
    result = impl_->api.createSession(impl_->instance, &sessionInfo, &impl_->session);
    impl_->WriteLog(
        L"OpenXR presentation session creation returned %s (%ld), handle=%p.",
        DescribeOpenXRResult(result),
        static_cast<long>(result),
        impl_->session);
    if (configuration.diagnosticOmitGraphicsBinding)
    {
        impl_->WriteLog(
            XR_FAILED(result) && impl_->session == XR_NULL_HANDLE
                ? L"OpenXR presentation unbound-session control was rejected as expected; no session or swapchain was retained."
                : L"OpenXR presentation unbound-session control unexpectedly succeeded; it will be destroyed without creating resources.");
        Shutdown();
        return false;
    }
    if (XR_FAILED(result) || impl_->session == XR_NULL_HANDLE)
    {
        impl_->WriteLog(
            L"OpenXR D3D11 session creation failed (%s, %ld); flat fallback remains active.",
            DescribeOpenXRResult(result),
            static_cast<long>(result));
        Shutdown();
        return false;
    }
    impl_->WriteLog(L"OpenXR presentation session exists; creating swapchains and LOCAL space.");
    if (!impl_->CreateResources())
    {
        Shutdown();
        return false;
    }
    if (!impl_->InitializeControllerInput())
    {
        impl_->WriteLog(
            L"OpenXR controller input is unavailable; presentation remains active and game input remains unchanged.");
    }
    const OpenXRQuickMenuApi quickMenuApi = {
        impl_->api.createSwapchain,
        impl_->api.destroySwapchain,
        impl_->api.enumerateSwapchainImages,
        impl_->api.acquireSwapchainImage,
        impl_->api.waitSwapchainImage,
        impl_->api.releaseSwapchainImage};
    if (!impl_->quickMenu.Initialize(
            payloadDirectory,
            impl_->session,
            impl_->swapchainFormat,
            impl_->device,
            impl_->context,
            quickMenuApi,
            configuration.uiWidthMeters,
            configuration.uiDistanceMeters,
            logCallback,
            logContext))
    {
        impl_->WriteLog(
            L"Quick Menu resources are unavailable; OpenXR world/HUD presentation remains active and right A still submits no native jump/action input.");
    }
    const OpenXRComfortVignetteApi comfortVignetteApi = {
        impl_->api.createSwapchain, impl_->api.destroySwapchain,
        impl_->api.enumerateSwapchainImages, impl_->api.acquireSwapchainImage,
        impl_->api.waitSwapchainImage, impl_->api.releaseSwapchainImage};
    if (!impl_->comfortVignette.Initialize(
            impl_->session, impl_->swapchainFormat, impl_->device,
            impl_->context, comfortVignetteApi, logCallback, logContext))
    {
        impl_->WriteLog(
            L"Comfort-vignette resources are unavailable; world/HUD presentation remains active without the optional effect.");
    }
    impl_->initialized = true;
    impl_->WriteLog(L"OpenXR presentation initialized; awaiting session READY before it submits frames.");
    return true;
}

bool OpenXRPresentation::PollEvents()
{
    return impl_ != nullptr && impl_->PollEvents();
}
bool OpenXRPresentation::SubmitFrame(
    const OpenXRPresentationTextures& textures,
    OpenXRUiReferenceMode uiReferenceMode,
    const OpenXRPresentationPose* worldUiAnchor,
    OpenXRUiPresentationMode uiPresentationMode)
{
    return impl_ != nullptr &&
        impl_->SubmitFrame(
            textures,
            uiReferenceMode,
            worldUiAnchor,
            uiPresentationMode);
}
bool OpenXRPresentation::BeginFrame(OpenXRPresentationFrameState& frameState)
{
    return impl_ != nullptr && impl_->BeginFrame(frameState);
}
bool OpenXRPresentation::EndFrame(
    const OpenXRPresentationTextures& textures,
    OpenXRUiReferenceMode uiReferenceMode,
    const OpenXRPresentationPose* worldUiAnchor,
    OpenXRUiPresentationMode uiPresentationMode,
    OpenXRSwapchainContentMode swapchainContentMode)
{
    return impl_ != nullptr &&
        impl_->EndFrame(
            textures,
            uiReferenceMode,
            worldUiAnchor,
            uiPresentationMode,
            swapchainContentMode);
}
stereo::QuickMenuSelection
OpenXRPresentation::TakeQuickMenuSelection() noexcept
{
    return impl_ == nullptr
        ? stereo::QuickMenuSelection::None
        : impl_->quickMenu.TakeReleasedSelection();
}
void OpenXRPresentation::OpenSettingsMenu() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->quickMenu.OpenSettingsMenu();
    }
}
void OpenXRPresentation::SetMountedCameraDecoupled(
    bool decoupled) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->quickMenu.SetMountedCameraDecoupled(decoupled);
    }
}
void OpenXRPresentation::SetComfortVignetteTarget(float strength) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->comfortVignetteTarget = std::isfinite(strength)
            ? std::clamp(strength, 0.0F, 1.0F)
            : 0.0F;
    }
}
bool OpenXRPresentation::ApplyHapticFeedback(
    const OpenXRHapticEvent event,
    const std::uint32_t handMask) noexcept
{
    return impl_ != nullptr && impl_->ApplyHapticFeedback(event, handMask);
}
bool OpenXRPresentation::GetQuickMenuMirrorState(
    OpenXRQuickMenuMirrorState& state) const noexcept
{
    state = {};
    return impl_ != nullptr && impl_->quickMenu.GetMirrorState(state);
}
bool OpenXRPresentation::IsInitialized() const noexcept
{
    return impl_ != nullptr && impl_->initialized;
}
bool OpenXRPresentation::IsSessionRunning() const noexcept
{
    return impl_ != nullptr && impl_->sessionRunning;
}
OpenXRPresentationTextureRequirements OpenXRPresentation::GetTextureRequirements() const noexcept
{
    return impl_ == nullptr ? OpenXRPresentationTextureRequirements{} : impl_->textureRequirements;
}
ID3D11Device* OpenXRPresentation::GetD3D11Device() const noexcept
{
    return impl_ == nullptr ? nullptr : impl_->device;
}
ID3D11DeviceContext* OpenXRPresentation::GetD3D11Context() const noexcept
{
    return impl_ == nullptr ? nullptr : impl_->context;
}
void OpenXRPresentation::Shutdown()
{
    if (impl_ != nullptr)
    {
        impl_->Shutdown();
    }
}

} // namespace bfvr
