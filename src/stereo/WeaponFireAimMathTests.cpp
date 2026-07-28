#include "stereo/WeaponFireAimMath.h"
#include "stereo/WeaponPoseMath.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>

namespace
{

constexpr float kTolerance = 0.0001F;

bool NearlyEqual(float lhs, float rhs) noexcept
{
    return std::fabs(lhs - rhs) <= kTolerance;
}

bfvr::stereo::Matrix4 IdentityMatrix() noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

bfvr::stereo::Matrix4 YawRightAngle() noexcept
{
    auto result = IdentityMatrix();
    result.values[0][0] = 0.0F;
    result.values[0][2] = 1.0F;
    result.values[2][0] = -1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

bfvr::stereo::Matrix4 PitchUpRightAngle() noexcept
{
    auto result = IdentityMatrix();
    result.values[1][1] = 0.0F;
    result.values[1][2] = -1.0F;
    result.values[2][1] = 1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

bfvr::stereo::Matrix4 MultiplyMatrices(
    const bfvr::stereo::Matrix4& lhs,
    const bfvr::stereo::Matrix4& rhs) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                result.values[row][column] +=
                    lhs.values[row][inner] * rhs.values[inner][column];
            }
        }
    }
    return result;
}

bool Expect(
    bool condition,
    std::string_view test,
    std::string_view detail)
{
    if (condition)
    {
        return true;
    }
    std::fprintf(
        stderr,
        "[FAIL] %.*s: %.*s\n",
        static_cast<int>(test.size()),
        test.data(),
        static_cast<int>(detail.size()),
        detail.data());
    return false;
}

bool TestNeutralVisualWeaponPreservesNativeFireMatrix()
{
    constexpr std::string_view test = "neutral visual weapon";
    auto native = IdentityMatrix();
    native.values[0][0] = 0.0F;
    native.values[0][2] = -1.0F;
    native.values[2][0] = 1.0F;
    native.values[2][2] = 0.0F;
    native.values[3][0] = 1231.39F;
    native.values[3][1] = 106.392F;
    native.values[3][2] = 1697.94F;

    const auto adjusted =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            native,
            IdentityMatrix());
    if (!Expect(adjusted.has_value(), test, "valid rigid input was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        adjusted->values[row][column],
                        native.values[row][column]),
                    test,
                    "identity visual orientation changed the native matrix"))
            {
                return false;
            }
        }
    }
    return true;
}

bool TestRenderedWeaponForwardBecomesFireForward()
{
    constexpr std::string_view test = "rendered weapon direction";
    const auto native = IdentityMatrix();
    const auto yawed =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            native,
            YawRightAngle());
    if (!Expect(yawed.has_value(), test, "yawed weapon was rejected") ||
        !Expect(
            NearlyEqual(yawed->values[2][0], -1.0F) &&
                NearlyEqual(yawed->values[2][1], 0.0F) &&
                NearlyEqual(yawed->values[2][2], 0.0F),
            test,
            "rendered -X barrel did not produce -X fire forward"))
    {
        return false;
    }

    const auto pitched =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            native,
            PitchUpRightAngle());
    return Expect(pitched.has_value(), test, "pitched weapon was rejected") &&
        Expect(
            NearlyEqual(pitched->values[2][0], 0.0F) &&
                NearlyEqual(pitched->values[2][1], 1.0F) &&
                NearlyEqual(pitched->values[2][2], 0.0F),
            test,
            "rendered +Y barrel did not produce +Y fire forward");
}

bool TestNativePositionAndWorldAttachmentArePreserved()
{
    constexpr std::string_view test = "native fire world attachment";
    auto native = YawRightAngle();
    native.values[3][0] = -50.0F;
    native.values[3][1] = 7.0F;
    native.values[3][2] = 91.0F;
    auto visual = PitchUpRightAngle();
    visual.values[3][0] = 8.0F;
    visual.values[3][1] = -3.0F;
    visual.values[3][2] = 12.0F;

    const auto adjusted =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            native,
            visual);
    bool orientationMatches = adjusted.has_value();
    if (orientationMatches)
    {
        const auto expected = MultiplyMatrices(native, visual);
        for (std::size_t row = 0; row < 3 && orientationMatches; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                if (!NearlyEqual(
                        adjusted->values[row][column],
                        expected.values[row][column]))
                {
                    orientationMatches = false;
                    break;
                }
            }
        }
    }

    return Expect(adjusted.has_value(), test, "valid composition was rejected") &&
        Expect(
            NearlyEqual(adjusted->values[3][0], -50.0F) &&
                NearlyEqual(adjusted->values[3][1], 7.0F) &&
                NearlyEqual(adjusted->values[3][2], 91.0F),
            test,
            "visual weapon transform changed native fire position") &&
        Expect(
            orientationMatches,
            test,
            "visual orientation was not composed after the native world orientation");
}

