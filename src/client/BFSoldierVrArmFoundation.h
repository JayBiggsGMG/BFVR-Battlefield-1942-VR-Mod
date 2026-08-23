#pragma once

#include "stereo/ArmVrPoseMath.h"

#include <array>
#include <cstdint>

namespace bfvr
{

class BFSoldierBoneResolver;

// Preserves the established 0.15-m whole-viewmodel placement adjustment. It
// is deliberately separate from the new per-upper-arm shoulder ownership.
[[nodiscard]] bool TryMakeForwardShiftedBFSoldierVrArmRoot(
    void* skeleton,
    const stereo::Matrix4* rootTransform,
    void* soldier,
    bool localPlayerAlive,
    float forwardOffset,
    stereo::Matrix4& adjustedRoot) noexcept;

struct BFSoldierVrArmFoundationInput
{
    void* skeleton = nullptr;
    std::int32_t rightHandBone = -1;
    std::int32_t leftHandBone = -1;
    std::array<float, 3> rightHandTarget = {};
    std::array<float, 3> leftHandTarget = {};
    stereo::ArmVrShoulderAnchors shoulderAnchors = {};
    std::int32_t controllerGeneration = 0;
    bool rightActive = false;
    bool leftActive = false;
};

struct BFSoldierVrArmFoundationRestore
{
    std::array<void*, 2> upperArmRecords = {};
    std::array<std::array<float, 3>, 2> localTranslations = {};
    bool rightApplied = false;
    bool leftApplied = false;
};

// Applies a temporary translation only to each local upper-arm origin after
// BF1942 has produced the current native animation pose. A second native
// Skeleton evaluation then solves shoulder -> controller wrist. Restoring the
// local translations leaves the game's animation state untouched while its
// final hand/finger/item matrices retain the visible VR solve.
class BFSoldierVrArmFoundation final
{
public:
    [[nodiscard]] bool PrepareAfterNativeTransform(
        const BFSoldierVrArmFoundationInput& input,
        const BFSoldierBoneResolver& resolver,
        BFSoldierVrArmFoundationRestore& restore) noexcept;
    void Restore(BFSoldierVrArmFoundationRestore& restore) noexcept;
    void Reset() noexcept;

private:
    bool ResolveChains(
        void* skeleton,
        const BFSoldierBoneResolver& resolver,
        std::int32_t rightHandBone,
        std::int32_t leftHandBone) noexcept;

    void* cachedSkeleton_ = nullptr;
    std::int32_t cachedRightHand_ = -1;
    std::int32_t cachedRightUpperArm_ = -1;
    std::int32_t cachedLeftHand_ = -1;
    std::int32_t cachedLeftUpperArm_ = -1;
};

} // namespace bfvr
