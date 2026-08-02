#pragma once

#include "stereo/QuickMenuInteraction.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bfvr
{

// Loads and precomposes the owner-authored menu stack as premultiplied BGRA.
// Cursor placement remains separate because its top-left hotspot follows the
// live controller-ray hit point.
class QuickMenuArt
{
public:
    using LogCallback = void (*)(void* context, const wchar_t* message);

    bool InitializeFromDirectory(
        const std::wstring& assetsDirectory,
        LogCallback logCallback,
        void* logContext);
    void Reset() noexcept;

    bool CopyMenuPixels(
        stereo::QuickMenuSelection selection,
        std::vector<std::uint32_t>& pixels,
        UINT& width,
        UINT& height) const;
    bool CopyCursorPixels(
        std::vector<std::uint32_t>& pixels,
        UINT& width,
        UINT& height) const;
    bool CopyUtilityStripPixels(
        stereo::QuickMenuSelection hovered,
        bool mountedCameraDecoupled,
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
    void WriteLog(const wchar_t* format, ...) const;

    std::array<Image, stereo::kQuickMenuSelectionCount> menus_ = {};
    std::array<Image, stereo::kQuickMenuUtilityVisualCount> utilities_ = {};
    Image cursor_ = {};
    LogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};

} // namespace bfvr
