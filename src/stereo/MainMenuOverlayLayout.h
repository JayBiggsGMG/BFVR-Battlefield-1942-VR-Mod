#pragma once

namespace bfvr::stereo
{

constexpr float kMainMenuCanvasWidth = 800.0F;
constexpr float kMainMenuCanvasHeight = 600.0F;
constexpr float kBackToGameButtonAuthoredWidth = 256.0F;
constexpr float kBackToGameButtonAuthoredHeight = 64.0F;
// Owner reference-image tuning: the supplied border occupies 246x50 pixels
// inside its transparent 256x64 raster. These independent authored scales
// compensate for Ref2's 800x600-to-16:9 mapping and make that visible border
// approximately match the native top-row menu buttons while retaining the
// established bottom margin.
constexpr float kBackToGameButtonWidthScale = 0.43F;
constexpr float kBackToGameButtonHeightScale = 0.53F;
constexpr float kBackToGameButtonWidth =
    kBackToGameButtonAuthoredWidth * kBackToGameButtonWidthScale;
constexpr float kBackToGameButtonHeight =
    kBackToGameButtonAuthoredHeight * kBackToGameButtonHeightScale;
constexpr float kBackToGameButtonBottomMargin = 24.0F;

struct UiCanvasRect
{
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

// Maps an 800x600 Ref2 logical rectangle through the same source-aspect fit
// used when BFVR copies the game UI into a runtime-sized OpenXR texture.
[[nodiscard]] constexpr UiCanvasRect MapUiCanvasRectThroughAspectFit(
    const UiCanvasRect& logical,
    float sourceWidth,
    float sourceHeight,
    float destinationWidth,
    float destinationHeight) noexcept
{
    if (sourceWidth <= 0.0F || sourceHeight <= 0.0F ||
        destinationWidth <= 0.0F || destinationHeight <= 0.0F)
    {
        return {};
    }
    const float horizontalScale = destinationWidth / sourceWidth;
    const float verticalScale = destinationHeight / sourceHeight;
    const float fitScale = horizontalScale < verticalScale
        ? horizontalScale
        : verticalScale;
    const float fittedWidth = sourceWidth * fitScale;
    const float fittedHeight = sourceHeight * fitScale;
    const float fittedLeft = (destinationWidth - fittedWidth) * 0.5F;
    const float fittedTop = (destinationHeight - fittedHeight) * 0.5F;
    const float logicalScaleX = fittedWidth / kMainMenuCanvasWidth;
    const float logicalScaleY = fittedHeight / kMainMenuCanvasHeight;
    return {
        fittedLeft + logical.left * logicalScaleX,
        fittedTop + logical.top * logicalScaleY,
        fittedLeft + logical.right * logicalScaleX,
        fittedTop + logical.bottom * logicalScaleY};
}

// The supplied raster is authored for BF1942's observed 800x600 Ref2 canvas.
// Keep it horizontally centred and slightly above the lower edge so it reads
// as part of the menu rather than touching the captured panel boundary.
[[nodiscard]] constexpr UiCanvasRect BackToGameButtonRect() noexcept
{
    const float left =
        (kMainMenuCanvasWidth - kBackToGameButtonWidth) * 0.5F;
    const float top =
        kMainMenuCanvasHeight -
        kBackToGameButtonHeight -
        kBackToGameButtonBottomMargin;
    return {
        left,
        top,
        left + kBackToGameButtonWidth,
        top + kBackToGameButtonHeight};
}

[[nodiscard]] constexpr bool IsInsideUiCanvasRect(
    const UiCanvasRect& rect,
    float x,
    float y) noexcept
{
    return x >= rect.left && x < rect.right &&
        y >= rect.top && y < rect.bottom;
}

// Returns true once on a new press that begins over the visible button. A
// press that begins elsewhere stays consumed until release, preventing a held
// mouse button or trigger from activating merely by moving into the rectangle.
[[nodiscard]] inline bool ConsumeUiButtonPressEdge(
    bool visible,
    bool hovered,
    bool pressed,
    bool& previousPressed) noexcept
{
    const bool activated =
        visible && hovered && pressed && !previousPressed;
    previousPressed = pressed;
    return activated;
}

} // namespace bfvr::stereo
