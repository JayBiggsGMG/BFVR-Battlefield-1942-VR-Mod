#pragma once

#include <cstdint>

namespace bfvr
{

class BFSoldierNativeArmPole final
{
public:
    [[nodiscard]] bool Start(
        void* gameImage,
        void (*appendLog)(const wchar_t* message)) noexcept;
    void Stop() noexcept;

    void BeginFrame(
        void* skeleton,
        std::int32_t rightHandBone,
        std::int32_t rightHandleIndex,
        std::int32_t leftHandBone,
        std::int32_t leftHandleIndex,
        std::int32_t activeItemIndex) noexcept;
    void CaptureSolvedEndpoints(
        const void* rightBoneRecord,
        const void* leftBoneRecord) noexcept;
    void EndFrame() noexcept;

private:
    using MayaApplyIk2BoneSolverFn = void(__fastcall*)(
        const float* pole,
        const float* shoulder,
        const float* elbow,
        const float* wrist,
        const float* handTarget,
        void* upperRotation,
        void* forearmRotation);

    static void __fastcall MayaApplyIk2BoneSolverHook(
        const float* pole,
        const float* shoulder,
        const float* elbow,
        const float* wrist,
        const float* handTarget,
        void* upperRotation,
        void* forearmRotation);

    void WriteLog(const wchar_t* format, ...) const noexcept;

    static BFSoldierNativeArmPole* active_;
    void* target_ = nullptr;
    MayaApplyIk2BoneSolverFn original_ = nullptr;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    const float* priorRightTarget_ = nullptr;
    const float* priorLeftTarget_ = nullptr;
    std::int32_t priorActiveItemIndex_ = -1;
    float previousRightPole_[3] = {};
    float previousLeftPole_[3] = {};
    volatile long callbackEntrants_ = 0;
    volatile long appliedRight_ = 0;
    volatile long appliedLeft_ = 0;
    volatile long preservedNativePole_ = 0;
    volatile long rejected_ = 0;
    volatile long previousContinuity_ = 0;
    volatile long fallbackAxis_ = 0;
    volatile long endpointSamples_ = 0;
    volatile long endpointViolations_ = 0;
    bool hasPreviousRightPole_ = false;
    bool hasPreviousLeftPole_ = false;
    bool hookCreated_ = false;
    bool hookEnabled_ = false;
};

} // namespace bfvr
