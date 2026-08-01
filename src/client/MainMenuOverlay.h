#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bfvr
{

class MainMenuOverlay
{
public:
    using LogCallback = void (*)(const wchar_t* message);

    bool Initialize(LogCallback appendLog);
    bool InitializeFromDirectory(
        const std::wstring& assetsDirectory,
        LogCallback appendLog);
    void Reset() noexcept;

    void Composite(
        std::vector<DWORD>& destination,
        UINT destinationWidth,
        UINT destinationHeight,
        bool visible,
        bool hovered) const noexcept;

    bool CopyButtonPixels(
        bool hovered,
        std::vector<std::uint32_t>& pixels,
        UINT& width,
        UINT& height) const;

    [[nodiscard]] bool IsReady() const noexcept;

private:
    struct Image
    {
        UINT width = 0;
        UINT height = 0;
        // Premultiplied BGRA, matching Windows DWORD channel order.
        std::vector<std::uint32_t> pixels;
    };

    bool LoadButtonImages(const std::wstring& assetsDirectory);
    static bool LoadPng(
        void* imagingFactory,
        const std::wstring& path,
        Image& image);
    static bool CompositeLayer(const Image& source, Image& destination);
    static std::wstring FindAssetsDirectory();

    void WriteLog(const wchar_t* format, ...) const;

    Image normal_;
    Image hovered_;
    LogCallback appendLog_ = nullptr;
    std::wstring assetsDirectory_;
};

} // namespace bfvr
