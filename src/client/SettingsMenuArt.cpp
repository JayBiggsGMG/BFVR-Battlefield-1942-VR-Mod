#include "client/SettingsMenuArt.h"

#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <limits>
#include <utility>

namespace
{
constexpr UINT kExpectedSize = 1024;
constexpr std::array<const wchar_t*, 2> kArrowNames = {
    L"ArrowLeft.png", L"ArrowRight.png"};
constexpr std::array<const wchar_t*, 3> kBottomFrameNames = {
    L"SaveButtonFrame.png",
    L"CancelButtonFrame.png",
    L"DefaultsButtonFrame.png"};
constexpr std::array<const wchar_t*, 3> kSelectedTabNames = {
    L"Tab1Selected.png", L"Tab2Selected.png", L"Tab3Selected.png"};
constexpr std::array<const wchar_t*, 6> kHoverNames = {
    L"ConLayoutHover.png",
    L"ArrowLeftHover.png",
    L"ArrowRightHover.png",
    L"SaveButtonHover.png",
    L"CancelButtonHover.png",
    L"DefaultsButtonHover.png"};

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

std::size_t HoverIndex(
    bfvr::stereo::SettingsMenuSelection selection) noexcept
{
    using bfvr::stereo::SettingsMenuSelection;
    switch (selection)
    {
    case SettingsMenuSelection::ControllerLayout: return 0;
    case SettingsMenuSelection::ArrowLeft: return 1;
    case SettingsMenuSelection::ArrowRight: return 2;
    case SettingsMenuSelection::Save: return 3;
    case SettingsMenuSelection::Cancel: return 4;
    case SettingsMenuSelection::ResetDefaults: return 5;
    default: return 6;
    }
}
} // namespace

namespace bfvr
{

bool SettingsMenuArt::InitializeFromDirectory(
    const std::wstring& settingsDirectory,
    LogCallback logCallback,
    void* logContext)
{
    Reset();
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (settingsDirectory.empty() || !IsDirectory(settingsDirectory))
    {
        WriteLog(
            L"VR Settings menu is unavailable: its SettingsMenu asset directory was not found.");
        return false;
    }

    const HRESULT comStatus = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = comStatus == S_OK || comStatus == S_FALSE;
    if (FAILED(comStatus) && comStatus != RPC_E_CHANGED_MODE)
    {
        WriteLog(
            L"VR Settings menu could not initialize Windows imaging (HRESULT=0x%08lX).",
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
            L"VR Settings menu could not create its Windows imaging factory (HRESULT=0x%08lX).",
            static_cast<unsigned long>(factoryStatus));
        return false;
    }

    bool loaded =
        LoadPng(factory, JoinPath(settingsDirectory, L"TransBG.png"), transBackground_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"ButtonsBG.png"), buttonsBackground_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"ConLayoutOverlay.png"), controllerOverlay_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"ConLayoutButton.png"), controllerButton_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"FrameBorder.png"), frameBorder_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"CheckBox.png"), checkBox_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"SliderBarBG.png"), sliderBar_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"NumberBox.png"), numberBox_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"WhiteButton.png"), whiteButton_) &&
        LoadPng(factory, JoinPath(settingsDirectory, L"SettingsText.png"), text_);
    for (std::size_t index = 0; loaded && index < arrows_.size(); ++index)
    {
        loaded = LoadPng(
            factory,
            JoinPath(settingsDirectory, kArrowNames[index]),
            arrows_[index]);
    }
    for (std::size_t index = 0;
         loaded && index < bottomFrames_.size();
         ++index)
    {
        loaded = LoadPng(
            factory,
            JoinPath(settingsDirectory, kBottomFrameNames[index]),
            bottomFrames_[index]);
    }
    for (std::size_t index = 0;
         loaded && index < selectedTabs_.size();
         ++index)
    {
        loaded = LoadPng(
            factory,
            JoinPath(settingsDirectory, kSelectedTabNames[index]),
            selectedTabs_[index]);
    }
    for (std::size_t index = 0; loaded && index < hovers_.size(); ++index)
    {
        loaded = LoadPng(
            factory,
            JoinPath(settingsDirectory, kHoverNames[index]),
            hovers_[index]);
    }
    ReleaseInterface(factory);
    if (uninitializeCom)
    {
        CoUninitialize();
    }

    if (!loaded || !IsReady())
    {
        WriteLog(
            L"VR Settings menu is unavailable: every full-panel layer must decode as 1024x1024 in %s.",
            settingsDirectory.c_str());
        Reset();
        logCallback_ = logCallback;
        logContext_ = logContext;
        return false;
    }
    WriteLog(
        L"VR Settings menu loaded its authored layer stack and reusable slider, checkbox, number-box, and white-button controls from %s.",
        settingsDirectory.c_str());
    return true;
}

