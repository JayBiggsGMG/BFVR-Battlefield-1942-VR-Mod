#pragma once

namespace bfvr::stereo
{

// Knives and non-firearm gadgets have no firearm barrel basis suitable for
// driving BF1942's native look authority. They use the raw OpenXR aim pointer
// for authoritative direction and for the matching 3D crosshair. Slot 11 is
// the TNT detonator and remains excluded from pointer-reticle policy.
[[nodiscard]] constexpr bool IsInfantryControllerPointerItemIndex(
    const int itemIndex) noexcept
{
    return itemIndex == 1 || itemIndex == 4 || itemIndex == 5 ||
        itemIndex == 6;
}

} // namespace bfvr::stereo
