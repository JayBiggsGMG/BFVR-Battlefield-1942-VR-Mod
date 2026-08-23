#include "client/BFSoldierOffHandOverrides.h"

#include <array>
#include <cmath>
#include <limits>

namespace
{

using Matrix4 = bfvr::stereo::Matrix4;

struct OffHandOverride
{
    bfvr::BFSoldierOffHandWeaponFingerprint weaponFingerprint;
    Matrix4 nativeRelation;
    Matrix4 calibratedRelation;
};

constexpr Matrix4 Relation(const std::array<float, 12>& values) noexcept
{
    Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = values[row * 3 + column];
        }
    }
    result.values[3][3] = 1.0F;
    return result;
}

float NativeFingerprintError(
    const Matrix4& current,
    const Matrix4& recorded) noexcept
{
    constexpr float kMaximumBasisElementDifference = 0.015F;
    constexpr float kMaximumTranslationDifferenceMetres = 0.010F;
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!std::isfinite(current.values[row][column]) ||
                std::fabs(
                    current.values[row][column] -
                    recorded.values[row][column]) >
                    kMaximumBasisElementDifference)
            {
                return std::numeric_limits<float>::infinity();
            }
        }
    }
    for (std::size_t column = 0; column < 3; ++column)
    {
        if (!std::isfinite(current.values[3][column]) ||
            std::fabs(
                current.values[3][column] -
                recorded.values[3][column]) >
                kMaximumTranslationDifferenceMetres)
        {
            return std::numeric_limits<float>::infinity();
        }
    }
    float error = 0.0F;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            const float difference =
                current.values[row][column] - recorded.values[row][column];
            error += difference * difference;
        }
    }
    return error;
}

bool MatchesWeaponFingerprint(
    const bfvr::BFSoldierOffHandWeaponFingerprint& current,
    const bfvr::BFSoldierOffHandWeaponFingerprint& recorded) noexcept
{
    constexpr float kMaximumPropertyDifference = 0.0005F;
    if (!std::isfinite(current.zoomFov) ||
        std::fabs(current.zoomFov - recorded.zoomFov) >
            kMaximumPropertyDifference)
    {
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index)
    {
        if (!std::isfinite(current.soldierCameraPosition[index]) ||
            std::fabs(
                current.soldierCameraPosition[index] -
                recorded.soldierCameraPosition[index]) >
                kMaximumPropertyDifference)
        {
            return false;
        }
    }
    return true;
}

constexpr std::array<OffHandOverride, 6> kOverrides = {{
    {
        {0.6F, {-0.03F, -0.02F, -0.07F}},
        Relation({
            0.2986422F, -0.8820737F, 0.3643606F,
            -0.7125188F, -0.4600687F, -0.5297675F,
            0.6349251F, -0.1014027F, -0.7658902F,
            0.2204590F, -0.0816650F, 0.0508423F}),
        Relation({
            0.6275228F, -0.7633173F, 0.1535003F,
            -0.2594356F, -0.3908746F, -0.8831255F,
            0.7341043F, 0.5143575F, -0.4433146F,
            0.2825449F, -0.0187034F, -0.0982085F})
    },
    {
        {0.6F, {-0.01F, -0.04F, 0.09F}},
        Relation({
            0.3693111F, -0.3846701F, -0.8459539F,
            0.6523928F, -0.5409626F, 0.5307947F,
            -0.6618103F, -0.7479226F, 0.0511724F,
            0.2846680F, 0.1800537F, -0.0527344F}),
        Relation({
            0.8156028F, -0.4974883F, -0.2954625F,
            0.3691501F, 0.0541772F, 0.9277893F,
            -0.4455567F, -0.8657774F, 0.2278348F,
            0.3174495F, 0.1627778F, -0.1762243F})
    },
    {
        {0.6F, {-0.01F, -0.04F, 0.09F}},
        Relation({
            0.3690058F, -0.3842715F, -0.8462682F,
            0.6524482F, -0.5413660F, 0.5303151F,
            -0.6619259F, -0.7478356F, 0.0509501F,
            0.2846756F, 0.1801758F, -0.0525818F}),
        Relation({
            0.8347659F, -0.4633997F, -0.2973661F,
            0.3548947F, 0.0399177F, 0.9340540F,
            -0.4209702F, -0.8852503F, 0.1977798F,
            0.3151374F, 0.1538488F, -0.1775903F})
    },
    {
        {0.6F, {-0.02F, -0.03F, 0.01F}},
        Relation({
            0.7896664F, -0.5337967F, -0.3024703F,
            -0.5998216F, -0.7753332F, -0.1976677F,
            -0.1290008F, 0.3375196F, -0.9324376F,
            0.2968140F, -0.1567383F, 0.0127144F}),
        Relation({
            0.6897721F, -0.4213645F, 0.5887843F,
            -0.0938470F, -0.8583797F, -0.5043576F,
            0.7179189F, 0.2926361F, -0.6316301F,
            0.3243756F, 0.0544156F, -0.1433075F})
    },
    {
        {0.5F, {0.025F, 0.0F, 0.06F}},
        Relation({
            0.3668162F, -0.3814111F, -0.8485113F,
            0.6528491F, -0.5442396F, 0.5268694F,
            -0.6627473F, -0.7472142F, 0.0493681F,
            0.2843170F, 0.1806641F, -0.0510788F}),
        Relation({
            0.8376298F, -0.4881542F, -0.2451167F,
            0.2775514F, -0.0061380F, 0.9606913F,
            -0.4704702F, -0.8727358F, 0.1303466F,
            0.3172960F, 0.1594465F, -0.1808556F})
    },
    {
        {0.9F, {0.025F, 0.0F, 0.06F}},
        Relation({
            0.7902348F, -0.5337211F, -0.3011156F,
            -0.5995582F, -0.7749900F, -0.1998010F,
            -0.1267236F, 0.3384261F, -0.9324210F,
            0.2973633F, -0.1567383F, 0.0124817F}),
        Relation({
            0.6531161F, -0.6346301F, 0.4131398F,
            -0.2163717F, -0.6792235F, -0.7013121F,
            0.7256877F, 0.3686467F, -0.5809278F,
            0.3269300F, 0.0563503F, -0.1009266F})
    }
}};

} // namespace

namespace bfvr
{

std::optional<Matrix4> ResolveBFSoldierOffHandOverride(
    const BFSoldierOffHandWeaponFingerprint& weaponFingerprint,
    const LONG activeItemIndex,
    const Matrix4& nativeLeftHandFromRightHand) noexcept
{
    if (activeItemIndex != 3)
    {
        return std::nullopt;
    }
    const OffHandOverride* match = nullptr;
    float bestError = std::numeric_limits<float>::infinity();
    for (const OffHandOverride& candidate : kOverrides)
    {
        if (!MatchesWeaponFingerprint(
                weaponFingerprint, candidate.weaponFingerprint))
        {
            continue;
        }
        const float error = NativeFingerprintError(
            nativeLeftHandFromRightHand, candidate.nativeRelation);
        if (error < bestError)
        {
            bestError = error;
            match = &candidate;
        }
    }
    return match == nullptr
        ? std::nullopt
        : std::optional<Matrix4>(match->calibratedRelation);
}

} // namespace bfvr
