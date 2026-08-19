#include "client/BFSoldierWristPositionBinding.h"

#include "stereo/ArmVrPoseMath.h"

namespace bfvr
{

std::optional<std::array<float, 3>>
BFSoldierWristPositionBinding::Update(
    const void* soldier,
    const void* skeleton,
    const std::int32_t handBone,
    const stereo::Quaternion& gripOrientation) noexcept
{
    if (soldier == nullptr || skeleton == nullptr || handBone < 0)
    {
        Reset();
        return std::nullopt;
    }
    if (!valid_ || soldier_ != soldier || skeleton_ != skeleton ||
        handBone_ != handBone)
    {
        soldier_ = soldier;
        skeleton_ = skeleton;
        handBone_ = handBone;
        referenceGripOrientation_ = gripOrientation;
        valid_ = true;
    }

    stereo::ArmVrWristOffsetInput input = {};
    input.referenceGripOrientation = referenceGripOrientation_;
    input.currentGripOrientation = gripOrientation;
    const auto result = stereo::ComputeArmVrWristOffsetDelta(input);
    if (!result.has_value())
    {
        Reset();
    }
    return result;
}

void BFSoldierWristPositionBinding::Reset() noexcept
{
    soldier_ = nullptr;
    skeleton_ = nullptr;
    handBone_ = -1;
    referenceGripOrientation_ = {};
    valid_ = false;
}

} // namespace bfvr
