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

bfvr::stereo::Vec4 TransformRowVectorForTest(
    const bfvr::stereo::Vec4& vector,
    const bfvr::stereo::Matrix4& matrix) noexcept
{
    const float source[4] = {vector.x, vector.y, vector.z, vector.w};
    float result[4] = {};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            result[column] += source[row] * matrix.values[row][column];
        }
    }
    return {result[0], result[1], result[2], result[3]};
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

bool TestControllerAimDirectlyOwnsFireBasisAndOrigin()
{
    constexpr std::string_view test = "direct controller aim fire";
    auto native = YawRightAngle();
    native.values[3][0] = 100.0F;
    native.values[3][1] = 25.0F;
    native.values[3][2] = -40.0F;
    auto controllerGun = PitchUpRightAngle();
    controllerGun.values[3][0] = 101.25F;
    controllerGun.values[3][1] = 24.75F;
    controllerGun.values[3][2] = -39.50F;

    const auto directed =
        bfvr::stereo::MakeD3D8ControllerDirectedWeaponFireMatrix(
            native,
            controllerGun,
            true);
    const auto orientationOnly =
        bfvr::stereo::MakeD3D8ControllerDirectedWeaponFireMatrix(
            native,
            controllerGun,
            false);
    if (!Expect(
            directed.has_value() && orientationOnly.has_value(),
            test,
            "valid controller gun pose was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        directed->values[row][column],
                        controllerGun.values[row][column]),
                    test,
                    "native camera orientation leaked into controller fire"))
            {
                return false;
            }
        }
    }
    return Expect(
        NearlyEqual(directed->values[2][0], 0.0F) &&
            NearlyEqual(directed->values[2][1], 1.0F) &&
            NearlyEqual(directed->values[2][2], 0.0F),
        test,
        "fire forward did not equal controller gun forward") &&
        Expect(
            NearlyEqual(directed->values[3][0], controllerGun.values[3][0]) &&
                NearlyEqual(
                    directed->values[3][1],
                    controllerGun.values[3][1]) &&
                NearlyEqual(
                    directed->values[3][2],
                    controllerGun.values[3][2]),
            test,
            "direct fire did not use the held-gun origin") &&
        Expect(
            NearlyEqual(
                orientationOnly->values[3][0],
                native.values[3][0]) &&
                NearlyEqual(
                    orientationOnly->values[3][1],
                    native.values[3][1]) &&
                NearlyEqual(
                    orientationOnly->values[3][2],
                    native.values[3][2]),
            test,
            "orientation-only policy did not preserve native origin");
}

bool TestNativeHandPreservesAuthoredFireToWristRotation()
{
    constexpr std::string_view test = "authored fire-to-wrist rotation";
    auto nativeFire = YawRightAngle();
    nativeFire.values[3][0] = 100.0F;
    nativeFire.values[3][1] = 20.0F;
    nativeFire.values[3][2] = -40.0F;
    const auto authoredFireToHand = PitchUpRightAngle();
    auto nativeHand = MultiplyMatrices(authoredFireToHand, nativeFire);
    nativeHand.values[3][0] = 100.3F;
    nativeHand.values[3][1] = 19.8F;
    nativeHand.values[3][2] = -39.6F;

    const auto recovered =
        bfvr::stereo::MakeD3D8NativeHandFromFireRotation(
            nativeFire,
            nativeHand);
    if (!Expect(
            recovered.has_value(),
            test,
            "valid native fire/hand pair was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        recovered->values[row][column],
                        authoredFireToHand.values[row][column]),
                    test,
                    "recovered relation did not match the native rig"))
            {
                return false;
            }
        }
    }

    const auto controllerGun = YawRightAngle();
    const auto correctedHand =
        bfvr::stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
            controllerGun,
            *recovered);
    if (!Expect(
            correctedHand.has_value(),
            test,
            "valid controller/wrist relation was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        correctedHand->values[row][column],
                        nativeHand.values[row][column]),
                    test,
                    "local wrist correction was not pre-multiplied"))
            {
                return false;
            }
        }
    }

    auto invalidHand = nativeHand;
    invalidHand.values[0][0] = 2.0F;
    return Expect(
        !bfvr::stereo::MakeD3D8NativeHandFromFireRotation(
             nativeFire,
             invalidHand).has_value(),
        test,
        "non-rigid native hand pose was accepted") &&
        Expect(
            !bfvr::stereo::MakeD3D8ControllerDirectedNativeHandMatrix(
                 invalidHand,
                 *recovered).has_value(),
            test,
            "non-rigid controller pose was accepted");
}