void SettingsMenuArt::Reset() noexcept
{
    transBackground_ = {};
    buttonsBackground_ = {};
    controllerOverlay_ = {};
    controllerButton_ = {};
    frameBorder_ = {};
    arrows_ = {};
    bottomFrames_ = {};
    selectedTabs_ = {};
    hovers_ = {};
    checkBox_ = {};
    sliderBar_ = {};
    numberBox_ = {};
    whiteButton_ = {};
    text_ = {};
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

bool SettingsMenuArt::Compose(
    const stereo::SettingsMenuSnapshot& state,
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
    Image result = {};
    result.width = kExpectedSize;
    result.height = kExpectedSize;
    result.pixels.assign(
        static_cast<std::size_t>(kExpectedSize) * kExpectedSize,
        0U);
    bool composed = CompositeLayer(transBackground_, result) &&
        CompositeLayer(buttonsBackground_, result);
    if (composed)
    {
        // Settings controls intentionally sit below ConLayoutOverlay. The
        // interaction layer also disables their hit targets while it is open.
        composed = ComposeSettingsBody(state, result);
    }
    if (composed && state.controllerLayoutVisible)
    {
        composed = CompositeLayer(controllerOverlay_, result);
    }
    composed = composed && CompositeLayer(controllerButton_, result);
    composed = composed && CompositeLayer(frameBorder_, result);
    const std::size_t tabIndex = static_cast<std::size_t>(state.tab);
    if (composed && tabIndex < selectedTabs_.size())
    {
        // FrameBorder's authored top bar is opaque. Selected-tab art must sit
        // above it or the state changes correctly but remain visually masked.
        composed = CompositeLayer(selectedTabs_[tabIndex], result);
    }
    if (composed)
    {
        composed = DrawStatusField(result, state.status);
    }
    if (composed && state.arrowLeftVisible)
    {
        composed = CompositeLayer(arrows_[0], result);
    }
    if (composed && state.arrowRightVisible)
    {
        composed = CompositeLayer(arrows_[1], result);
    }
    for (const Image& frame : bottomFrames_)
    {
        composed = composed && CompositeLayer(frame, result);
    }
    const std::size_t hoverIndex = HoverIndex(state.hovered);
    if (composed && hoverIndex < hovers_.size() &&
        (state.hovered != stereo::SettingsMenuSelection::ArrowLeft ||
         state.arrowLeftVisible) &&
        (state.hovered != stereo::SettingsMenuSelection::ArrowRight ||
         state.arrowRightVisible))
    {
        composed = CompositeLayer(hovers_[hoverIndex], result);
    }
    composed = composed && CompositeLayer(text_, result);
    if (!composed)
    {
        return false;
    }
    pixels = std::move(result.pixels);
    width = result.width;
    height = result.height;
    return true;
}

bool SettingsMenuArt::IsReady() const noexcept
{
    const auto valid = [](const Image& image) {
        return image.width == kExpectedSize && image.height == kExpectedSize &&
            image.pixels.size() ==
                static_cast<std::size_t>(kExpectedSize) * kExpectedSize;
    };
    return valid(transBackground_) && valid(buttonsBackground_) &&
        valid(controllerOverlay_) && valid(controllerButton_) &&
        valid(frameBorder_) && valid(text_) &&
        std::all_of(arrows_.begin(), arrows_.end(), valid) &&
        std::all_of(bottomFrames_.begin(), bottomFrames_.end(), valid) &&
        std::all_of(selectedTabs_.begin(), selectedTabs_.end(), valid) &&
        std::all_of(hovers_.begin(), hovers_.end(), valid) &&
        checkBox_.width == 64 && checkBox_.height == 64 &&
        sliderBar_.width == 256 && sliderBar_.height == 64 &&
        numberBox_.width == 128 && numberBox_.height == 64 &&
        whiteButton_.width == 64 && whiteButton_.height == 64;
}

bool SettingsMenuArt::LoadPng(
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

bool SettingsMenuArt::CompositeLayer(
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

bool SettingsMenuArt::CompositeLayerAt(
    const Image& source,
    Image& destination,
    int left,
    int top,
    UINT width,
    UINT height)
{
    if (source.width == 0 || source.height == 0 || width == 0 || height == 0 ||
        source.pixels.size() !=
            static_cast<std::size_t>(source.width) * source.height ||
        destination.pixels.size() !=
            static_cast<std::size_t>(destination.width) * destination.height)
    {
        return false;
    }
    for (UINT y = 0; y < height; ++y)
    {
        const int destinationY = top + static_cast<int>(y);
        if (destinationY < 0 || destinationY >=
                static_cast<int>(destination.height))
        {
            continue;
        }
        const UINT sourceY = (y * source.height) / height;
        for (UINT x = 0; x < width; ++x)
        {
            const int destinationX = left + static_cast<int>(x);
            if (destinationX < 0 || destinationX >=
                    static_cast<int>(destination.width))
            {
                continue;
            }
            const UINT sourceX = (x * source.width) / width;
            const std::size_t sourceIndex =
                static_cast<std::size_t>(sourceY) * source.width + sourceX;
            const std::size_t destinationIndex =
                static_cast<std::size_t>(destinationY) * destination.width +
                static_cast<UINT>(destinationX);
            destination.pixels[destinationIndex] = BlendPremultiplied(
                source.pixels[sourceIndex],
                destination.pixels[destinationIndex]);
        }
    }
    return true;
}

bool SettingsMenuArt::DrawWhiteText(
    Image& destination,
    const wchar_t* text,
    int left,
    int top,
    int width,
    int height,
    int pixelHeight,
    UINT format)
{
    if (text == nullptr || text[0] == L'\0' || width <= 0 || height <= 0 ||
        pixelHeight <= 0)
    {
        return false;
    }
    BITMAPINFO information = {};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = width;
    information.bmiHeader.biHeight = -height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;
    void* bitmapPixels = nullptr;
    HDC deviceContext = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = deviceContext == nullptr
        ? nullptr
        : CreateDIBSection(
              deviceContext,
              &information,
              DIB_RGB_COLORS,
              &bitmapPixels,
              nullptr,
              0);
    HFONT font = CreateFontW(
        -pixelHeight,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI Semibold");
    if (deviceContext == nullptr || bitmap == nullptr || bitmapPixels == nullptr ||
        font == nullptr)
    {
        if (font != nullptr) DeleteObject(font);
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (deviceContext != nullptr) DeleteDC(deviceContext);
        return false;
    }
    const HGDIOBJ previousBitmap = SelectObject(deviceContext, bitmap);
    const HGDIOBJ previousFont = SelectObject(deviceContext, font);
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, RGB(255, 255, 255));
    RECT textRectangle = {0, 0, width, height};
    const int drawn = DrawTextW(
        deviceContext,
        text,
        -1,
        &textRectangle,
        format | DT_NOPREFIX);
    Image textImage = {};
    textImage.width = static_cast<UINT>(width);
    textImage.height = static_cast<UINT>(height);
    textImage.pixels.resize(
        static_cast<std::size_t>(textImage.width) * textImage.height);
    const auto* sourcePixels = static_cast<const std::uint32_t*>(bitmapPixels);
    for (std::size_t index = 0; index < textImage.pixels.size(); ++index)
    {
        const std::uint32_t source = sourcePixels[index];
        const std::uint32_t coverage = (std::max)({
            source & 0xFFU,
            (source >> 8U) & 0xFFU,
            (source >> 16U) & 0xFFU});
        textImage.pixels[index] = PackPixel(
            coverage,
            coverage,
            coverage,
            coverage);
    }
    SelectObject(deviceContext, previousFont);
    SelectObject(deviceContext, previousBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(deviceContext);
    return drawn != 0 && CompositeLayerAt(
        textImage,
        destination,
        left,
        top,
        textImage.width,
        textImage.height);
}

bool SettingsMenuArt::DrawStatusField(
    Image& destination,
    stereo::SettingsMenuStatus status)
{
    constexpr int left = 64;
    constexpr int top = 858;
    constexpr int width = 470;
    constexpr int height = 60;
    constexpr int border = 3;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const int destinationX = left + x;
            const int destinationY = top + y;
            const bool isBorder = x < border || x >= width - border ||
                y < border || y >= height - border;
            const std::uint32_t source = isBorder
                ? PackPixel(150U, 150U, 150U, 220U)
                : PackPixel(0U, 0U, 0U, 185U);
            const std::size_t index =
                static_cast<std::size_t>(destinationY) * destination.width +
                static_cast<UINT>(destinationX);
            destination.pixels[index] = BlendPremultiplied(
                source,
                destination.pixels[index]);
        }
    }
    const wchar_t* message = L"Settings loaded";
    switch (status)
    {
    case stereo::SettingsMenuStatus::SettingsNotSaved:
        message = L"Settings not saved";
        break;
    case stereo::SettingsMenuStatus::SettingsSaved:
        message = L"Settings saved";
        break;
    case stereo::SettingsMenuStatus::SettingsSavedRestartRequired:
        message = L"Settings saved - restart required";
        break;
    case stereo::SettingsMenuStatus::DefaultsRestored:
        message = L"Defaults restored - not saved";
        break;
    case stereo::SettingsMenuStatus::DefaultsLoaded:
        message = L"Defaults loaded";
        break;
    case stereo::SettingsMenuStatus::InvalidConfigDefaultsLoaded:
        message = L"Invalid config - defaults loaded";
        break;
    case stereo::SettingsMenuStatus::ConfigReadFailed:
        message = L"Config read failed - defaults loaded";
        break;
    case stereo::SettingsMenuStatus::SaveFailed:
        message = L"Save failed - changes not saved";
        break;
    case stereo::SettingsMenuStatus::StandingHeightCalibrated:
        message = L"Calibration staged - press Save";
        break;
    case stereo::SettingsMenuStatus::StandingHeightUnavailable:
        message = L"Standing floor height unavailable";
        break;
    case stereo::SettingsMenuStatus::StandingModeRequired:
        message = L"Select Standing Play Mode first";
        break;
    case stereo::SettingsMenuStatus::ForwardRecentered:
        message = L"Forward direction recentered";
        break;
    case stereo::SettingsMenuStatus::ForwardRecenterFailed:
        message = L"Recenter unavailable - tracking not ready";
        break;
    case stereo::SettingsMenuStatus::SettingsLoaded:
    default:
        break;
    }
    return DrawWhiteText(
        destination,
        message,
        left + 10,
        top,
        width - 20,
        height,
        24,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

bool SettingsMenuArt::ComposeSettingsBody(
    const stereo::SettingsMenuSnapshot& state,
    Image& destination) const
{
    constexpr int controlLeft =
        static_cast<int>(stereo::kSettingsMenuControlColumnPixels);
    if (state.tab == stereo::SettingsMenuTab::VrSettings)
    {
        const auto drawSelectorAt = [&](int centerY,
                                        const wchar_t* label,
                                        const wchar_t* value,
                                        stereo::SettingsMenuSelection previous,
                                        stereo::SettingsMenuSelection next) {
            const int leftCenter = static_cast<int>(
                stereo::kSettingsMenuSelectorLeftArrowCenterPixels);
            const int rightCenter = static_cast<int>(
                stereo::kSettingsMenuSelectorRightArrowCenterPixels);
            return DrawWhiteText(destination, label, 82, centerY - 32, 445, 64,
                       27, DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
                DrawWhiteText(destination, L"<", leftCenter - 38, centerY - 38,
                    76, 76, state.hovered == previous ? 38 : 30,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER) &&
                DrawWhiteText(destination, value, leftCenter + 38, centerY - 32,
                    rightCenter - leftCenter - 76, 64, 23,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER) &&
                DrawWhiteText(destination, L">", rightCenter - 38,
                    centerY - 38, 76, 76,
                    state.hovered == next ? 38 : 30,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };
        const auto drawSliderAt = [&](int centerY,
                                      const wchar_t* label,
                                      float normalized,
                                      const std::wstring& display) {
            const int markerCenter = controlLeft + 12 +
                static_cast<int>(std::lround(
                    std::clamp(normalized, 0.0F, 1.0F) * 232.0F));
            return DrawWhiteText(destination, label, 82, centerY - 32, 445, 64,
                       27, DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
                CompositeLayerAt(sliderBar_, destination, controlLeft,
                    centerY - 32, 256, 64) &&
                CompositeLayerAt(numberBox_, destination,
                    static_cast<int>(stereo::kSettingsMenuNumberBoxLeftPixels),
                    centerY - 32, 128, 64) &&
                CompositeLayerAt(whiteButton_, destination, markerCenter - 24,
                    centerY - 24, 48, 48) &&
                DrawWhiteText(destination, display.c_str(),
                    static_cast<int>(stereo::kSettingsMenuNumberBoxLeftPixels),
                    centerY - 32, 128, 64, 25,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };
        if (state.page == 0)
        {
            const wchar_t* playMode =
                state.values.playMode == settings::PlayMode::Standing
                ? L"STANDING" : L"SEATED";
            const wchar_t* turnMode =
                state.values.artificialTurnMode ==
                    settings::ArtificialTurnMode::Snap
                ? L"SNAP"
                : L"SMOOTH";
            const wchar_t* movement = L"CHARACTER";
            if (state.values.movementDirection == settings::MovementDirection::Head)
                movement = L"HEAD";
            else if (state.values.movementDirection ==
                     settings::MovementDirection::OffHandController)
                movement = L"OFF-HAND CONTROLLER";
            bool drawn = drawSelectorAt(
                    static_cast<int>(stereo::kSettingsMenuVrPageOneRowCentersPixels[0]),
                    L"Play Mode", playMode,
                    stereo::SettingsMenuSelection::PlayModePrevious,
                    stereo::SettingsMenuSelection::PlayModeNext) &&
                drawSelectorAt(
                    static_cast<int>(stereo::kSettingsMenuVrPageOneRowCentersPixels[1]),
                    L"Artificial Turning", turnMode,
                    stereo::SettingsMenuSelection::ArtificialTurnPrevious,
                    stereo::SettingsMenuSelection::ArtificialTurnNext);
            const int conditionalY = static_cast<int>(
                stereo::kSettingsMenuVrPageOneRowCentersPixels[2]);
            if (drawn && state.values.artificialTurnMode ==
                settings::ArtificialTurnMode::Snap)
            {
                const std::wstring angle =
                    std::to_wstring(state.values.snapTurnAngleDegrees) + L" degrees";
                drawn = drawSelectorAt(conditionalY, L"Snap Angle", angle.c_str(),
                    stereo::SettingsMenuSelection::SnapAnglePrevious,
                    stereo::SettingsMenuSelection::SnapAngleNext);
            }
            else if (drawn && state.values.artificialTurnMode ==
                     settings::ArtificialTurnMode::Smooth)
            {
                const float normalized = static_cast<float>(
                    state.values.infantryTurnSpeedPercent -
                    settings::kMinimumInfantryTurnSpeedPercent) /
                    static_cast<float>(settings::kMaximumInfantryTurnSpeedPercent -
                        settings::kMinimumInfantryTurnSpeedPercent);
                drawn = drawSliderAt(conditionalY, L"Turn Speed", normalized,
                    std::to_wstring(state.values.infantryTurnSpeedPercent) + L"%");
            }
            return drawn && drawSelectorAt(
                static_cast<int>(stereo::kSettingsMenuVrPageOneRowCentersPixels[3]),
                L"Movement Direction", movement,
                stereo::SettingsMenuSelection::MovementDirectionPrevious,
                stereo::SettingsMenuSelection::MovementDirectionNext);
        }
        const int heightY = static_cast<int>(
            stereo::kSettingsMenuVrPageTwoRowCentersPixels[0]);
        const float normalizedHeight = static_cast<float>(
            state.values.vrHeightAdjustmentCentimeters -
            settings::kMinimumVrHeightAdjustmentCentimeters) /
            static_cast<float>(settings::kMaximumVrHeightAdjustmentCentimeters -
                settings::kMinimumVrHeightAdjustmentCentimeters);
        const std::wstring height =
            (state.values.vrHeightAdjustmentCentimeters > 0 ? L"+" : L"") +
            std::to_wstring(state.values.vrHeightAdjustmentCentimeters) + L" cm";
        const auto drawAction = [&](int centerY,
                                    const wchar_t* label,
                                    stereo::SettingsMenuSelection selection) {
            // NumberBox.png intentionally carries transparent padding on its
            // right edge. Centre its visible 91/128-wide frame on the panel,
            // then centre text inside that visible frame rather than inside
            // the padded source rectangle.
            return CompositeLayerAt(numberBox_, destination, 306, centerY - 42,
                       580, 84) &&
                DrawWhiteText(destination, label, 316, centerY - 42, 392, 84,
                    state.hovered == selection ? 27 : 24,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };
        return drawSliderAt(heightY, L"Manual Height Adjustment",
                   normalizedHeight, height) &&
            DrawWhiteText(destination,
                L"Infantry only; vehicles and mounted seats use independent neutral poses.",
                82, heightY + 42, 860, 46, 18,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
            drawAction(static_cast<int>(
                    stereo::kSettingsMenuVrPageTwoRowCentersPixels[1]),
                L"CALIBRATE STANDING",
                stereo::SettingsMenuSelection::AutoCalibrateStandingHeight) &&
            drawAction(static_cast<int>(
                    stereo::kSettingsMenuVrPageTwoRowCentersPixels[2]),
                L"RECENTER FORWARD",
                stereo::SettingsMenuSelection::RecenterForward);
    }
    if (state.tab == stereo::SettingsMenuTab::GraphicsAudio)
    {
        const auto drawToggle = [&](std::size_t row,
                                    const wchar_t* label,
                                    bool checked,
                                    bool requiresRestart) {
            const int centerY = static_cast<int>(
                stereo::kSettingsMenuGraphicsRowCentersPixels[row]);
            bool drawn = DrawWhiteText(
                    destination,
                    label,
                    82,
                    centerY - 31,
                    requiresRestart ? 255 : 440,
                    62,
                    26,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
                CompositeLayerAt(
                    checkBox_, destination, controlLeft, centerY - 32, 64, 64);
            if (drawn && requiresRestart)
            {
                drawn = DrawWhiteText(
                    destination,
                    L"(Requires Restart)",
                    330,
                    centerY - 29,
                    215,
                    58,
                    19,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
            return drawn && (!checked || CompositeLayerAt(
                whiteButton_,
                destination,
                controlLeft + 8,
                centerY - 24,
                48,
                48));
        };
        const auto drawSlider = [&](std::size_t row,
                                    const wchar_t* label,
                                    std::uint32_t value,
                                    std::uint32_t minimum,
                                    std::uint32_t maximum,
                                    const std::wstring& display) {
            const int centerY = static_cast<int>(
                stereo::kSettingsMenuGraphicsRowCentersPixels[row]);
            const float normalized = static_cast<float>(value - minimum) /
                static_cast<float>(maximum - minimum);
            const int markerCenter = controlLeft + 12 +
                static_cast<int>(std::lround(
                    std::clamp(normalized, 0.0F, 1.0F) * 232.0F));
            return DrawWhiteText(
                       destination,
                       label,
                       82,
                       centerY - 31,
                       450,
                       62,
                       25,
                       DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
                CompositeLayerAt(
                    sliderBar_, destination, controlLeft, centerY - 32, 256, 64) &&
                CompositeLayerAt(
                    numberBox_,
                    destination,
                    static_cast<int>(stereo::kSettingsMenuNumberBoxLeftPixels),
                    centerY - 32,
                    128,
                    64) &&
                CompositeLayerAt(
                    whiteButton_,
                    destination,
                    markerCenter - 24,
                    centerY - 24,
                    48,
                    48) &&
                DrawWhiteText(
                    destination,
                    display.c_str(),
                    static_cast<int>(stereo::kSettingsMenuNumberBoxLeftPixels),
                    centerY - 32,
                    128,
                    64,
                    21,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };
        wchar_t aoRadius[32] = {};
        _snwprintf_s(
            aoRadius,
            std::size(aoRadius),
            _TRUNCATE,
            L"%u.%02um",
            state.values.ambientOcclusionRadiusCentimeters / 100U,
            state.values.ambientOcclusionRadiusCentimeters % 100U);
        wchar_t bloomThreshold[16] = {};
        _snwprintf_s(
            bloomThreshold,
            std::size(bloomThreshold),
            _TRUNCATE,
            L"0.%02u",
            state.values.bloomThresholdPercent);
        wchar_t bloomIntensity[16] = {};
        _snwprintf_s(
            bloomIntensity,
            std::size(bloomIntensity),
            _TRUNCATE,
            L"%u.%02u",
            state.values.bloomIntensityPercent / 100U,
            state.values.bloomIntensityPercent % 100U);
        return drawToggle(0, L"FXAA", state.values.fxaaEnabled, false) &&
            drawSlider(
                1,
                L"FXAA Sharpening",
                state.values.fxaaSharpeningPercent,
                settings::kMinimumFxaaSharpeningPercent,
                settings::kMaximumFxaaSharpeningPercent,
                std::to_wstring(state.values.fxaaSharpeningPercent) + L"%") &&
            drawToggle(
                2,
                L"Ambient Occlusion",
                state.values.ambientOcclusionEnabled,
                true) &&
            drawSlider(
                3,
                L"AO Radius",
                state.values.ambientOcclusionRadiusCentimeters,
                settings::kMinimumAmbientOcclusionRadiusCentimeters,
                settings::kMaximumAmbientOcclusionRadiusCentimeters,
                aoRadius) &&
            drawSlider(
                4,
                L"AO Strength",
                state.values.ambientOcclusionStrengthPercent,
                settings::kMinimumAmbientOcclusionStrengthPercent,
                settings::kMaximumAmbientOcclusionStrengthPercent,
                std::to_wstring(
                    state.values.ambientOcclusionStrengthPercent) + L"%") &&
            drawToggle(
                5,
                L"Bloom",
                state.values.bloomEnabled,
                true) &&
            drawSlider(
                6,
                L"Bloom Threshold",
                state.values.bloomThresholdPercent,
                settings::kMinimumBloomThresholdPercent,
                settings::kMaximumBloomThresholdPercent,
                bloomThreshold) &&
            drawSlider(
                7,
                L"Bloom Intensity",
                state.values.bloomIntensityPercent,
                settings::kMinimumBloomIntensityPercent,
                settings::kMaximumBloomIntensityPercent,
                bloomIntensity);
    }
    if (state.tab != stereo::SettingsMenuTab::Controls)
    {
        return true;
    }
    const auto gripStyleName = [](settings::OffHandGripStyle style) {
        return style == settings::OffHandGripStyle::Toggle
            ? L"TOGGLE"
            : L"HOLD";
    };
    const auto crosshairModeName = [](settings::WorldCrosshairMode mode) {
        switch (mode)
        {
        case settings::WorldCrosshairMode::Off: return L"OFF";
        case settings::WorldCrosshairMode::HitMarkerOnly:
            return L"HIT MARKER ONLY";
        case settings::WorldCrosshairMode::On:
        default: return L"ON";
        }
    };
    const auto drawSelector = [&](std::size_t row,
                                  const wchar_t* label,
                                  const wchar_t* value,
                                  stereo::SettingsMenuSelection previous,
                                  stereo::SettingsMenuSelection next) {
        const int centerY = static_cast<int>(
            stereo::kSettingsMenuControlsSelectorRowCentersPixels[row]);
        const int leftCenter = static_cast<int>(
            stereo::kSettingsMenuSelectorLeftArrowCenterPixels);
        const int rightCenter = static_cast<int>(
            stereo::kSettingsMenuSelectorRightArrowCenterPixels);
        return DrawWhiteText(
                   destination,
                   label,
                   82,
                   centerY - 32,
                   445,
                   64,
                   26,
                   DT_LEFT | DT_SINGLELINE | DT_VCENTER) &&
            DrawWhiteText(
                destination,
                L"<",
                leftCenter - 38,
                centerY - 38,
                76,
                76,
                state.hovered == previous ? 38 : 30,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER) &&
            DrawWhiteText(
                destination,
                value,
                leftCenter + 38,
                centerY - 32,
                rightCenter - leftCenter - 76,
                64,
                23,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER) &&
            DrawWhiteText(
                destination,
                L">",
                rightCenter - 38,
                centerY - 38,
                76,
                76,
                state.hovered == next ? 38 : 30,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };
    if (!drawSelector(
            0,
            L"Off-hand Grip Style",
            gripStyleName(state.values.offHandGripStyle),
            stereo::SettingsMenuSelection::OffHandGripPrevious,
            stereo::SettingsMenuSelection::OffHandGripNext) ||
        !DrawWhiteText(
            destination,
            L"3D HUD Crosshairs:",
            82,
            245,
            460,
            52,
            27,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER) ||
        !drawSelector(
            1,
            L"Hand Weapons",
            crosshairModeName(state.values.handWeaponCrosshair),
            stereo::SettingsMenuSelection::HandCrosshairPrevious,
            stereo::SettingsMenuSelection::HandCrosshairNext) ||
        !drawSelector(
            2,
            L"Mounted Guns",
            crosshairModeName(state.values.mountedWeaponCrosshair),
            stereo::SettingsMenuSelection::MountedCrosshairPrevious,
            stereo::SettingsMenuSelection::MountedCrosshairNext) ||
        !DrawWhiteText(
            destination,
            L"Controller Feedback & Vehicle Inversion:",
            82,
            495,
            460,
            52,
            27,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER))
    {
        return false;
    }
    constexpr std::array<const wchar_t*, 4> labels = {
        L"Flight Pitch (Invert Vertical Stick)",
        L"Turret Pitch (Up/Down)",
        L"Turret Yaw (Left/Right)",
        L"Controller Haptics"};
    const std::array<bool, 4> checked = {
        state.values.invertFlightPitch,
        state.values.invertTurretPitch,
        state.values.invertTurretYaw,
        state.values.controllerHapticsEnabled};
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        const int centerY = static_cast<int>(
            stereo::kSettingsMenuControlsToggleRowCentersPixels[index]);
        if (!DrawWhiteText(
                destination,
                labels[index],
                82,
                centerY - 36,
                450,
                72,
                27,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER) ||
            !CompositeLayerAt(
                checkBox_, destination, controlLeft, centerY - 32, 64, 64))
        {
            return false;
        }
        if (checked[index] &&
            !CompositeLayerAt(
                whiteButton_,
                destination,
                controlLeft + 8,
                centerY - 24,
                48,
                48))
        {
            return false;
        }
    }
    return true;
}

void SettingsMenuArt::WriteLog(const wchar_t* format, ...) const
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
