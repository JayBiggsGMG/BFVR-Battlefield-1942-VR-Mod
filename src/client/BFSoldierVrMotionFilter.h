#pragma once

#include <windows.h>

#include <memory>

namespace bfvr
{

using BFSoldierVrMotionFilterLogCallback = void (*)(const wchar_t* message);

// The direct engine-returned angles that would otherwise be applied to the
// legacy camera. BFVR consumes this transient value only for the controller-
// held weapon, never for the HMD/eye cameras.
struct BFSoldierVrLegacyRecoil
{
    float pitch = 0.0F;
    float yaw = 0.0F;
    const void* soldier = nullptr;
    LONG sequence = 0;
};

[[nodiscard]] bool ReadFreshBFSoldierVrLegacyRecoil(
    BFSoldierVrLegacyRecoil& recoil,
    DWORD maximumAgeMs) noexcept;

// The camera-shake boundary identifies BF1942's local camera soldier on the
// same game thread that later updates its animation Skeleton. Consumers may
// compare this pointer with their current callback argument, but must not
// dereference it outside that current callback.
void* ReadCurrentBFSoldierVrCameraSoldier() noexcept;

// Removes only the legacy player-camera recoil and shake contributions while
// BFVR is presenting. Weapon recoil state, animation, firing, spread, and the
// controller-directed fire transform remain owned by BF1942.
//
// The caller owns MinHook initialization and must remove this filter before
// calling MH_Uninitialize.
class BFSoldierVrMotionFilter
{
public:
    BFSoldierVrMotionFilter();
    ~BFSoldierVrMotionFilter();

    BFSoldierVrMotionFilter(const BFSoldierVrMotionFilter&) = delete;
    BFSoldierVrMotionFilter& operator=(const BFSoldierVrMotionFilter&) = delete;

    bool Create(void* gameImage, BFSoldierVrMotionFilterLogCallback logCallback);
    bool Enable();
    void DisableAndRemove();
    void LogSummary() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bfvr
