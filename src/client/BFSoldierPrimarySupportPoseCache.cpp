#include "client/BFSoldierPrimarySupportPoseCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace
{

constexpr LONG kPrimaryItemIndex = 3;

float TranslationDrift(
    const bfvr::stereo::Matrix4& left,
    const bfvr::stereo::Matrix4& right) noexcept
{
    const float x = left.values[3][0] - right.values[3][0];
    const float y = left.values[3][1] - right.values[3][1];
    const float z = left.values[3][2] - right.values[3][2];
    return std::sqrt(x * x + y * y + z * z);
}

float RotationDriftDegrees(
    const bfvr::stereo::Matrix4& left,
    const bfvr::stereo::Matrix4& right) noexcept
{
    float trace = 0.0F;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            trace += left.values[row][column] * right.values[row][column];
        }
    }
    const float cosine = std::clamp((trace - 1.0F) * 0.5F, -1.0F, 1.0F);
    return std::acos(cosine) * 180.0F / 3.14159265358979323846F;
}

} // namespace

namespace bfvr
{

void BFSoldierPrimarySupportPoseCache::Resolve(
    void* soldier,
    void* skeleton,
    const void* activeItem,
    const LONG activeItemIndex,
    stereo::Matrix4& leftHandFromRightHand,
    void (*appendLog)(const wchar_t* message)) noexcept
{
    if (soldier == nullptr || skeleton == nullptr || activeItem == nullptr ||
        activeItemIndex != kPrimaryItemIndex)
    {
        return;
    }

    stereo::Matrix4 cached = {};
    bool stored = false;
    bool reused = false;
    AcquireSRWLockExclusive(&lock_);
    if (!valid_ || soldier_ != soldier || skeleton_ != skeleton ||
        activeItem_ != activeItem)
    {
        relation_ = leftHandFromRightHand;
        soldier_ = soldier;
        skeleton_ = skeleton;
        activeItem_ = activeItem;
        valid_ = true;
        stored = true;
    }
    else
    {
        cached = relation_;
        reused = true;
    }
    ReleaseSRWLockExclusive(&lock_);

    if (reused)
    {
        const float translationDrift =
            TranslationDrift(leftHandFromRightHand, cached);
        const float rotationDrift =
            RotationDriftDegrees(leftHandFromRightHand, cached);
        leftHandFromRightHand = cached;
        if (appendLog != nullptr)
        {
            std::array<wchar_t, 768> message = {};
            _snwprintf_s(
                message.data(), message.size(), _TRUNCATE,
                L"Native 1P primary support reused its first known-good full "
                L"left-from-right relation after item return: soldier=%p "
                L"skeleton=%p item=%p rejectedDeployTranslation=%.4f m "
                L"rejectedDeployRotation=%.3f deg. The deploy sample cannot "
                L"steer the rifle or WeaponFire_Core.",
                soldier, skeleton, activeItem, translationDrift, rotationDrift);
            appendLog(message.data());
        }
    }
    else if (stored && appendLog != nullptr)
    {
        std::array<wchar_t, 512> message = {};
        _snwprintf_s(
            message.data(), message.size(), _TRUNCATE,
            L"Native 1P primary support stored its first full authored "
            L"left-from-right relation: soldier=%p skeleton=%p item=%p.",
            soldier, skeleton, activeItem);
        appendLog(message.data());
    }
}

void BFSoldierPrimarySupportPoseCache::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    relation_ = {};
    soldier_ = nullptr;
    skeleton_ = nullptr;
    activeItem_ = nullptr;
    valid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