bool TestAnatomicalHandRecoversItemFunctionalBasis()
{
    constexpr std::string_view test =
        "anatomical wrist recovers item functional basis";
    auto handFromFunctional = PitchUpRightAngle();
    handFromFunctional.values[3][0] = 0.12F;
    handFromFunctional.values[3][1] = -0.05F;
    handFromFunctional.values[3][2] = 0.08F;
    auto functional = YawRightAngle();
    functional.values[3][0] = 12.0F;
    functional.values[3][1] = -4.0F;
    functional.values[3][2] = 7.0F;
    const auto hand = MultiplyMatrices(handFromFunctional, functional);

    const auto captured =
        bfvr::stereo::MakeD3D8NativeHandFromFunctionalTransform(
            functional,
            hand);
    if (!Expect(
            captured.has_value(),
            test,
            "valid translated hand/item relation was rejected"))
    {
        return false;
    }
    const auto recovered =
        bfvr::stereo::MakeD3D8FunctionalFromNativeHandTransform(
            hand,
            *captured);
    if (!Expect(
            recovered.has_value(),
            test,
            "valid anatomical hand/item relation was rejected"))
    {
        return false;
    }
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!Expect(
                    NearlyEqual(
                        recovered->values[row][column],
                        functional.values[row][column]),
                    test,
                    "inverse authored relation did not reconstruct the item basis"))
            {
                return false;
            }
        }
    }
    auto invalidRelation = handFromFunctional;
    invalidRelation.values[1][1] = 2.0F;
    return Expect(
        !bfvr::stereo::MakeD3D8FunctionalFromNativeHandTransform(
             hand,
             invalidRelation).has_value(),
        test,
        "non-rigid hand-to-functional relation was accepted");
}

bool TestLegacyCameraRecoilBecomesHeldWeaponRecoil()
{
    constexpr std::string_view test = "legacy recoil transferred to weapon";
    auto weaponToGrip = IdentityMatrix();
    weaponToGrip.values[3][0] = -0.24F;
    weaponToGrip.values[3][1] = 0.08F;
    weaponToGrip.values[3][2] = -0.51F;
    const bfvr::stereo::Pose grip = {
        {0.75F, 1.20F, -1.75F},
        {0.0F, 0.0F, 0.0F, 1.0F}};
    const auto accumulatedOnce = bfvr::stereo::AccumulateD3D8WeaponRecoil(
        {},
        0.20F,
        -0.05F);
    const auto accumulatedTwice = accumulatedOnce.has_value()
        ? bfvr::stereo::AccumulateD3D8WeaponRecoil(
            *accumulatedOnce,
            0.10F,
            0.02F)
        : std::nullopt;
    if (!Expect(
            accumulatedTwice.has_value() &&
                NearlyEqual(accumulatedTwice->pitch, 0.30F) &&
                NearlyEqual(accumulatedTwice->yaw, -0.03F),
            test,
            "native recoil impulses were not accumulated exactly once"))
    {
        return false;
    }

    const auto recoiled =
        bfvr::stereo::MakeD3D8AbsoluteGripWeaponRecoilDelta(
            weaponToGrip,
            grip,
            1.0F,
            90.0F,
            0.0F);
    if (!Expect(recoiled.has_value(), test, "valid recoil was rejected") ||
        !Expect(
            NearlyEqual(recoiled->values[2][0], 0.0F) &&
                NearlyEqual(recoiled->values[2][1], -1.0F) &&
                NearlyEqual(recoiled->values[2][2], 0.0F),
            test,
            "positive legacy pitch did not rotate the physical gun in the inverse camera sense") ||
        !Expect(
            [&]
            {
                // weaponToGrip maps this local anchor to the grip origin.
                // It must remain at the live grip after the gun recoils.
                const auto anchor = TransformRowVectorForTest(
                    {0.24F, -0.08F, 0.51F, 1.0F},
                    *recoiled);
                return NearlyEqual(anchor.x, 0.75F) &&
                    NearlyEqual(anchor.y, 1.20F) &&
                    NearlyEqual(anchor.z, 1.75F) &&
                    NearlyEqual(anchor.w, 1.0F);
            }(),
            test,
            "recoil rotation moved the controller grip anchor"))
    {
        return false;
    }

    const auto invalid = bfvr::stereo::MakeD3D8AbsoluteGripWeaponRecoilDelta(
        weaponToGrip,
        grip,
        1.0F,
        std::numeric_limits<float>::quiet_NaN(),
        0.0F);
    const auto invalidAccumulation = bfvr::stereo::AccumulateD3D8WeaponRecoil(
        {},
        std::numeric_limits<float>::quiet_NaN(),
        0.0F);
    return Expect(
        !invalid.has_value(),
        test,
        "non-finite engine recoil rotation was accepted") &&
        Expect(
            !invalidAccumulation.has_value(),
            test,
            "non-finite engine recoil impulse was accepted");
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
    const auto moved =
        bfvr::stereo::MakeD3D8WorldAttachedWeaponFireMatrix(
            native,
            visual,
            true);
    const auto expectedMoved = MultiplyMatrices(native, visual);
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
            "visual orientation was not composed after the native world orientation") &&
        Expect(
            moved.has_value() &&
                NearlyEqual(
                    moved->values[3][0],
                    expectedMoved.values[3][0]) &&
                NearlyEqual(
                    moved->values[3][1],
                    expectedMoved.values[3][1]) &&
                NearlyEqual(
                    moved->values[3][2],
                    expectedMoved.values[3][2]),
            test,
            "native-arm fire origin did not follow the complete world attachment");
}

