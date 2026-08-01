#include "client/MainMenuOverlay.h"

#include "stereo/MainMenuOverlayLayout.h"

#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <utility>

namespace
{

constexpr UINT kExpectedButtonWidth = 256;
constexpr UINT kExpectedButtonHeight = 64;
constexpr wchar_t kBackgroundName[] = L"BackToGame_bg.png";
constexpr wchar_t kHoverName[] = L"BackToGame_hover.png";
constexpr wchar_t kTextName[] = L"BackToGame_text.png";
constexpr wchar_t kBorderName[] = L"BackToGame_BorderFrame.png";
constexpr wchar_t kHoverBorderName[] =
    L"BackToGame_BorderFrameHover.png";
int g_moduleAnchor = 0;

template <typename T>
void ReleaseInterface(T*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
}

std::wstring ParentDirectory(const std::wstring& path)
{
    const std::wstring::size_type separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring{}
        : path.substr(0, separator);
}

std::wstring JoinPath(
    const std::wstring& directory,
    const wchar_t* child)
{
    if (directory.empty() || child == nullptr || child[0] == L'\0')
    {
        return {};
    }
    std::wstring result = directory;
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(child);
    return result;
}

std::wstring ModuleDirectory(HMODULE module)
{
    std::array<wchar_t, 32768> path = {};
    const DWORD length = GetModuleFileNameW(
        module,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return {};
    }
    return ParentDirectory(std::wstring(path.data(), length));
}

bool IsDirectory(const std::wstring& path) noexcept
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::uint8_t Channel(std::uint32_t pixel, unsigned int shift) noexcept
{
    return static_cast<std::uint8_t>((pixel >> shift) & 0xFFU);
}

std::uint32_t PackPixel(
    std::uint32_t blue,
    std::uint32_t green,
    std::uint32_t red,
    std::uint32_t alpha) noexcept
{
    return std::min(blue, 255U) |
        (std::min(green, 255U) << 8U) |
        (std::min(red, 255U) << 16U) |
        (std::min(alpha, 255U) << 24U);
}

std::uint32_t BlendPremultiplied(
    std::uint32_t source,
    std::uint32_t destination) noexcept
{
    const std::uint32_t sourceAlpha = Channel(source, 24U);
    const std::uint32_t inverseAlpha = 255U - sourceAlpha;
    const auto blendChannel = [inverseAlpha](
                                  std::uint32_t sourceChannel,
                                  std::uint32_t destinationChannel) {
        return sourceChannel +
            (destinationChannel * inverseAlpha + 127U) / 255U;
    };
    return PackPixel(
        blendChannel(Channel(source, 0U), Channel(destination, 0U)),
        blendChannel(Channel(source, 8U), Channel(destination, 8U)),
        blendChannel(Channel(source, 16U), Channel(destination, 16U)),
        blendChannel(sourceAlpha, Channel(destination, 24U)));
}

std::uint32_t InterpolateChannel(
    std::uint32_t topLeft,
    std::uint32_t topRight,
    std::uint32_t bottomLeft,
    std::uint32_t bottomRight,
    float xFraction,
    float yFraction,
    unsigned int shift) noexcept
{
    const float top =
        static_cast<float>(Channel(topLeft, shift)) +
        (static_cast<float>(Channel(topRight, shift)) -
         static_cast<float>(Channel(topLeft, shift))) * xFraction;
    const float bottom =
        static_cast<float>(Channel(bottomLeft, shift)) +
        (static_cast<float>(Channel(bottomRight, shift)) -
         static_cast<float>(Channel(bottomLeft, shift))) * xFraction;
    return static_cast<std::uint32_t>(std::clamp(
        top + (bottom - top) * yFraction + 0.5F,
        0.0F,
        255.0F));
}

template <typename ImageType>
std::uint32_t SampleBilinearPremultiplied(
    const ImageType& image,
    float x,
    float y) noexcept
{
    if (image.pixels.empty() || image.width == 0 || image.height == 0)
    {
        return 0;
    }
    const float clampedX = std::clamp(
        x,
        0.0F,
        static_cast<float>(image.width - 1));
    const float clampedY = std::clamp(
        y,
        0.0F,
        static_cast<float>(image.height - 1));
    const UINT x0 = static_cast<UINT>(clampedX);
    const UINT y0 = static_cast<UINT>(clampedY);
    const UINT x1 = std::min(x0 + 1, image.width - 1);
    const UINT y1 = std::min(y0 + 1, image.height - 1);
    const float xFraction = clampedX - static_cast<float>(x0);
    const float yFraction = clampedY - static_cast<float>(y0);
    const std::uint32_t topLeft =
        image.pixels[static_cast<std::size_t>(y0) * image.width + x0];
    const std::uint32_t topRight =
        image.pixels[static_cast<std::size_t>(y0) * image.width + x1];
    const std::uint32_t bottomLeft =
        image.pixels[static_cast<std::size_t>(y1) * image.width + x0];
    const std::uint32_t bottomRight =
        image.pixels[static_cast<std::size_t>(y1) * image.width + x1];
    return PackPixel(
        InterpolateChannel(
            topLeft,
            topRight,
            bottomLeft,
            bottomRight,
            xFraction,
            yFraction,
            0U),
        InterpolateChannel(
            topLeft,
            topRight,
            bottomLeft,
            bottomRight,
            xFraction,
            yFraction,
            8U),
        InterpolateChannel(
            topLeft,
            topRight,
            bottomLeft,
            bottomRight,
            xFraction,
            yFraction,
            16U),
        InterpolateChannel(
            topLeft,
            topRight,
            bottomLeft,
            bottomRight,
            xFraction,
            yFraction,
            24U));
}

} // namespace

