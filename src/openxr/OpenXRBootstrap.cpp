#include "openxr/OpenXRBootstrap.h"

#include <windows.h>
#include <d3d11.h>

#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

#include "openxr/OpenXRApiVersion.h"

namespace
{
void WriteLog(bfvr::OpenXRLogCallback logCallback, void* logContext, const wchar_t* format, ...)
{
    if (logCallback == nullptr)
    {
        return;
    }

    wchar_t message[512] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback(logContext, message);
}

std::wstring BuildLoaderPath(const wchar_t* payloadDirectory)
{
    if (payloadDirectory == nullptr || *payloadDirectory == L'\0')
    {
        return {};
    }
#if defined(_WIN64)
    return std::wstring(payloadDirectory) + L"\\runtime\\openxr\\win64\\openxr_loader.dll";
#else
    return std::wstring(payloadDirectory) + L"\\runtime\\openxr\\win32\\openxr_loader.dll";
#endif
}

template <typename T>
bool ResolveOpenXRFunction(
    PFN_xrGetInstanceProcAddr getInstanceProcAddr,
    XrInstance instance,
    const char* name,
    T& function,
    bfvr::OpenXRLogCallback logCallback,
    void* logContext)
{
    PFN_xrVoidFunction rawFunction = nullptr;
    const XrResult result = getInstanceProcAddr(instance, name, &rawFunction);
    if (XR_FAILED(result) || rawFunction == nullptr)
    {
        WriteLog(logCallback, logContext, L"OpenXR could not resolve %S (XrResult=%ld).", name, static_cast<long>(result));
        return false;
    }

    function = reinterpret_cast<T>(rawFunction);
    return true;
}

const wchar_t* DescribeOpenXRResult(XrResult result)
{
    switch (result)
    {
    case XR_SUCCESS:
        return L"XR_SUCCESS";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
        return L"XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_RUNTIME_UNAVAILABLE:
        return L"XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_API_VERSION_UNSUPPORTED:
        return L"XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_INITIALIZATION_FAILED:
        return L"XR_ERROR_INITIALIZATION_FAILED";
    default:
        return L"unclassified OpenXR result";
    }
}

struct RuntimeGraphicsExtensions
{
    bool d3d11Available = false;
    bool d3d12Available = false;
};

RuntimeGraphicsExtensions EnumerateRuntimeGraphicsExtensions(
    PFN_xrGetInstanceProcAddr getInstanceProcAddr,
    bfvr::OpenXRLogCallback logCallback,
    void* logContext)
{
    RuntimeGraphicsExtensions capabilities = {};
    PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions = nullptr;
    if (!ResolveOpenXRFunction(
            getInstanceProcAddr,
            XR_NULL_HANDLE,
            "xrEnumerateInstanceExtensionProperties",
            enumerateExtensions,
            logCallback,
            logContext))
    {
        return capabilities;
    }

    uint32_t extensionCount = 0;
    const XrResult countResult = enumerateExtensions(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(countResult))
    {
        WriteLog(logCallback, logContext, L"OpenXR extension-count query returned %s (%ld).", DescribeOpenXRResult(countResult), static_cast<long>(countResult));
        return capabilities;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount);
    for (XrExtensionProperties& extension : extensions)
    {
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
        extension.next = nullptr;
    }

    const XrResult listResult = enumerateExtensions(nullptr, extensionCount, &extensionCount, extensions.data());
    if (XR_FAILED(listResult))
    {
        WriteLog(logCallback, logContext, L"OpenXR extension-list query returned %s (%ld).", DescribeOpenXRResult(listResult), static_cast<long>(listResult));
        return capabilities;
    }

    for (const XrExtensionProperties& extension : extensions)
    {
        capabilities.d3d11Available = capabilities.d3d11Available || std::strcmp(extension.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
        capabilities.d3d12Available = capabilities.d3d12Available || std::strcmp(extension.extensionName, "XR_KHR_D3D12_enable") == 0;
    }
    WriteLog(
        logCallback,
        logContext,
        L"OpenXR runtime exposes %u instance extensions; D3D11=%s, D3D12=%s.",
        extensionCount,
        capabilities.d3d11Available ? L"available" : L"unavailable",
        capabilities.d3d12Available ? L"available" : L"unavailable");
    return capabilities;
}

void QueryD3D11GraphicsRequirements(
    PFN_xrGetInstanceProcAddr getInstanceProcAddr,
    XrInstance instance,
    XrSystemId systemId,
    bfvr::OpenXRLogCallback logCallback,
    void* logContext)
{
    PFN_xrGetD3D11GraphicsRequirementsKHR getD3D11GraphicsRequirements = nullptr;
    if (!ResolveOpenXRFunction(
            getInstanceProcAddr,
            instance,
            "xrGetD3D11GraphicsRequirementsKHR",
            getD3D11GraphicsRequirements,
            logCallback,
            logContext))
    {
        return;
    }

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    const XrResult requirementsResult = getD3D11GraphicsRequirements(instance, systemId, &requirements);
    if (XR_FAILED(requirementsResult))
    {
        WriteLog(
            logCallback,
            logContext,
            L"OpenXR D3D11 graphics-requirements query returned %s (%ld); no D3D11 device or session was created.",
            DescribeOpenXRResult(requirementsResult),
            static_cast<long>(requirementsResult));
        return;
    }

    WriteLog(
        logCallback,
        logContext,
        L"OpenXR D3D11 graphics requirements: adapterLuid=%08lX:%08lX minFeatureLevel=0x%04X. BFVR created no adapter-bound D3D11 device, session, graphics binding, swapchain, or layer.",
        static_cast<unsigned long>(static_cast<DWORD>(requirements.adapterLuid.HighPart)),
        static_cast<unsigned long>(requirements.adapterLuid.LowPart),
        static_cast<unsigned int>(requirements.minFeatureLevel));
}

bool DestroyOpenXRInstance(
    PFN_xrGetInstanceProcAddr getInstanceProcAddr,
    XrInstance& instance,
    bfvr::OpenXRLogCallback logCallback,
    void* logContext)
{
    if (instance == XR_NULL_HANDLE)
    {
        return true;
    }

    PFN_xrDestroyInstance destroyInstance = nullptr;
    if (!ResolveOpenXRFunction(getInstanceProcAddr, instance, "xrDestroyInstance", destroyInstance, logCallback, logContext))
    {
        return false;
    }

    const XrResult destroyResult = destroyInstance(instance);
    if (XR_FAILED(destroyResult))
    {
        WriteLog(logCallback, logContext, L"OpenXR instance destruction returned XrResult=%ld.", static_cast<long>(destroyResult));
        return false;
    }

    instance = XR_NULL_HANDLE;
    return true;
}
}

namespace bfvr
{
bool ProbeOpenXRRuntime(const wchar_t* payloadDirectory, OpenXRLogCallback logCallback, void* logContext)
{
    const std::wstring loaderPath = BuildLoaderPath(payloadDirectory);
    if (loaderPath.empty())
    {
        WriteLog(logCallback, logContext, L"OpenXR probe skipped because the BFVR payload directory is unavailable.");
        return false;
    }

    HMODULE loader = LoadLibraryW(loaderPath.c_str());
    if (loader == nullptr)
    {
        WriteLog(logCallback, logContext, L"OpenXR loader was not loaded from %s (error %lu).", loaderPath.c_str(), GetLastError());
        return false;
    }

    auto getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader, "xrGetInstanceProcAddr"));
    if (getInstanceProcAddr == nullptr)
    {
        WriteLog(logCallback, logContext, L"OpenXR loader does not export xrGetInstanceProcAddr.");
        FreeLibrary(loader);
        return false;
    }

