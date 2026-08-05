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
    [[nodiscard]] bool IsReady() const noexcept;

private:
    struct Image
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<std::uint32_t> pixels;
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
    static bool DrawWhiteText(
        Image& destination,
        const wchar_t* text,
        int left,
        int top,
        int width,
        int height,
        int pixelHeight,
        UINT format);
    static bool DrawStatusField(
        Image& destination,
        stereo::SettingsMenuStatus status);
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
    LogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};

} // namespace bfvr
