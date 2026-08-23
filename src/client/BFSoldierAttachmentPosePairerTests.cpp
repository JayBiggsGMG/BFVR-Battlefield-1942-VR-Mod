#include "client/BFSoldierAttachmentPosePairer.h"

#include <cstdio>

namespace
{

bool Expect(const bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

bfvr::BFSoldierAttachmentPoseSample Sample(const float x) noexcept
{
    bfvr::BFSoldierAttachmentPoseSample sample = {};
    sample.rightHandLocal.values[3][0] = x;
    sample.rightHandLocal.values[3][3] = 1.0F;
    sample.leftHandLocal.values[3][0] = x + 10.0F;
    sample.leftHandLocal.values[3][3] = 1.0F;
    sample.leftHandBone = 7;
    sample.leftHandValid = true;
    return sample;
}

bool PairsCurrentItemWithPreviousSubmittedHand() noexcept
{
    bfvr::BFSoldierAttachmentPosePairer pairer;
    const auto* const soldier = reinterpret_cast<void*>(0x1000);
    const auto* const skeleton = reinterpret_cast<void*>(0x2000);
    const auto* const item = reinterpret_cast<void*>(0x3000);
    const auto first = pairer.Advance(soldier, skeleton, item, Sample(1.0F));
    const auto second = pairer.Advance(soldier, skeleton, item, Sample(2.0F));
    return Expect(!first.has_value(), "first attachment callback was paired") &&
        Expect(
            second.has_value() &&
                second->rightHandLocal.values[3][0] == 1.0F &&
                second->leftHandLocal.values[3][0] == 11.0F &&
                second->leftHandBone == 7 && second->leftHandValid,
            "item transform was not paired with the prior submitted hands");
}

bool BindingChangesCannotReuseAHandSample() noexcept
{
    bfvr::BFSoldierAttachmentPosePairer pairer;
    const auto* const soldier = reinterpret_cast<void*>(0x1000);
    const auto* const skeleton = reinterpret_cast<void*>(0x2000);
    const auto* const firstItem = reinterpret_cast<void*>(0x3000);
    const auto* const secondItem = reinterpret_cast<void*>(0x4000);
    [[maybe_unused]] const auto first = pairer.Advance(
        soldier, skeleton, firstItem, Sample(1.0F));
    const auto switched = pairer.Advance(
        soldier, skeleton, secondItem, Sample(2.0F));
    const auto settled = pairer.Advance(
        soldier, skeleton, secondItem, Sample(3.0F));
    return Expect(
               !switched.has_value(),
               "weapon switch reused the prior item's hand sample") &&
        Expect(
               settled.has_value() &&
                   settled->rightHandLocal.values[3][0] == 2.0F,
               "new item did not establish its own deferred hand sample");
}

bool ResetRequiresACompleteNewPair() noexcept
{
    bfvr::BFSoldierAttachmentPosePairer pairer;
    const auto* const soldier = reinterpret_cast<void*>(0x1000);
    const auto* const skeleton = reinterpret_cast<void*>(0x2000);
    const auto* const item = reinterpret_cast<void*>(0x3000);
    [[maybe_unused]] const auto first = pairer.Advance(
        soldier, skeleton, item, Sample(1.0F));
    pairer.Reset();
    return Expect(
        !pairer.Advance(soldier, skeleton, item, Sample(2.0F)).has_value(),
        "reset retained a deferred attachment sample");
}

} // namespace

int main()
{
    const bool passed =
        PairsCurrentItemWithPreviousSubmittedHand() &&
        BindingChangesCannotReuseAHandSample() &&
        ResetRequiresACompleteNewPair();
    if (!passed)
    {
        return 1;
    }
    std::puts("BFVR BFSoldier attachment-pose pairer tests passed.");
    return 0;
}
