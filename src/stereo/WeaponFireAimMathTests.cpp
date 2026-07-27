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
        bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
        bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
        bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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

bool TestNativePositionAndBodyFrameArePreserved()
{
    constexpr std::string_view test = "native fire body frame";
    auto native = IdentityMatrix();
    native.values[0][0] = 0.0F;
    native.values[0][2] = -1.0F;
    native.values[2][0] = 1.0F;
    native.values[2][2] = 0.0F;
    native.values[3][0] = -50.0F;
    native.values[3][1] = 7.0F;
    native.values[3][2] = 91.0F;
    auto visual = YawRightAngle();
    visual.values[3][0] = 8.0F;
    visual.values[3][1] = -3.0F;
    visual.values[3][2] = 12.0F;

    const auto adjusted =
        bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
            native,
            visual);
    return Expect(adjusted.has_value(), test, "valid composition was rejected") &&
        Expect(
            NearlyEqual(adjusted->values[3][0], -50.0F) &&
                NearlyEqual(adjusted->values[3][1], 7.0F) &&
                NearlyEqual(adjusted->values[3][2], 91.0F),
            test,
            "visual weapon transform changed native fire position") &&
        Expect(
            NearlyEqual(adjusted->values[2][0], 0.0F) &&
                NearlyEqual(adjusted->values[2][1], 0.0F) &&
                NearlyEqual(adjusted->values[2][2], 1.0F),
            test,
            "visual orientation was not composed through native body orientation");
}

bool TestFireOrientationMatchesVisualReplay()
{
    constexpr std::string_view test = "visual/fire orientation invariant";
    auto nativeBody = IdentityMatrix();
    nativeBody.values[0][0] = 0.0F;
    nativeBody.values[0][2] = -1.0F;
    nativeBody.values[2][0] = 1.0F;
    nativeBody.values[2][2] = 0.0F;
    auto bodyView = IdentityMatrix();
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            bodyView.values[row][column] =
                nativeBody.values[column][row];
        }
    }
    auto visualViewOffset = PitchUpRightAngle();
    visualViewOffset.values[3][0] = 0.2F;
    visualViewOffset.values[3][1] = -0.1F;
    visualViewOffset.values[3][2] = 0.4F;

    const auto worldOffset =
        bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(
            bodyView,
            visualViewOffset);
    const auto renderedWorld = worldOffset.has_value()
        ? bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
            nativeBody,
            *worldOffset)
        : std::nullopt;
    const auto adjustedFire =
        bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
            nativeBody,
            visualViewOffset);
    if (!Expect(
            worldOffset.has_value() &&
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
    return true;
}

bool TestInvalidInputsFailClosed()
{
    constexpr std::string_view test = "visual weapon fail-closed inputs";
    auto scaledNative = IdentityMatrix();
    scaledNative.values[0][0] = 2.0F;
    if (!Expect(
            !bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
            !bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
            !bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
        !bfvr::stereo::MakeD3D8VisualWeaponFireMatrix(
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
        TestNativePositionAndBodyFrameArePreserved() &&
        TestFireOrientationMatchesVisualReplay() &&
        TestInvalidInputsFailClosed();
    if (!passed)
    {
        return 1;
    }
    std::puts("BFVR weapon fire-aim math tests passed.");
    return 0;
}
