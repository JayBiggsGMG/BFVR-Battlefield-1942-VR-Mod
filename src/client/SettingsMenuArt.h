#pragma once

#include "stereo/SettingsMenuInteraction.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bfvr
{

// Loads the owner-authored 1024-square Settings layers and composites only
// when visual state changes. The output is premultiplied BGRA for D3D11/XR.
class SettingsMenuArt
{
public:
    using LogCallback = void (*)(void* context, const wchar_t* message);
    static constexpr UINT kVersionBannerWidth = 1024;
    static constexpr UINT kVersionBannerHeight = 64;

    bool InitializeFromDirectory(
        const std::wstring& settingsDirectory,
        LogCallback logCallback,
        void* logContext);
    void Reset() noexcept;

    bool Compose(
        const stereo::SettingsMenuSnapshot& state,
        std::vector<std::uint32_t>& pixels,
        UINT& width,
        UINT& height) const;
    bool ComposeVersionBanner(
        std::vector<std::uint32_t>& pixels,
        UINT& width,
        UINT& height) const;
    [[nodiscard]] bool IsReady() const noexcept;

private:
    struct Image
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<std::uint32_t> pixels;
    };

    struct TextCacheEntry
    {
        std::wstring text;
        int width = 0;
        int height = 0;
        int pixelHeight = 0;
        UINT format = 0;
        std::uint32_t tintArgb = 0xFFFFFFFFU;
        Image image;
    };

    static bool LoadPng(
        void* imagingFactory,
        const std::wstring& path,
        Image& image);
    static bool CompositeLayer(const Image& source, Image& destination);
    static bool CompositeLayerAt(
        const Image& source,
        Image& destination,
        int left,
        int top,
        UINT width,
        UINT height);
    bool DrawWhiteText(
        Image& destination,
        const wchar_t* text,
        int left,
        int top,
        int width,
        int height,
        int pixelHeight,
        UINT format) const;
    bool DrawTintedText(
        Image& destination,
        const wchar_t* text,
        int left,
        int top,
        int width,
        int height,
        int pixelHeight,
        UINT format,
        std::uint32_t tintArgb) const;
    bool DrawStatusField(
        Image& destination,
        stereo::SettingsMenuStatus status) const;
    bool ComposeSettingsBody(
        const stereo::SettingsMenuSnapshot& state,
        Image& destination) const;
    void WriteLog(const wchar_t* format, ...) const;

    Image transBackground_ = {};
    Image buttonsBackground_ = {};
    Image controllerOverlay_ = {};
    Image controllerButton_ = {};
    Image frameBorder_ = {};
    std::array<Image, 2> arrows_ = {};
    std::array<Image, 3> bottomFrames_ = {};
    std::array<Image, 3> selectedTabs_ = {};
    std::array<Image, 6> hovers_ = {};
    Image checkBox_ = {};
    Image sliderBar_ = {};
    Image numberBox_ = {};
    Image whiteButton_ = {};
    Image text_ = {};
    mutable std::vector<TextCacheEntry> textCache_;
    LogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};

} // namespace bfvr