    const RuntimeGraphicsExtensions graphicsExtensions =
        EnumerateRuntimeGraphicsExtensions(getInstanceProcAddr, logCallback, logContext);
    PFN_xrCreateInstance createInstance = nullptr;
    if (!ResolveOpenXRFunction(getInstanceProcAddr, XR_NULL_HANDLE, "xrCreateInstance", createInstance, logCallback, logContext))
    {
        FreeLibrary(loader);
        return false;
    }

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(createInfo.applicationInfo.applicationName, "BFVR bootstrap");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy_s(createInfo.applicationInfo.engineName, "BFVR");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion =
        bfvr::kRequestedOpenXRApiVersion;

    XrInstance instance = XR_NULL_HANDLE;
    const XrResult createResult = createInstance(&createInfo, &instance);
    if (XR_FAILED(createResult) || instance == XR_NULL_HANDLE)
    {
        WriteLog(
            logCallback,
            logContext,
            L"OpenXR instance creation with requested API %u.%u.%u returned %s (%ld); flat fallback remains active.",
            static_cast<unsigned>(XR_VERSION_MAJOR(
                bfvr::kRequestedOpenXRApiVersion)),
            static_cast<unsigned>(XR_VERSION_MINOR(
                bfvr::kRequestedOpenXRApiVersion)),
            static_cast<unsigned>(XR_VERSION_PATCH(
                bfvr::kRequestedOpenXRApiVersion)),
            DescribeOpenXRResult(createResult),
            static_cast<long>(createResult));
        FreeLibrary(loader);
        return false;
    }

    WriteLog(
        logCallback,
        logContext,
        L"OpenXR instance created through BFVR's x86 loader (requested API %u.%u.%u).",
        static_cast<unsigned>(XR_VERSION_MAJOR(
            bfvr::kRequestedOpenXRApiVersion)),
        static_cast<unsigned>(XR_VERSION_MINOR(
            bfvr::kRequestedOpenXRApiVersion)),
        static_cast<unsigned>(XR_VERSION_PATCH(
            bfvr::kRequestedOpenXRApiVersion)));