namespace bfvr
{

bool MainMenuOverlay::Initialize(LogCallback appendLog)
{
    return InitializeFromDirectory(FindAssetsDirectory(), appendLog);
}

bool MainMenuOverlay::InitializeFromDirectory(
    const std::wstring& assetsDirectory,
    LogCallback appendLog)
{
    Reset();
    appendLog_ = appendLog;
    if (assetsDirectory.empty() || !IsDirectory(assetsDirectory))
    {
        WriteLog(
            L"Back-to-game main-menu overlay is unavailable: BFVR's assets directory was not found.");
        return false;
    }
    assetsDirectory_ = assetsDirectory;
    if (!LoadButtonImages(assetsDirectory_))
    {
        WriteLog(
            L"Back-to-game main-menu overlay is unavailable: one or more 256x64 PNG layers could not be decoded from %s.",
            assetsDirectory_.c_str());
        Reset();
        appendLog_ = appendLog;
        return false;
    }
    const stereo::UiCanvasRect buttonRect =
        stereo::BackToGameButtonRect();
    WriteLog(
        L"Back-to-game main-menu overlay loaded five aligned 256x64 PNG layers from %s; authoredScales=(%.2f,%.2f), logical rectangle=(%.1f,%.1f)-(%.1f,%.1f), bottomMargin=%.1f.",
        assetsDirectory_.c_str(),
        stereo::kBackToGameButtonWidthScale,
        stereo::kBackToGameButtonHeightScale,
        buttonRect.left,
        buttonRect.top,
        buttonRect.right,
        buttonRect.bottom,
        stereo::kBackToGameButtonBottomMargin);
    return true;
}

void MainMenuOverlay::Reset() noexcept
{
    normal_ = {};
    hovered_ = {};
    appendLog_ = nullptr;
    assetsDirectory_.clear();
}

void MainMenuOverlay::Composite(
    std::vector<DWORD>& destination,
    UINT destinationWidth,
    UINT destinationHeight,
    bool visible,
    bool hovered) const noexcept
{
    const Image& source = hovered ? hovered_ : normal_;
    const std::uint64_t requiredPixels =
        static_cast<std::uint64_t>(destinationWidth) *
        static_cast<std::uint64_t>(destinationHeight);
    if (!visible || !IsReady() || destinationWidth == 0 ||
        destinationHeight == 0 ||
        requiredPixels > std::numeric_limits<std::size_t>::max() ||
        destination.size() < static_cast<std::size_t>(requiredPixels))
    {
        return;
    }

    const stereo::UiCanvasRect logical =
        stereo::BackToGameButtonRect();
    const auto scaleX = [destinationWidth](float value) {
        return value * static_cast<float>(destinationWidth) /
            stereo::kMainMenuCanvasWidth;
    };
    const auto scaleY = [destinationHeight](float value) {
        return value * static_cast<float>(destinationHeight) /
            stereo::kMainMenuCanvasHeight;
    };
    const int left = std::clamp(
        static_cast<int>(scaleX(logical.left) + 0.5F),
        0,
        static_cast<int>(destinationWidth));
    const int top = std::clamp(
        static_cast<int>(scaleY(logical.top) + 0.5F),
        0,
        static_cast<int>(destinationHeight));
    const int right = std::clamp(
        static_cast<int>(scaleX(logical.right) + 0.5F),
        left,
        static_cast<int>(destinationWidth));
    const int bottom = std::clamp(
        static_cast<int>(scaleY(logical.bottom) + 0.5F),
        top,
        static_cast<int>(destinationHeight));
    const int drawWidth = right - left;
    const int drawHeight = bottom - top;
    if (drawWidth <= 0 || drawHeight <= 0)
    {
        return;
    }

    for (int destinationY = 0; destinationY < drawHeight; ++destinationY)
    {
        const float sourceY =
            (static_cast<float>(destinationY) + 0.5F) *
                static_cast<float>(source.height) /
                static_cast<float>(drawHeight) -
            0.5F;
        for (int destinationX = 0;
             destinationX < drawWidth;
             ++destinationX)
        {
            const float sourceX =
                (static_cast<float>(destinationX) + 0.5F) *
                    static_cast<float>(source.width) /
                    static_cast<float>(drawWidth) -
                0.5F;
            const std::uint32_t sourcePixel =
                SampleBilinearPremultiplied(source, sourceX, sourceY);
            const std::size_t destinationIndex =
                static_cast<std::size_t>(top + destinationY) *
                    destinationWidth +
                static_cast<std::size_t>(left + destinationX);
            destination[destinationIndex] = BlendPremultiplied(
                sourcePixel,
                destination[destinationIndex] | 0xFF000000U);
        }
    }
}

bool MainMenuOverlay::CopyButtonPixels(
    bool hovered,
    std::vector<std::uint32_t>& pixels,
    UINT& width,
    UINT& height) const
{
    pixels.clear();
    width = 0;
    height = 0;
    if (!IsReady())
    {
        return false;
    }
    const Image& source = hovered ? hovered_ : normal_;
    pixels = source.pixels;
    width = source.width;
    height = source.height;
    return true;
}

bool MainMenuOverlay::IsReady() const noexcept
{
    return normal_.width == kExpectedButtonWidth &&
        normal_.height == kExpectedButtonHeight &&
        normal_.pixels.size() ==
            static_cast<std::size_t>(kExpectedButtonWidth) *
                kExpectedButtonHeight &&
        hovered_.width == normal_.width &&
        hovered_.height == normal_.height &&
        hovered_.pixels.size() == normal_.pixels.size();
}

bool MainMenuOverlay::LoadButtonImages(
    const std::wstring& assetsDirectory)
{
    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom =
        comStatus == S_OK || comStatus == S_FALSE;
    if (FAILED(comStatus) && comStatus != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    const HRESULT factoryStatus = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(factoryStatus) || factory == nullptr)
    {
        if (uninitializeCom)
        {
            CoUninitialize();
        }
        return false;
    }

    Image background = {};
    Image hover = {};
    Image text = {};
    Image border = {};
    Image hoverBorder = {};
    const bool loaded =
        LoadPng(factory, JoinPath(assetsDirectory, kBackgroundName), background) &&
        LoadPng(factory, JoinPath(assetsDirectory, kHoverName), hover) &&
        LoadPng(factory, JoinPath(assetsDirectory, kTextName), text) &&
        LoadPng(factory, JoinPath(assetsDirectory, kBorderName), border) &&
        LoadPng(factory, JoinPath(assetsDirectory, kHoverBorderName), hoverBorder);
    ReleaseInterface(factory);
    if (uninitializeCom)
    {
        CoUninitialize();
    }
    if (!loaded)
    {
        return false;
    }

    const auto expected = [](const Image& image) {
        return image.width == kExpectedButtonWidth &&
            image.height == kExpectedButtonHeight;
    };
    if (!expected(background) || !expected(hover) || !expected(text) ||
        !expected(border) || !expected(hoverBorder))
    {
        return false;
    }

    normal_ = {};
    normal_.width = kExpectedButtonWidth;
    normal_.height = kExpectedButtonHeight;
    normal_.pixels.assign(
        static_cast<std::size_t>(kExpectedButtonWidth) *
            kExpectedButtonHeight,
        0U);
    hovered_ = normal_;
    return CompositeLayer(background, normal_) &&
        CompositeLayer(text, normal_) &&
        CompositeLayer(border, normal_) &&
        CompositeLayer(background, hovered_) &&
        CompositeLayer(hover, hovered_) &&
        CompositeLayer(text, hovered_) &&
        CompositeLayer(hoverBorder, hovered_);
}

bool MainMenuOverlay::LoadPng(
    void* imagingFactory,
    const std::wstring& path,
    Image& image)
{
    auto* const factory = static_cast<IWICImagingFactory*>(imagingFactory);
    if (factory == nullptr)
    {
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    HRESULT status = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (SUCCEEDED(status))
    {
        status = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(status))
    {
        status = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(status))
    {
        status = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(status))
    {
        status = converter->GetSize(&width, &height);
    }
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * height;
    if (SUCCEEDED(status) &&
        (width == 0 || height == 0 ||
         pixelCount > std::numeric_limits<std::size_t>::max() /
             sizeof(std::uint32_t) ||
         pixelCount > std::numeric_limits<UINT>::max() /
             sizeof(std::uint32_t) ||
         width > std::numeric_limits<UINT>::max() / sizeof(std::uint32_t)))
    {
        status = E_INVALIDARG;
    }
    std::vector<std::uint32_t> pixels;
    if (SUCCEEDED(status))
    {
        pixels.resize(static_cast<std::size_t>(pixelCount));
        status = converter->CopyPixels(
            nullptr,
            width * sizeof(std::uint32_t),
            static_cast<UINT>(pixels.size() * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(pixels.data()));
    }
    ReleaseInterface(converter);
    ReleaseInterface(frame);
    ReleaseInterface(decoder);
    if (FAILED(status))
    {
        return false;
    }
    image.width = width;
    image.height = height;
    image.pixels = std::move(pixels);
    return true;
}

bool MainMenuOverlay::CompositeLayer(
    const Image& source,
    Image& destination)
{
    if (source.width == 0 || source.height == 0 ||
        source.width != destination.width ||
        source.height != destination.height ||
        source.pixels.size() != destination.pixels.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < source.pixels.size(); ++index)
    {
        destination.pixels[index] = BlendPremultiplied(
            source.pixels[index],
            destination.pixels[index]);
    }
    return true;
}

std::wstring MainMenuOverlay::FindAssetsDirectory()
{
    HMODULE clientModule = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&g_moduleAnchor),
            &clientModule))
    {
        const std::wstring besideClient = JoinPath(
            ModuleDirectory(clientModule),
            L"assets");
        if (IsDirectory(besideClient))
        {
            return besideClient;
        }
    }

    const std::wstring sourceAssets = JoinPath(
        ModuleDirectory(GetModuleHandleW(nullptr)),
        L"BFVR\\assets");
    return IsDirectory(sourceAssets) ? sourceAssets : std::wstring{};
}

void MainMenuOverlay::WriteLog(const wchar_t* format, ...) const
{
    if (appendLog_ == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1200> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    appendLog_(message.data());
}

} // namespace bfvr