bool TestNativeArmFireAnchorDistancesRejectCinematicOrigin()
{
    constexpr std::string_view test = "native-arm fire anchor distances";
    auto nativeHand = IdentityMatrix();
    nativeHand.values[3][0] = 100.0F;
    nativeHand.values[3][1] = 20.0F;
    nativeHand.values[3][2] = -50.0F;
    auto targetHand = nativeHand;
    targetHand.values[3][0] += 0.35F;
    targetHand.values[3][1] -= 0.20F;
    targetHand.values[3][2] += 0.10F;
    auto nearbyFire = nativeHand;
    nearbyFire.values[3][0] += 0.55F;
    auto cinematicFire = nativeHand;
    cinematicFire.values[3][1] += 4.50F;

    const auto nearby =
        bfvr::stereo::MeasureD3D8NativeArmFireAnchorDistances(
            nearbyFire,
            nativeHand,
            targetHand);
    const auto cinematic =
        bfvr::stereo::MeasureD3D8NativeArmFireAnchorDistances(
            cinematicFire,
            nativeHand,
            targetHand);
    auto invalidHand = nativeHand;
    invalidHand.values[0][0] = 2.0F;
    const auto invalid =
        bfvr::stereo::MeasureD3D8NativeArmFireAnchorDistances(
            nearbyFire,
            invalidHand,
            targetHand);

    return Expect(
               nearby.has_value() &&
                   NearlyEqual(nearby->nativeFireToHand, 0.55F) &&
                   NearlyEqual(
                       nearby->solvedHandDisplacement,
                       std::sqrt(0.35F * 0.35F +
                           0.20F * 0.20F +
                           0.10F * 0.10F)),
               test,
               "nearby native muzzle/hand pair was measured incorrectly") &&
        Expect(
            cinematic.has_value() &&
                cinematic->nativeFireToHand > 1.25F,
            test,
            "distant cinematic origin did not exceed the gameplay guard") &&
        Expect(
            !invalid.has_value(),
            test,
            "non-rigid hand anchor was accepted");
}

bool TestNativeArmFireAssociationExtendsOnlyExactActiveItem()
{
    constexpr std::string_view test =
        "native-arm exact active-item fire association";
    const bfvr::stereo::NativeArmFireAnchorDistances firstSpawn = {
        1.272F,
        1.035F};
    const bfvr::stereo::NativeArmFireAnchorDistances cinematic = {
        4.50F,
        1.035F};
    const bfvr::stereo::NativeArmFireAnchorDistances displacedHand = {
        0.718F,
        1.501F};

    return Expect(
               NearlyEqual(
                   bfvr::stereo::SelectD3D8NativeArmFireToHandLimit(false),
                   1.25F),
               test,
               "unverified fire no longer uses the original 1.25 m limit") &&
        Expect(
            NearlyEqual(
                bfvr::stereo::SelectD3D8NativeArmFireToHandLimit(true),
                1.35F),
            test,
            "exact active-item fire did not select the bounded 1.35 m limit") &&
        Expect(
            !bfvr::stereo::IsD3D8NativeArmFireAnchorWithinPolicy(
                firstSpawn,
                false),
            test,
            "first-spawn distance was widened without exact item identity") &&
        Expect(
            bfvr::stereo::IsD3D8NativeArmFireAnchorWithinPolicy(
                firstSpawn,
                true),
            test,
            "observed exact-item first-spawn distance remained rejected") &&
        Expect(
            !bfvr::stereo::IsD3D8NativeArmFireAnchorWithinPolicy(
                cinematic,
                true),
            test,
            "multi-metre cinematic origin passed exact-item policy") &&
        Expect(
            !bfvr::stereo::IsD3D8NativeArmFireAnchorWithinPolicy(
                displacedHand,
                true),
            test,
            "solved-hand displacement guard was weakened");
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
        TestControllerAimDirectlyOwnsFireBasisAndOrigin() &&
        TestNativeHandPreservesAuthoredFireToWristRotation() &&
        TestAnatomicalHandRecoversItemFunctionalBasis() &&
        TestLegacyCameraRecoilBecomesHeldWeaponRecoil() &&
        TestNativePositionAndWorldAttachmentArePreserved() &&
        TestNativeArmFireAnchorDistancesRejectCinematicOrigin() &&
        TestNativeArmFireAssociationExtendsOnlyExactActiveItem() &&
        TestFireOrientationMatchesSourceViewConjugatedVisualReplay() &&
        TestInvalidInputsFailClosed();
    if (!passed)
    {
        return 1;
    }
    std::puts("BFVR weapon fire-aim math tests passed.");
    return 0;
}