    PFN_xrGetInstanceProperties getInstanceProperties = nullptr;
    if (ResolveOpenXRFunction(getInstanceProcAddr, instance, "xrGetInstanceProperties", getInstanceProperties, logCallback, logContext))
    {
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
        const XrResult propertiesResult = getInstanceProperties(instance, &properties);
        if (XR_SUCCEEDED(propertiesResult))
        {
            WriteLog(
                logCallback,
                logContext,
                L"OpenXR runtime=%S version=%u.%u.%u.",
                properties.runtimeName,
                static_cast<unsigned>(XR_VERSION_MAJOR(properties.runtimeVersion)),
                static_cast<unsigned>(XR_VERSION_MINOR(properties.runtimeVersion)),
                static_cast<unsigned>(XR_VERSION_PATCH(properties.runtimeVersion)));
        }
        else
        {
            WriteLog(logCallback, logContext, L"OpenXR runtime-properties query returned XrResult=%ld.", static_cast<long>(propertiesResult));
        }
    }

    PFN_xrGetSystem getSystem = nullptr;
    bool systemAvailable = false;
    if (ResolveOpenXRFunction(getInstanceProcAddr, instance, "xrGetSystem", getSystem, logCallback, logContext))
    {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId systemId = XR_NULL_SYSTEM_ID;
        const XrResult systemResult = getSystem(instance, &systemInfo, &systemId);
        systemAvailable = XR_SUCCEEDED(systemResult) && systemId != XR_NULL_SYSTEM_ID;
        if (systemAvailable)
        {
            WriteLog(logCallback, logContext, L"OpenXR HMD system is available (id=%llu).", static_cast<unsigned long long>(systemId));
            if (graphicsExtensions.d3d11Available)
            {
                // Oculus rejects an extension-enabled instance while no HMD is
                // present. Probe availability through the baseline instance,
                // then create a second short-lived instance only when the HMD
                // exists and graphics requirements can actually be queried.
                if (DestroyOpenXRInstance(getInstanceProcAddr, instance, logCallback, logContext))
                {
                    const char* const enabledExtensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
                    XrInstanceCreateInfo d3d11CreateInfo = createInfo;
                    d3d11CreateInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(enabledExtensions));
                    d3d11CreateInfo.enabledExtensionNames = enabledExtensions;
                    WriteLog(logCallback, logContext, L"OpenXR HMD is available; creating a second short-lived instance with XR_KHR_D3D11_enable for graphics-requirements preflight only.");
                    const XrResult d3d11CreateResult = createInstance(&d3d11CreateInfo, &instance);
                    if (XR_FAILED(d3d11CreateResult) || instance == XR_NULL_HANDLE)
                    {
                        WriteLog(
                            logCallback,
                            logContext,
                            L"OpenXR D3D11 preflight instance creation returned %s (%ld); flat fallback remains active.",
                            DescribeOpenXRResult(d3d11CreateResult),
                            static_cast<long>(d3d11CreateResult));
                        instance = XR_NULL_HANDLE;
                    }
                    else
                    {
                        PFN_xrGetSystem d3d11GetSystem = nullptr;
                        XrSystemId d3d11SystemId = XR_NULL_SYSTEM_ID;
                        XrSystemGetInfo d3d11SystemInfo{XR_TYPE_SYSTEM_GET_INFO};
                        d3d11SystemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
                        if (ResolveOpenXRFunction(getInstanceProcAddr, instance, "xrGetSystem", d3d11GetSystem, logCallback, logContext))
                        {
                            const XrResult d3d11SystemResult = d3d11GetSystem(instance, &d3d11SystemInfo, &d3d11SystemId);
                            if (XR_SUCCEEDED(d3d11SystemResult) && d3d11SystemId != XR_NULL_SYSTEM_ID)
                            {
                                QueryD3D11GraphicsRequirements(getInstanceProcAddr, instance, d3d11SystemId, logCallback, logContext);
                            }
                            else
                            {
                                WriteLog(logCallback, logContext, L"OpenXR D3D11 preflight system query returned %s (%ld); no device or session was created.", DescribeOpenXRResult(d3d11SystemResult), static_cast<long>(d3d11SystemResult));
                            }
                        }
                    }
                }
            }
        }
        else
        {
            WriteLog(
                logCallback,
                logContext,
                L"OpenXR HMD system query returned %s (%ld); this is expected when no headset is available.",
                DescribeOpenXRResult(systemResult),
                static_cast<long>(systemResult));
            if (graphicsExtensions.d3d11Available)
            {
                WriteLog(logCallback, logContext, L"OpenXR D3D11 graphics-requirements preflight awaits an available HMD system; no D3D11 device or session was created.");
            }
        }
    }

    DestroyOpenXRInstance(getInstanceProcAddr, instance, logCallback, logContext);

    FreeLibrary(loader);
    return systemAvailable;
}
}
