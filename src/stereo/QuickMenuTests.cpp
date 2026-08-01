#include "client/QuickMenuArt.h"
#include "stereo/QuickMenuInteraction.h"
#include "stereo/QuickMenuMirrorMath.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
using bfvr::stereo::Pose;
using bfvr::stereo::QuickMenuSelection;
using bfvr::stereo::Vec3;

bool NearlyEqual(float lhs, float rhs, float tolerance = 0.0001F)
{
    return std::fabs(lhs - rhs) <= tolerance;
}

bool SamePosition(const Pose& lhs, const Pose& rhs)
{
    return NearlyEqual(lhs.position.x, rhs.position.x) &&
        NearlyEqual(lhs.position.y, rhs.position.y) &&
        NearlyEqual(lhs.position.z, rhs.position.z);
}

Vec3 Add(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Scale(const Vec3& value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Vec3 Rotate(const bfvr::stereo::Quaternion& orientation, const Vec3& value)
{
    const Vec3 axis{orientation.x, orientation.y, orientation.z};
    const Vec3 doubledCross = Scale(Cross(axis, value), 2.0F);
    return Add(
        value,
        Add(
            Scale(doubledCross, orientation.w),
            Cross(axis, doubledCross)));
}

bool TestBounds()
{
    struct Sample
    {
        float x;
        float y;
        QuickMenuSelection expected;
    };
    constexpr Sample samples[] = {
        {0.20F, 0.08F, QuickMenuSelection::MainMenu},
        {0.70F, 0.08F, QuickMenuSelection::Deploy},
        {0.20F, 0.30F, QuickMenuSelection::Weapon1},
        {0.60F, 0.30F, QuickMenuSelection::Weapon2},
        {0.20F, 0.55F, QuickMenuSelection::Weapon3},
        {0.60F, 0.55F, QuickMenuSelection::Weapon4},
        {0.20F, 0.88F, QuickMenuSelection::Weapon5},
        {0.60F, 0.88F, QuickMenuSelection::Weapon6},
        {0.92F, 0.28F, QuickMenuSelection::CameraF9},
        {0.92F, 0.46F, QuickMenuSelection::CameraF10},
        {0.92F, 0.68F, QuickMenuSelection::CameraF11},
        {0.92F, 0.92F, QuickMenuSelection::CameraF12}};
    for (const Sample& sample : samples)
    {
        if (bfvr::stereo::QuickMenuSelectionAt(sample.x, sample.y) !=
            sample.expected)
        {
            return false;
        }
    }
    return bfvr::stereo::QuickMenuSelectionAt(-0.01F, 0.5F) ==
            QuickMenuSelection::None &&
        bfvr::stereo::QuickMenuSelectionAt(0.5F, 1.01F) ==
            QuickMenuSelection::None &&
        bfvr::stereo::QuickMenuSelectionAt(1.0F, 1.0F) ==
            QuickMenuSelection::CameraF12;
}

bool TestPhysicalSize()
{
    constexpr float initialPanelSizeMeters = 0.38F;
    constexpr float requestedScale = 0.70F;
    return NearlyEqual(
               bfvr::stereo::kQuickMenuWidthMeters,
               initialPanelSizeMeters * requestedScale) &&
        NearlyEqual(
               bfvr::stereo::kQuickMenuHeightMeters,
               initialPanelSizeMeters * requestedScale);
}

bool TestMirrorProjection()
{
    bfvr::stereo::QuickMenuMirrorView eye = {};
    const float quarterTurn = std::atan(1.0F);
    eye.angleLeft = -quarterTurn;
    eye.angleRight = quarterTurn;
    eye.angleUp = quarterTurn;
    eye.angleDown = -quarterTurn;
    Pose panel = {};
    panel.position.z = -1.0F;
    std::array<bfvr::stereo::QuickMenuMirrorVertex, 4> vertices = {};
    if (!bfvr::stereo::ProjectQuickMenuQuadToMirror(
            panel,
            1.0F,
            1.0F,
            eye,
            {},
            vertices))
    {
        return false;
    }
    const auto normalizedX = [](const auto& vertex) {
        return vertex.clipX / vertex.clipW;
    };
    const auto normalizedY = [](const auto& vertex) {
        return vertex.clipY / vertex.clipW;
    };
    if (!NearlyEqual(normalizedX(vertices[0]), -0.5F) ||
        !NearlyEqual(normalizedY(vertices[0]), -0.5F) ||
        !NearlyEqual(normalizedX(vertices[1]), -0.5F) ||
        !NearlyEqual(normalizedY(vertices[1]), 0.5F) ||
        !NearlyEqual(normalizedX(vertices[2]), 0.5F) ||
        !NearlyEqual(normalizedY(vertices[2]), -0.5F) ||
        !NearlyEqual(normalizedX(vertices[3]), 0.5F) ||
        !NearlyEqual(normalizedY(vertices[3]), 0.5F))
    {
        return false;
    }

    const bfvr::stereo::QuickMenuMirrorCrop verticalCentreCrop = {
        1.0F,
        0.5F,
        0.0F,
        0.25F};
    if (!bfvr::stereo::ProjectQuickMenuQuadToMirror(
            panel,
            1.0F,
            1.0F,
            eye,
            verticalCentreCrop,
            vertices) ||
        !NearlyEqual(normalizedY(vertices[0]), -1.0F) ||
        !NearlyEqual(normalizedY(vertices[1]), 1.0F))
    {
        return false;
    }

    panel.position.z = 1.0F;
    return !bfvr::stereo::ProjectQuickMenuQuadToMirror(
        panel,
        1.0F,
        1.0F,
        eye,
        {},
        vertices);
}

bool TestInteraction()
{
    bfvr::stereo::QuickMenuInteraction interaction;
    bfvr::stereo::QuickMenuFrameInput input = {};
    input.predictedDisplayTime = 1'000'000'000;
    input.sessionFocused = true;
    input.shouldRender = true;
    input.headTracked = true;
    input.rightGripTracked = true;
    input.rightAimTracked = true;
    input.rightPrimaryHeld = true;
    input.headPose.position = {0.0F, 1.70F, 0.0F};
    input.rightGripPose.position = {0.25F, 1.20F, -0.35F};
    input.rightAimPose.position = input.headPose.position;
    interaction.Update(input);
    const auto opened = interaction.Snapshot();
    if (!opened.visible)
    {
        return false;
    }
    const Vec3 panelForward = Rotate(
        opened.panelPose.orientation,
        {0.0F, 0.0F, 1.0F});
    const Vec3 panelRight = Rotate(
        opened.panelPose.orientation,
        {1.0F, 0.0F, 0.0F});
    const Vec3 toHead = {
        input.headPose.position.x - opened.panelPose.position.x,
        input.headPose.position.y - opened.panelPose.position.y,
        input.headPose.position.z - opened.panelPose.position.z};
    const float toHeadLength = std::sqrt(
        toHead.x * toHead.x + toHead.y * toHead.y +
        toHead.z * toHead.z);
    const float facingDot = toHeadLength > 0.0F
        ? (panelForward.x * toHead.x + panelForward.y * toHead.y +
           panelForward.z * toHead.z) / toHeadLength
        : 0.0F;
    if (facingDot < 0.999F || std::fabs(panelRight.y) > 0.0001F)
    {
        return false;
    }

    input.predictedDisplayTime += 11'111'111;
    input.rightAimPose.orientation = opened.panelPose.orientation;
    interaction.Update(input);
    const auto aimed = interaction.Snapshot();
    if (!aimed.visible || !aimed.pointerVisible ||
        aimed.hovered != QuickMenuSelection::Weapon4 ||
        !NearlyEqual(aimed.pointerU, 0.5F, 0.002F) ||
        !NearlyEqual(aimed.pointerV, 0.5F, 0.002F))
    {
        return false;
    }

    input.predictedDisplayTime += 11'111'111;
    input.rightGripPose.position.x += 0.005F;
    interaction.Update(input);
    const auto jittered = interaction.Snapshot();
    if (!SamePosition(aimed.panelPose, jittered.panelPose))
    {
        return false;
    }

    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = false;
    interaction.Update(input);
    if (interaction.Snapshot().visible ||
        interaction.TakeReleasedSelection() !=
            QuickMenuSelection::Weapon4 ||
        interaction.TakeReleasedSelection() != QuickMenuSelection::None)
    {
        return false;
    }

    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.headTracked = false;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = false;
    input.headTracked = true;
    interaction.Update(input);
    if (interaction.Snapshot().visible ||
        interaction.TakeReleasedSelection() != QuickMenuSelection::None)
    {
        return false;
    }

    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.sessionFocused = false;
    input.rightPrimaryHeld = false;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.sessionFocused = true;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    if (interaction.Snapshot().visible)
    {
        return false;
    }
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = false;
    interaction.Update(input);
    input.predictedDisplayTime += 11'111'111;
    input.rightPrimaryHeld = true;
    interaction.Update(input);
    return interaction.Snapshot().visible;
}

bool TestCursorHotspot()
{
    Pose panel = {};
    constexpr float cursorSize = 0.04F;
    const Pose cursor = bfvr::stereo::MakeQuickMenuCursorPose(
        panel,
        0.0F,
        0.0F,
        cursorSize,
        cursorSize);
    const float cursorLeft = cursor.position.x - cursorSize * 0.5F;
    const float cursorTop = cursor.position.y + cursorSize * 0.5F;
    return NearlyEqual(
               cursorLeft,
               -bfvr::stereo::kQuickMenuWidthMeters * 0.5F) &&
        NearlyEqual(
               cursorTop,
               bfvr::stereo::kQuickMenuHeightMeters * 0.5F) &&
        cursor.position.z > 0.0F &&
        bfvr::stereo::kQuickMenuCursorHotspotX == 0.0F &&
        bfvr::stereo::kQuickMenuCursorHotspotY == 0.0F;
}

bool TestArt(const wchar_t* assetsDirectory)
{
    bfvr::QuickMenuArt art;
    if (!art.InitializeFromDirectory(assetsDirectory, nullptr, nullptr) ||
        !art.IsReady())
    {
        return false;
    }
    std::vector<std::uint32_t> base;
    UINT width = 0;
    UINT height = 0;
    if (!art.CopyMenuPixels(
            QuickMenuSelection::None,
            base,
            width,
            height) ||
        width != 512 || height != 512 || base.empty())
    {
        return false;
    }
    for (std::size_t index = 1;
         index < bfvr::stereo::kQuickMenuSelectionCount;
         ++index)
    {
        std::vector<std::uint32_t> variant;
        UINT variantWidth = 0;
        UINT variantHeight = 0;
        if (!art.CopyMenuPixels(
                static_cast<QuickMenuSelection>(index),
                variant,
                variantWidth,
                variantHeight) ||
            variantWidth != width || variantHeight != height ||
            variant == base)
        {
            return false;
        }
    }
    std::vector<std::uint32_t> cursor;
    UINT cursorWidth = 0;
    UINT cursorHeight = 0;
    return art.CopyCursorPixels(
               cursor,
               cursorWidth,
               cursorHeight) &&
        cursorWidth > 0 && cursorHeight > 0 && !cursor.empty() &&
        std::any_of(
            cursor.begin(),
            cursor.end(),
            [](std::uint32_t pixel) {
                return (pixel & 0xFF000000U) != 0;
            });
}
} // namespace

int wmain(int argumentCount, wchar_t** arguments)
{
    if (argumentCount != 2 || arguments[1] == nullptr)
    {
        std::cerr << "Expected one BFVR assets-directory argument.\n";
        return 1;
    }
    if (!TestBounds())
    {
        std::cerr << "Quick Menu connected bounds test failed.\n";
        return 1;
    }
    if (!TestPhysicalSize())
    {
        std::cerr << "Quick Menu owner-requested physical-size test failed.\n";
        return 1;
    }
    if (!TestMirrorProjection())
    {
        std::cerr << "Quick Menu right-eye mirror projection test failed.\n";
        return 1;
    }
    if (!TestInteraction())
    {
        std::cerr << "Quick Menu hold/filter/ray/release test failed.\n";
        return 1;
    }
    if (!TestCursorHotspot())
    {
        std::cerr << "Quick Menu top-left cursor hotspot test failed.\n";
        return 1;
    }
    if (!TestArt(arguments[1]))
    {
        std::cerr << "Quick Menu authored layer-stack test failed.\n";
        return 1;
    }
    std::cout << "BFVR Quick Menu tests passed.\n";
    return 0;
}
