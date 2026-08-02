#include "client/QuickMenuArt.h"

#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cwchar>
#include <limits>
#include <utility>

namespace
{
constexpr UINT kExpectedMenuWidth = 512;
constexpr UINT kExpectedMenuHeight = 512;
constexpr wchar_t kBackgroundName[] = L"QM_bg.png";
constexpr wchar_t kFrameName[] = L"QM_frame.png";
constexpr wchar_t kTextName[] = L"QM_text.png";
constexpr wchar_t kCursorName[] = L"QM_Cursor.png";
constexpr std::array<const wchar_t*, 12> kHoverNames = {
    L"QM_menuhover.png",
    L"QM_deployhover.png",
    L"QM_1hover.png",
    L"QM_2hover.png",
    L"QM_3hover.png",
    L"QM_4hover.png",
    L"QM_5hover.png",
    L"QM_6hover.png",
    L"QM_F9hover.png",
    L"QM_F10hover.png",
    L"QM_F11hover.png",
    L"QM_F12hover.png"};

template <typename T>
void ReleaseInterface(T*& value) noexcept
{
    if (value != nullptr)
    {
        value->Release();
        value = nullptr;
    }
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

std::uint32_t MakePremultipliedPixel(
    std::uint8_t blue,
    std::uint8_t green,
    std::uint8_t red,
    std::uint8_t alpha) noexcept
{
    const auto premultiply = [alpha](std::uint8_t channel) {
        return (static_cast<std::uint32_t>(channel) * alpha + 127U) / 255U;
    };
    return PackPixel(
        premultiply(blue),
        premultiply(green),
        premultiply(red),
        alpha);
}

void FillRectangle(
    std::vector<std::uint32_t>& pixels,
    UINT width,
    UINT height,
    UINT left,
    UINT top,
    UINT right,
    UINT bottom,
    std::uint32_t color) noexcept
{
    left = std::min(left, width);
    right = std::min(right, width);
    top = std::min(top, height);
    bottom = std::min(bottom, height);
    for (UINT y = top; y < bottom; ++y)
    {
        const std::size_t row = static_cast<std::size_t>(y) * width;
        std::fill(
            pixels.begin() + row + left,
            pixels.begin() + row + right,
            color);
    }
}

void DrawPlaceholderGlyph(
    std::vector<std::uint32_t>& pixels,
    UINT width,
    UINT height,
    UINT button,
    const std::array<std::uint8_t, 7>& rows,
    std::uint32_t color) noexcept
{
    constexpr UINT scale = 7;
    constexpr UINT glyphWidth = 5 * scale;
    constexpr UINT glyphHeight = 7 * scale;
    const UINT buttonWidth = width /
        static_cast<UINT>(bfvr::stereo::kQuickMenuUtilityButtonCount);
    const UINT originX = button * buttonWidth +
        (buttonWidth - glyphWidth) / 2;
    const UINT originY = (height - glyphHeight) / 2;
    for (UINT row = 0; row < rows.size(); ++row)
    {
        for (UINT column = 0; column < 5; ++column)
        {
            if ((rows[row] & (1U << (4U - column))) == 0)
            {
                continue;
            }
            FillRectangle(
                pixels,
                width,
                height,
                originX + column * scale,
                originY + row * scale,
                originX + (column + 1) * scale - 1,
                originY + (row + 1) * scale - 1,
                color);
        }
    }
}
} // namespace

namespace bfvr
{

bool QuickMenuArt::InitializeFromDirectory(
    const std::wstring& assetsDirectory,
    LogCallback logCallback,
    void* logContext)
{
    Reset();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (assetsDirectory.empty() || !IsDirectory(assetsDirectory))
    {
        WriteLog(
            L"Quick Menu is unavailable: BFVR's assets directory was not found.");
        return false;
    }

    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = comStatus == S_OK || comStatus == S_FALSE;
    if (FAILED(comStatus) && comStatus != RPC_E_CHANGED_MODE)
    {
        WriteLog(
            L"Quick Menu could not initialize Windows imaging (HRESULT=0x%08lX).",
            static_cast<unsigned long>(comStatus));
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
        WriteLog(
            L"Quick Menu could not create its Windows imaging factory (HRESULT=0x%08lX).",
            static_cast<unsigned long>(factoryStatus));
        return false;
    }

    Image background = {};
    Image frame = {};
    Image text = {};
    std::array<Image, kHoverNames.size()> hovers = {};
    bool loaded =
        LoadPng(factory, JoinPath(assetsDirectory, kBackgroundName), background) &&
        LoadPng(factory, JoinPath(assetsDirectory, kFrameName), frame) &&
        LoadPng(factory, JoinPath(assetsDirectory, kTextName), text) &&
        LoadPng(factory, JoinPath(assetsDirectory, kCursorName), cursor_);
    for (std::size_t index = 0; loaded && index < hovers.size(); ++index)
    {
        loaded = LoadPng(
            factory,
            JoinPath(assetsDirectory, kHoverNames[index]),
            hovers[index]);
    }
    ReleaseInterface(factory);
    if (uninitializeCom)
    {
        CoUninitialize();
    }

    const auto isMenuSize = [](const Image& image) {
        return image.width == kExpectedMenuWidth &&
            image.height == kExpectedMenuHeight &&
            image.pixels.size() ==
                static_cast<std::size_t>(kExpectedMenuWidth) *
                    kExpectedMenuHeight;
    };
    if (!loaded || !isMenuSize(background) || !isMenuSize(frame) ||
        !isMenuSize(text) || cursor_.width == 0 || cursor_.height == 0 ||
        cursor_.pixels.empty() ||
        !std::all_of(hovers.begin(), hovers.end(), isMenuSize))
    {
        WriteLog(
            L"Quick Menu is unavailable: every menu layer must decode as 512x512 and the cursor must be a nonempty PNG in %s.",
            assetsDirectory.c_str());
        Reset();
        logCallback_ = logCallback;
        logContext_ = logContext;
        return false;
    }

    Image base = {};
    base.width = kExpectedMenuWidth;
    base.height = kExpectedMenuHeight;
    base.pixels.assign(
        static_cast<std::size_t>(base.width) * base.height,
        0U);
    if (!CompositeLayer(background, base) ||
        !CompositeLayer(text, base) ||
        !CompositeLayer(frame, base))
    {
        Reset();
        logCallback_ = logCallback;
        logContext_ = logContext;
        return false;
    }
    menus_[static_cast<std::size_t>(
        stereo::QuickMenuSelection::None)] = base;
    for (std::size_t index = 0; index < hovers.size(); ++index)
    {
        Image variant = {};
        variant.width = base.width;
        variant.height = base.height;
        variant.pixels.assign(base.pixels.size(), 0U);
        if (!CompositeLayer(background, variant) ||
            !CompositeLayer(hovers[index], variant) ||
            !CompositeLayer(text, variant) ||
            !CompositeLayer(frame, variant))
        {
            Reset();
            logCallback_ = logCallback;
            logContext_ = logContext;
            return false;
        }
        menus_[index + 1] = std::move(variant);
    }
    for (std::size_t index = static_cast<std::size_t>(
             stereo::QuickMenuSelection::MountedCameraDecouple);
         index < menus_.size();
         ++index)
    {
        // Utility hover is rendered in the separate strip, so the authored
        // square menu remains on its neutral variant.
        menus_[index] = base;
    }
    WriteLog(
        L"Quick Menu loaded background -> hover -> text -> final frame variants, a %ux%u top-left-hotspot cursor, and the temporary three-button utility strip from %s.",
        cursor_.width,
        cursor_.height,
        assetsDirectory.c_str());
    return true;
}

void QuickMenuArt::Reset() noexcept
{
    menus_ = {};
    cursor_ = {};
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

bool QuickMenuArt::CopyMenuPixels(
    stereo::QuickMenuSelection selection,
    std::vector<std::uint32_t>& pixels,
    UINT& width,
    UINT& height) const
{
    pixels.clear();
    width = 0;
    height = 0;
    const std::size_t index = static_cast<std::size_t>(selection);
    if (!IsReady() || index >= menus_.size())
    {
        return false;
    }
    const Image& source = menus_[index];
    pixels = source.pixels;
    width = source.width;
    height = source.height;
    return true;
}

bool QuickMenuArt::CopyCursorPixels(
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
    pixels = cursor_.pixels;
    width = cursor_.width;
    height = cursor_.height;
    return true;
}

bool QuickMenuArt::CopyUtilityStripPixels(
    stereo::QuickMenuSelection hovered,
    bool mountedCameraDecoupled,
    std::vector<std::uint32_t>& pixels,
    UINT& width,
    UINT& height) const
{
    width = stereo::kQuickMenuUtilityTextureWidth;
    height = stereo::kQuickMenuUtilityTextureHeight;
    pixels.assign(static_cast<std::size_t>(width) * height, 0U);
    if (!IsReady())
    {
        pixels.clear();
        width = 0;
        height = 0;
        return false;
    }

    constexpr UINT buttonWidth =
        stereo::kQuickMenuUtilityTextureWidth /
        static_cast<UINT>(stereo::kQuickMenuUtilityButtonCount);
    const std::uint32_t outline = MakePremultipliedPixel(
        174, 184, 188, 236);
    const std::uint32_t glyph = MakePremultipliedPixel(
        230, 238, 240, 244);
    for (UINT button = 0;
         button < stereo::kQuickMenuUtilityButtonCount;
         ++button)
    {
        const stereo::QuickMenuSelection selection =
            static_cast<stereo::QuickMenuSelection>(
                static_cast<std::uint32_t>(
                    stereo::QuickMenuSelection::MountedCameraDecouple) +
                button);
        const bool highlighted = hovered == selection;
        const bool active = button == 0 && mountedCameraDecoupled;
        const std::uint32_t fill = active
            ? MakePremultipliedPixel(
                highlighted ? 112 : 86,
                highlighted ? 196 : 164,
                highlighted ? 126 : 96,
                226)
            : MakePremultipliedPixel(
                highlighted ? 88 : 54,
                highlighted ? 104 : 66,
                highlighted ? 114 : 74,
                218);
        const UINT left = button * buttonWidth + 4;
        const UINT right = (button + 1) * buttonWidth - 4;
        FillRectangle(pixels, width, height, left, 4, right, height - 4, outline);
        FillRectangle(
            pixels,
            width,
            height,
            left + 3,
            7,
            right - 3,
            height - 7,
            fill);
    }

    constexpr std::array<std::uint8_t, 7> decoupleGlyph = {
        0b11110, 0b10001, 0b10001, 0b10001,
        0b10001, 0b10001, 0b11110};
    constexpr std::array<std::uint8_t, 7> secondGlyph = {
        0b11110, 0b00001, 0b00001, 0b11110,
        0b10000, 0b10000, 0b11111};
    constexpr std::array<std::uint8_t, 7> thirdGlyph = {
        0b11110, 0b00001, 0b00001, 0b01110,
        0b00001, 0b00001, 0b11110};
    DrawPlaceholderGlyph(
        pixels, width, height, 0, decoupleGlyph, glyph);
    DrawPlaceholderGlyph(
        pixels, width, height, 1, secondGlyph, glyph);
    DrawPlaceholderGlyph(
        pixels, width, height, 2, thirdGlyph, glyph);
    return true;
}

bool QuickMenuArt::IsReady() const noexcept
{
    if (cursor_.width == 0 || cursor_.height == 0 || cursor_.pixels.empty())
    {
        return false;
    }
    return std::all_of(
        menus_.begin(),
        menus_.end(),
        [](const Image& image) {
            return image.width == kExpectedMenuWidth &&
                image.height == kExpectedMenuHeight &&
                image.pixels.size() ==
                    static_cast<std::size_t>(kExpectedMenuWidth) *
                        kExpectedMenuHeight;
        });
}

bool QuickMenuArt::LoadPng(
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
         width > std::numeric_limits<UINT>::max() /
             sizeof(std::uint32_t)))
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

bool QuickMenuArt::CompositeLayer(
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

void QuickMenuArt::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr || format == nullptr)
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
    logCallback_(logContext_, message.data());
}

} // namespace bfvr
