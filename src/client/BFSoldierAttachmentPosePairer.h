#pragma once

#include "stereo/StereoMath.h"

#include <optional>

namespace bfvr
{

struct BFSoldierAttachmentPoseSample
{
    stereo::Matrix4 rightHandLocal = {};
    stereo::Matrix4 leftHandLocal = {};
    const void* skeleton = nullptr;
    long leftHandBone = -1;
    bool leftHandValid = false;
};

// BF1942 updates an attached item's world transform after submitting its hand
// matrix. Pairing the item transform visible at callback N with the hand from
// callback N-1 keeps both inputs on the same native attachment update.
class BFSoldierAttachmentPosePairer final
{
public:
    [[nodiscard]] std::optional<BFSoldierAttachmentPoseSample> Advance(
        const void* soldier,
        const void* skeleton,
        const void* itemInterface,
        const BFSoldierAttachmentPoseSample& current) noexcept
    {
        if (soldier == nullptr || skeleton == nullptr ||
            itemInterface == nullptr)
        {
            Reset();
            return std::nullopt;
        }
        const bool sameBinding = valid_ && soldier_ == soldier &&
            skeleton_ == skeleton && itemInterface_ == itemInterface;
        const auto previous = sameBinding
            ? std::optional<BFSoldierAttachmentPoseSample>(sample_)
            : std::nullopt;
        soldier_ = soldier;
        skeleton_ = skeleton;
        itemInterface_ = itemInterface;
        sample_ = current;
        valid_ = true;
        return previous;
    }

    void Reset() noexcept
    {
        soldier_ = nullptr;
        skeleton_ = nullptr;
        itemInterface_ = nullptr;
        sample_ = {};
        valid_ = false;
    }

private:
    const void* soldier_ = nullptr;
    const void* skeleton_ = nullptr;
    const void* itemInterface_ = nullptr;
    BFSoldierAttachmentPoseSample sample_ = {};
    bool valid_ = false;
};

} // namespace bfvr
