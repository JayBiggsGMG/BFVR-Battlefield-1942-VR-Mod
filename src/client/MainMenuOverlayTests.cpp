#include "client/MainMenuOverlay.h"

#include "stereo/MainMenuOverlayLayout.h"
#include "stereo/MainMenuScroll.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr UINT kTestWidth = 800;
constexpr UINT kTestHeight = 600;
constexpr DWORD kCanvasColor = 0xFF19324BU;

bool IsInsideButton(UINT x, UINT y)
{
    return bfvr::stereo::IsInsideUiCanvasRect(
        bfvr::stereo::BackToGameButtonRect(),
        static_cast<float>(x) + 0.5F,
        static_cast<float>(y) + 0.5F);
}

std::size_t CountChangedPixels(const std::vector<DWORD>& pixels)
{
    return static_cast<std::size_t>(std::count_if(
        pixels.begin(),
        pixels.end(),
        [](DWORD pixel) { return pixel != kCanvasColor; }));
}

bool ChangedOnlyInsideButton(const std::vector<DWORD>& pixels)
{
    for (UINT y = 0; y < kTestHeight; ++y)
    {
        for (UINT x = 0; x < kTestWidth; ++x)
        {
            const DWORD pixel =
                pixels[static_cast<std::size_t>(y) * kTestWidth + x];
            if (!IsInsideButton(x, y) && pixel != kCanvasColor)
            {
                return false;
            }
            if ((pixel & 0xFF000000U) != 0xFF000000U)
            {
                return false;
            }
        }
    }
    return true;
}

bool TestMainMenuScrollRepeat()
{
    bfvr::stereo::MainMenuScrollRepeatState state = {};
    return
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, false, 1.0F, 10) == 0 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.64F, 20) == 0 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.80F, 100) == 1 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.80F, 419) == 0 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.80F, 420) == 1 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.0F, 430) == 0 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, -0.80F, 500) == -1 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, true, 0.80F, 510) == 1 &&
        bfvr::stereo::UpdateMainMenuScrollRepeat(
            state, false, 0.80F, 520) == 0 &&
        state.heldDirection == 0;
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount != 2 || arguments[1] == nullptr)
    {
        std::cerr << "Expected one BFVR assets-directory argument.\n";
        return 1;
    }

    if (!TestMainMenuScrollRepeat())
    {
        std::cerr << "The main-menu right-stick wheel repeat policy failed.\n";
        return 1;
    }

    bfvr::MainMenuOverlay overlay;
    if (!overlay.InitializeFromDirectory(arguments[1], nullptr) ||
        !overlay.IsReady())
    {
        std::cerr << "The supplied main-menu PNG layers did not load.\n";
        return 1;
    }

    std::vector<DWORD> hidden(
        static_cast<std::size_t>(kTestWidth) * kTestHeight,
        kCanvasColor);
    overlay.Composite(hidden, kTestWidth, kTestHeight, false, false);
    if (CountChangedPixels(hidden) != 0)
    {
        std::cerr << "The hidden overlay changed the canvas.\n";
        return 1;
    }

    std::vector<DWORD> normal(
        static_cast<std::size_t>(kTestWidth) * kTestHeight,
        kCanvasColor);
    overlay.Composite(normal, kTestWidth, kTestHeight, true, false);
    const std::size_t normalChanged = CountChangedPixels(normal);
    if (normalChanged == 0 || !ChangedOnlyInsideButton(normal))
    {
        std::cerr << "The normal overlay escaped its logical rectangle.\n";
        return 1;
    }

    std::vector<DWORD> hovered(
        static_cast<std::size_t>(kTestWidth) * kTestHeight,
        kCanvasColor);
    overlay.Composite(hovered, kTestWidth, kTestHeight, true, true);
    if (CountChangedPixels(hovered) == 0 ||
        !ChangedOnlyInsideButton(hovered) ||
        hovered == normal)
    {
        std::cerr << "The hover stack was empty, escaped, or matched normal.\n";
        return 1;
    }

    const bfvr::stereo::UiCanvasRect runtimeRect =
        bfvr::stereo::MapUiCanvasRectThroughAspectFit(
            bfvr::stereo::BackToGameButtonRect(),
            1920.0F,
            1080.0F,
            1872.0F,
            2016.0F);
    const float fittedTop = (2016.0F - 1053.0F) * 0.5F;
    const float fittedBottom = fittedTop + 1053.0F;
    if (std::fabs((runtimeRect.left + runtimeRect.right) * 0.5F -
                  936.0F) > 0.01F ||
        runtimeRect.top <= fittedTop ||
        runtimeRect.bottom >= fittedBottom ||
        std::fabs(
            fittedBottom - runtimeRect.bottom -
            1053.0F * 24.0F / 600.0F) > 0.01F)
    {
        std::cerr << "The runtime button did not preserve its logical bottom margin inside the aspect-fit menu panel.\n";
        return 1;
    }

    std::cout << "BFVR main-menu overlay tests passed.\n";
    return 0;
}