bool TestFireOrientationMatchesSourceViewConjugatedVisualReplay()
{
    constexpr std::string_view test =
        "source-view-conjugated visual/fire orientation invariant";
    auto nativeWorld = YawRightAngle();
    nativeWorld.values[3][0] = 31.0F;
    nativeWorld.values[3][1] = -7.0F;
    nativeWorld.values[3][2] = 12.0F;
    auto sourceView = PitchUpRightAngle();
    sourceView.values[3][0] = 0.31F;
    sourceView.values[3][1] = -0.47F;
    sourceView.values[3][2] = 0.86F;
    auto visualViewOffset = YawRightAngle();
    visualViewOffset.values[3][0] = 0.2F;
    visualViewOffset.values[3][1] = -0.1F;
    visualViewOffset.values[3][2] = 0.4F;

    const auto worldAttachment =
        bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(
            sourceView,
            visualViewOffset);
    const auto renderedWorld = worldAttachment.has_value()
        ? bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
            nativeWorld,
            *worldAttachment)
        : std::nullopt;
    const auto adjustedFire =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            nativeWorld,
            worldAttachment.value_or(IdentityMatrix()));
    if (!Expect(
            worldAttachment.has_value() &&
                renderedWorld.has_value() &&
                adjustedFire.has_value(),
            test,
            "valid visual or fire composition was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        renderedWorld->values[row][column],
                        adjustedFire->values[row][column]),
                    test,
                    "rendered and firing orientations diverged"))
            {
                return false;
            }
        }
    }
    if (!Expect(
            NearlyEqual(adjustedFire->values[3][0], nativeWorld.values[3][0]) &&
                NearlyEqual(adjustedFire->values[3][1], nativeWorld.values[3][1]) &&
                NearlyEqual(adjustedFire->values[3][2], nativeWorld.values[3][2]),
            test,
            "controller attachment moved the native muzzle origin"))
    {
        return false;
    }

    const auto actualVisual = MultiplyMatrices(
        *renderedWorld,
        sourceView);
    const auto expectedVisual = MultiplyMatrices(
        MultiplyMatrices(nativeWorld, sourceView),
        visualViewOffset);
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        actualVisual.values[row][column],
                        expectedVisual.values[row][column]),
                    test,
                    "world attachment did not reproduce sourceView * viewOffset"))
            {
                return false;
            }
        }
    }

    const auto bodyFrameAttachment =
        bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(
            IdentityMatrix(),
            visualViewOffset);
    const auto bodyFrameWorld =
        bodyFrameAttachment.has_value()
        ? bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
            nativeWorld,
            *bodyFrameAttachment)
        : std::nullopt;
    bool distinguishesSourceView = false;
    if (bodyFrameWorld.has_value())
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                distinguishesSourceView = distinguishesSourceView ||
                    !NearlyEqual(
                        renderedWorld->values[row][column],
                        bodyFrameWorld->values[row][column]);
            }
        }
    }
    return Expect(
        distinguishesSourceView,
        test,
        "fixture did not distinguish the old body-frame attachment order");
}

bool TestInvalidInputsFailClosed()
{
    constexpr std::string_view test = "visual weapon fail-closed inputs";
    auto scaledNative = IdentityMatrix();
    scaledNative.values[0][0] = 2.0F;
    if (!Expect(
            !bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
                 scaledNative,
                 IdentityMatrix()).has_value(),
            test,
            "non-rigid native fire matrix was accepted"))
    {
        return false;
    }

    auto scaledVisual = IdentityMatrix();
    scaledVisual.values[1][1] = 0.5F;
    if (!Expect(
            !bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
                 IdentityMatrix(),
                 scaledVisual).has_value(),
            test,
            "non-rigid visual weapon matrix was accepted"))
    {
        return false;
    }

    auto reflected = IdentityMatrix();
    reflected.values[2][2] = -1.0F;
    if (!Expect(
            !bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
                 IdentityMatrix(),
                 reflected).has_value(),
            test,
            "reflected visual weapon matrix was accepted"))
    {
        return false;
    }

    auto nonFinite = IdentityMatrix();
    nonFinite.values[0][0] =
        std::numeric_limits<float>::quiet_NaN();
    return Expect(
        !bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
             IdentityMatrix(),
             nonFinite).has_value(),
        test,
        "non-finite visual weapon matrix was accepted");
}

} // namespace

int main()
{
    const bool passed =
        TestNeutralVisualWeaponPreservesNativeFireMatrix() &&
        TestRenderedWeaponForwardBecomesFireForward() &&
        TestNativePositionAndWorldAttachmentArePreserved() &&
        TestFireOrientationMatchesSourceViewConjugatedVisualReplay() &&
        TestInvalidInputsFailClosed();
    if (!passed)
    {
        return 1;
    }
    std::puts("BFVR weapon fire-aim math tests passed.");
    return 0;
}
