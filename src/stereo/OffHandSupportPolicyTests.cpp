#include "stereo/OffHandSupportPolicy.h"
#include "stereo/OffHandWeaponSteeringMath.h"
#include "client/BFSoldierOffHandWeaponSteering.h"
#include "client/BFSoldierOffHandSupportBinding.h"
#include "client/BFSoldierOffHandCalibration.h"
#include "client/BFSoldierOffHandOverrides.h"
#include "client/BFSoldierLeftGripRotationBinding.h"
#include "client/BFSoldierNativeArmMath.h"
#include "client/BFSoldierPrimarySupportPoseCache.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

using bfvr::stereo::OffHandSupportPolicy;
using bfvr::stereo::OffHandSupportSample;
using bfvr::stereo::OffHandSupportState;
using bfvr::stereo::Matrix4;

bool Expect(
    const bool condition,
    const char* message) noexcept
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
    return condition;
}

OffHandSupportSample ValidSample(
    const double timeSeconds,
    const float distanceMetres,
    const std::uint64_t bindingId = 1) noexcept
{
    OffHandSupportSample sample = {};
    sample.bindingId = bindingId;
    sample.timeSeconds = timeSeconds;
    sample.supportDistanceMetres = distanceMetres;
    sample.sessionFocused = true;
    sample.leftGripTracked = true;
    sample.leftGripHeld = true;
    sample.supportPoseValid = true;
    return sample;
}

Matrix4 Translation(
    const float x,
    const float y,
    const float z) noexcept
{
    Matrix4 result = {};
    result.values[0][0] = 1.0F;
    result.values[1][1] = 1.0F;
    result.values[2][2] = 1.0F;
    result.values[3][0] = x;
    result.values[3][1] = y;
    result.values[3][2] = z;
    result.values[3][3] = 1.0F;
    return result;
}

Matrix4 YawRightAngle() noexcept
{
    auto result = Translation(0.0F, 0.0F, 0.0F);
    result.values[0][0] = 0.0F;
    result.values[0][2] = 1.0F;
    result.values[2][0] = -1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

Matrix4 PitchUpRightAngle() noexcept
{
    auto result = Translation(0.0F, 0.0F, 0.0F);
    result.values[1][1] = 0.0F;
    result.values[1][2] = 1.0F;
    result.values[2][1] = -1.0F;
    result.values[2][2] = 0.0F;
    return result;
}

bool LearnedLeftWristReferenceSurvivesItemAndTrackingChanges() noexcept
{
    bfvr::BFSoldierLeftGripRotationBinding binding;
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const primary = reinterpret_cast<void*>(0x3000);
    auto* const gadget = reinterpret_cast<void*>(0x4000);
    const auto grip = Translation(0.0F, 0.0F, 0.0F);
    const auto authoredPrimary = YawRightAngle();
    const auto gadgetNative = PitchUpRightAngle();
    Matrix4 target = {};
    bool created = false;
    if (!binding.Update(
            soldier, skeleton, primary, 21, grip, authoredPrimary,
            target, created))
    {
        return Expect(false, "left wrist fallback could not initialize");
    }
    binding.CaptureAnatomicalReference(
        soldier, skeleton, primary, 21, grip, authoredPrimary, nullptr);
    created = false;
    if (!binding.Update(
            soldier, skeleton, gadget, 21, grip, gadgetNative,
            target, created) ||
        !Expect(
            std::fabs(target.values[0][2] - 1.0F) < 0.0001F &&
                std::fabs(target.values[1][2]) < 0.0001F && !created,
            "gadget switch replaced the learned anatomical left wrist"))
    {
        return false;
    }
    binding.ResetTransient();
    created = false;
    if (!binding.Update(
            soldier, skeleton, gadget, 21, grip, gadgetNative,
            target, created) ||
        !Expect(
            std::fabs(target.values[0][2] - 1.0F) < 0.0001F && !created,
            "tracking reset discarded the learned anatomical left wrist"))
    {
        return false;
    }
    binding.Reset();
    created = false;
    return binding.Update(
               soldier, skeleton, gadget, 21, grip, gadgetNative,
               target, created) &&
        Expect(
            std::fabs(target.values[1][2] - 1.0F) < 0.0001F && created,
            "full lifetime reset retained the prior anatomical left wrist");
}

bool PrimarySupportRelationRejectsRedeployDrift() noexcept
{
    bfvr::BFSoldierPrimarySupportPoseCache cache;
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const rifle = reinterpret_cast<void*>(0x3000);
    auto first = YawRightAngle();
    first.values[3][0] = 0.45F;
    auto redeploy = PitchUpRightAngle();
    redeploy.values[3][0] = 0.57F;
    cache.Resolve(soldier, skeleton, rifle, 3, first, nullptr);
    cache.Resolve(soldier, skeleton, rifle, 3, redeploy, nullptr);
    if (!Expect(
            std::fabs(redeploy.values[3][0] - 0.45F) < 0.0001F &&
                std::fabs(redeploy.values[0][2] - 1.0F) < 0.0001F,
            "primary redeploy replaced its first known-good support relation"))
    {
        return false;
    }
    auto gadget = PitchUpRightAngle();
    gadget.values[3][0] = 0.12F;
    cache.Resolve(soldier, skeleton, rifle, 4, gadget, nullptr);
    if (!Expect(
            std::fabs(gadget.values[3][0] - 0.12F) < 0.0001F,
            "non-primary relation was overwritten by the primary cache"))
    {
        return false;
    }
    cache.Reset();
    auto nextLifetime = Translation(0.61F, 0.0F, 0.0F);
    cache.Resolve(soldier, skeleton, rifle, 3, nextLifetime, nullptr);
    return Expect(
        std::fabs(nextLifetime.values[3][0] - 0.61F) < 0.0001F,
        "primary cache reset retained the prior lifetime relation");
}

Matrix4 Relation(const std::array<float, 12>& values) noexcept
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

bfvr::BFSoldierOffHandWeaponFingerprint WeaponFingerprint(
    const float zoomFov,
    const float x,
    const float y,
    const float z) noexcept
{
    return {zoomFov, {x, y, z}};
}

bool CalibrationCapturesOnlyOneFreePrimaryPressEdge() noexcept
{
    bfvr::BFSoldierOffHandCalibration calibration;
    calibration.ConfigureForTesting(true);
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const rifle = reinterpret_cast<void*>(0x3000);
    bfvr::BFSoldierOffHandCalibrationInput input = {};
    input.soldier = soldier;
    input.skeleton = skeleton;
    input.activeItem = rifle;
    input.activeItemIndex = 3;
    input.leftGripTracked = true;
    input.controllerRightHandWorld = Translation(1.0F, 2.0F, 3.0F);
    input.inverseSoldierWorld = Translation(0.0F, 0.0F, 0.0F);
    input.nativeLeftHandFromRightHand = Translation(0.20F, 0.0F, 0.0F);
    input.freeLeftHandLocal = Translation(1.45F, 2.10F, 3.25F);
    calibration.UpdateCapture(input);
    input.leftStickDown = true;
    calibration.UpdateCapture(input);
    auto resolved = calibration.Resolve(
        soldier, skeleton, rifle, 3,
        input.nativeLeftHandFromRightHand);
    if (!Expect(
            std::fabs(resolved.values[3][0] - 0.45F) < 0.0001F &&
                std::fabs(resolved.values[3][1] - 0.10F) < 0.0001F &&
                std::fabs(resolved.values[3][2] - 0.25F) < 0.0001F,
            "calibration did not recover the placed left-from-right relation"))
    {
        return false;
    }
    input.freeLeftHandLocal = Translation(1.80F, 2.0F, 3.0F);
    calibration.UpdateCapture(input);
    resolved = calibration.Resolve(
        soldier, skeleton, rifle, 3,
        input.nativeLeftHandFromRightHand);
    if (!Expect(
            std::fabs(resolved.values[3][0] - 0.45F) < 0.0001F,
            "held left-stick button recaptured every frame"))
    {
        return false;
    }
    input.leftStickDown = false;
    calibration.UpdateCapture(input);
    auto desired = YawRightAngle();
    desired.values[3][0] = 0.80F;
    desired.values[3][1] = 0.20F;
    desired.values[3][2] = 0.30F;
    auto rotatedRight = PitchUpRightAngle();
    rotatedRight.values[3][0] = 1.0F;
    rotatedRight.values[3][1] = 2.0F;
    rotatedRight.values[3][2] = 3.0F;
    input.controllerRightHandWorld = rotatedRight;
    input.freeLeftHandLocal =
        bfvr::native_arm_math::Multiply(desired, rotatedRight);
    input.leftStickDown = true;
    calibration.UpdateCapture(input);
    resolved = calibration.Resolve(
        soldier, skeleton, rifle, 3,
        input.nativeLeftHandFromRightHand);
    return Expect(
        std::fabs(resolved.values[3][0] - 0.80F) < 0.0001F &&
            std::fabs(resolved.values[0][2] - 1.0F) < 0.0001F,
        "second left-stick press did not preserve the full calibrated pose");
}

bool CalibrationRejectsHeldSupportAndOtherItems() noexcept
{
    bfvr::BFSoldierOffHandCalibration calibration;
    calibration.ConfigureForTesting(true);
    auto* const soldier = reinterpret_cast<void*>(0x1000);
    auto* const skeleton = reinterpret_cast<void*>(0x2000);
    auto* const rifle = reinterpret_cast<void*>(0x3000);
    auto* const sidearm = reinterpret_cast<void*>(0x4000);
    const auto native = Translation(0.20F, 0.0F, 0.0F);
    bfvr::BFSoldierOffHandCalibrationInput input = {};
    input.soldier = soldier;
    input.skeleton = skeleton;
    input.activeItem = rifle;
    input.activeItemIndex = 3;
    input.leftStickDown = true;
    input.leftGripTracked = true;
    input.supportHeld = true;
    input.freeLeftHandLocal = Translation(1.50F, 2.0F, 3.0F);
    input.controllerRightHandWorld = Translation(1.0F, 2.0F, 3.0F);
    input.inverseSoldierWorld = Translation(0.0F, 0.0F, 0.0F);
    input.nativeLeftHandFromRightHand = native;
    calibration.UpdateCapture(input);
    auto resolved = calibration.Resolve(soldier, skeleton, rifle, 3, native);
    if (!Expect(
            std::fabs(resolved.values[3][0] - 0.20F) < 0.0001F,
            "held support was accepted as a free-hand calibration"))
    {
        return false;
    }
    const auto sidearmResolved = calibration.Resolve(
        soldier, skeleton, sidearm, 2, native);
    if (!Expect(
            std::fabs(sidearmResolved.values[3][0] - 0.20F) < 0.0001F,
            "calibration leaked into a different item or slot"))
    {
        return false;
    }
    calibration.Reset();
    resolved = calibration.Resolve(soldier, skeleton, rifle, 3, native);
    return Expect(
        std::fabs(resolved.values[3][0] - 0.20F) < 0.0001F,
        "calibration survived a full experiment reset");
}

bool CalibrationDirectlyFlushesItsDedicatedAudit() noexcept
{
    std::array<wchar_t, MAX_PATH> temporaryDirectory = {};
    const DWORD directoryLength = GetTempPathW(
        static_cast<DWORD>(temporaryDirectory.size()),
        temporaryDirectory.data());
    if (directoryLength == 0 ||
        directoryLength >= temporaryDirectory.size())
    {
        return Expect(false, "could not resolve calibration test directory");
    }
    std::array<wchar_t, MAX_PATH> auditPath = {};
    if (_snwprintf_s(
            auditPath.data(), auditPath.size(), _TRUNCATE,
            L"%lsBFVROffHandCalibrationTests-%lu.log",
            temporaryDirectory.data(), GetCurrentProcessId()) < 0)
    {
        return Expect(false, "could not format calibration test path");
    }
    DeleteFileW(auditPath.data());

    bfvr::BFSoldierOffHandCalibration calibration;
    calibration.ConfigureForTesting(true, auditPath.data());
    bfvr::BFSoldierOffHandCalibrationInput input = {};
    input.soldier = reinterpret_cast<void*>(0x1000);
    input.skeleton = reinterpret_cast<void*>(0x2000);
    input.activeItem = reinterpret_cast<void*>(0x3000);
    input.activeItemIndex = 3;
    input.leftStickDown = true;
    input.leftGripTracked = true;
    input.freeLeftHandLocal = Translation(0.45F, 0.10F, 0.25F);
    input.controllerRightHandWorld = Translation(0.0F, 0.0F, 0.0F);
    input.inverseSoldierWorld = Translation(0.0F, 0.0F, 0.0F);
    input.nativeLeftHandFromRightHand = Translation(0.20F, 0.0F, 0.0F);
    calibration.UpdateCapture(input);

    HANDLE file = CreateFileW(
        auditPath.data(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    bool containsCapture = false;
    if (file != INVALID_HANDLE_VALUE)
    {
        const DWORD bytes = GetFileSize(file, nullptr);
        if (bytes != INVALID_FILE_SIZE && bytes >= sizeof(wchar_t))
        {
            std::vector<wchar_t> contents(
                bytes / sizeof(wchar_t) + 1U, L'\0');
            DWORD bytesRead = 0;
            if (ReadFile(file, contents.data(), bytes, &bytesRead, nullptr) &&
                bytesRead == bytes)
            {
                containsCapture = std::wstring_view(contents.data()).find(
                    L"OFFHAND_CALIBRATION_CAPTURE sequence=1") !=
                    std::wstring_view::npos;
            }
        }
        CloseHandle(file);
    }
    DeleteFileW(auditPath.data());
    return Expect(
        containsCapture,
        "dedicated calibration audit was not synchronously written");
}

bool AcceptedOverridesRequireExactTemplatesAndNativeFingerprint() noexcept
{
    auto dpNative = Relation({
        0.2986422F, -0.8820737F, 0.3643606F,
        -0.7125188F, -0.4600687F, -0.5297675F,
        0.6349251F, -0.1014027F, -0.7658902F,
        0.2204590F, -0.0816650F, 0.0508423F});
    const auto dp = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.6F, -0.03F, -0.02F, -0.07F), 3, dpNative);
    if (!Expect(
            dp.has_value() &&
                std::fabs(dp->values[3][0] - 0.2825449F) < 0.000001F &&
                std::fabs(dp->values[3][2] + 0.0982085F) < 0.000001F,
            "accepted Russian DP calibration was not recovered"))
    {
        return false;
    }

    auto mp18Native = Relation({
        0.3693111F, -0.3846701F, -0.8459539F,
        0.6523928F, -0.5409626F, 0.5307947F,
        -0.6618103F, -0.7479226F, 0.0511724F,
        0.2846680F, 0.1800537F, -0.0527344F});
    const auto russianMp18 = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.6F, -0.01F, -0.04F, 0.09F), 3, mp18Native);
    mp18Native = Relation({
        0.3690058F, -0.3842715F, -0.8462682F,
        0.6524482F, -0.5413660F, 0.5303151F,
        -0.6619259F, -0.7478356F, 0.0509501F,
        0.2846756F, 0.1801758F, -0.0525818F});
    const auto japaneseMp18 = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.6F, -0.01F, -0.04F, 0.09F), 3, mp18Native);
    if (!Expect(
            russianMp18.has_value() && japaneseMp18.has_value() &&
                std::fabs(
                    russianMp18->values[3][0] - 0.3174495F) < 0.000001F &&
                std::fabs(
                    japaneseMp18->values[3][0] - 0.3151374F) < 0.000001F,
            "shared MP18 template did not retain faction-specific captures"))
    {
        return false;
    }

    const auto type5Native = Relation({
        0.7896664F, -0.5337967F, -0.3024703F,
        -0.5998216F, -0.7753332F, -0.1976677F,
        -0.1290008F, 0.3375196F, -0.9324376F,
        0.2968140F, -0.1567383F, 0.0127144F});
    const auto type5 = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.6F, -0.02F, -0.03F, 0.01F), 3, type5Native);
    const auto saiga = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.9F, 0.025F, 0.0F, 0.06F), 3, type5Native);
    if (!Expect(
            type5.has_value() && saiga.has_value() &&
                std::fabs(type5->values[3][2] + 0.1433075F) < 0.000001F &&
                std::fabs(saiga->values[3][2] + 0.1009266F) < 0.000001F,
            "engineer weapon calibrations were not independently recovered"))
    {
        return false;
    }

    const auto chinaAk = bfvr::ResolveBFSoldierOffHandOverride(
        WeaponFingerprint(0.5F, 0.025F, 0.0F, 0.06F), 3,
        Relation({
            0.3668162F, -0.3814111F, -0.8485113F,
            0.6528491F, -0.5442396F, 0.5268694F,
            -0.6627473F, -0.7472142F, 0.0493681F,
            0.2843170F, 0.1806641F, -0.0510788F}));
    if (!Expect(
            chinaAk.has_value() &&
                std::fabs(chinaAk->values[3][2] + 0.1808556F) < 0.000001F,
            "accepted Chinese AK47 calibration was not recovered"))
    {
        return false;
    }

    dpNative.values[0][0] += 0.010F;
    if (!Expect(
            bfvr::ResolveBFSoldierOffHandOverride(
                WeaponFingerprint(0.6F, -0.03F, -0.02F, -0.07F),
                3, dpNative).has_value(),
            "bounded native-pose noise rejected an exact template"))
    {
        return false;
    }
    dpNative.values[0][0] += 0.020F;
    return Expect(
               !bfvr::ResolveBFSoldierOffHandOverride(
                    WeaponFingerprint(0.6F, -0.03F, -0.02F, -0.07F),
                    3, dpNative).has_value(),
               "materially changed mod pose matched the stock fingerprint") &&
        Expect(
            !bfvr::ResolveBFSoldierOffHandOverride(
                 WeaponFingerprint(0.6F, 0.25F, -0.02F, -0.07F),
                 3, Relation({
                     0.2986422F, -0.8820737F, 0.3643606F,
                     -0.7125188F, -0.4600687F, -0.5297675F,
                     0.6349251F, -0.1014027F, -0.7658902F,
                     0.2204590F, -0.0816650F, 0.0508423F})).has_value(),
            "changed mod weapon properties inherited an accepted override") &&
        Expect(
            !bfvr::ResolveBFSoldierOffHandOverride(
                 WeaponFingerprint(0.6F, -0.03F, -0.02F, -0.07F),
                 2, Relation({
                     0.2986422F, -0.8820737F, 0.3643606F,
                     -0.7125188F, -0.4600687F, -0.5297675F,
                     0.6349251F, -0.1014027F, -0.7658902F,
                     0.2204590F, -0.0816650F, 0.0508423F})).has_value(),
            "accepted primary override leaked into another item slot");
}

bool RuntimeOverrideReadsNativeWeaponTemplateProperties() noexcept
{
    std::array<wchar_t, MAX_PATH> temporaryDirectory = {};
    const DWORD directoryLength = GetTempPathW(
        static_cast<DWORD>(temporaryDirectory.size()),
        temporaryDirectory.data());
    if (directoryLength == 0 ||
        directoryLength >= temporaryDirectory.size())
    {
        return Expect(false, "could not resolve override test directory");
    }
    std::array<wchar_t, MAX_PATH> auditPath = {};
    if (_snwprintf_s(
            auditPath.data(), auditPath.size(), _TRUNCATE,
            L"%lsBFVROffHandOverrideTests-%lu.log",
            temporaryDirectory.data(), GetCurrentProcessId()) < 0)
    {
        return Expect(false, "could not format override test path");
    }
    DeleteFileW(auditPath.data());

    alignas(void*) std::array<std::byte, 0x60> item = {};
    alignas(float) std::array<std::byte, 0x400> weaponTemplate = {};
    const std::byte* weaponTemplatePointer = weaponTemplate.data();
    std::memcpy(
        item.data() + 0x4C,
        &weaponTemplatePointer,
        sizeof(weaponTemplatePointer));
    const float zoomFov = 0.6F;
    const std::array<float, 3> soldierCameraPosition = {
        -0.03F, -0.02F, -0.07F};
    std::memcpy(
        weaponTemplate.data() + 0x3DC, &zoomFov, sizeof(zoomFov));
    std::memcpy(
        weaponTemplate.data() + 0x3F0,
        soldierCameraPosition.data(),
        sizeof(soldierCameraPosition));

    const auto native = Relation({
        0.2986422F, -0.8820737F, 0.3643606F,
        -0.7125188F, -0.4600687F, -0.5297675F,
        0.6349251F, -0.1014027F, -0.7658902F,
        0.2204590F, -0.0816650F, 0.0508423F});
    bfvr::BFSoldierOffHandCalibration calibration;
    calibration.ConfigureForTesting(false, auditPath.data());
    const auto resolved = calibration.Resolve(
        nullptr, nullptr, item.data(), 3, native);

    HANDLE file = CreateFileW(
        auditPath.data(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    bool loggedApplication = false;
    bool loggedProbe = false;
    if (file != INVALID_HANDLE_VALUE)
    {
        const DWORD bytes = GetFileSize(file, nullptr);
        if (bytes != INVALID_FILE_SIZE && bytes >= sizeof(wchar_t))
        {
            std::vector<wchar_t> contents(
                bytes / sizeof(wchar_t) + 1U, L'\0');
            DWORD bytesRead = 0;
            if (ReadFile(file, contents.data(), bytes, &bytesRead, nullptr) &&
                bytesRead == bytes)
            {
                loggedApplication = std::wstring_view(contents.data()).find(
                    L"OFFHAND_CALIBRATION_OVERRIDE_APPLIED") !=
                    std::wstring_view::npos;
                loggedProbe = std::wstring_view(contents.data()).find(
                    L"OFFHAND_CALIBRATION_OVERRIDE_PROBE") !=
                    std::wstring_view::npos;
            }
        }
        CloseHandle(file);
    }
    DeleteFileW(auditPath.data());
    return Expect(
               std::fabs(resolved.values[3][0] - 0.2825449F) < 0.000001F,
               "runtime weapon-template properties did not select DP") &&
        Expect(
            loggedApplication,
            "runtime override application was not synchronously audited") &&
        Expect(
            loggedProbe,
            "runtime override probe was not synchronously audited");
}

bool ReconstructsAuthoredVisualSocket() noexcept
{
    const auto leftFromRightHand =
        Translation(0.40F, 0.0F, 0.0F);
    Matrix4 controllerRightHandWorld = {};
    // Row-vector +90-degree Z rotation: the authored +X socket must become
    // +Y under the live solved right-hand basis.
    controllerRightHandWorld.values[0][1] = 1.0F;
    controllerRightHandWorld.values[1][0] = -1.0F;
    controllerRightHandWorld.values[2][2] = 1.0F;
    controllerRightHandWorld.values[3][0] = 2.0F;
    controllerRightHandWorld.values[3][1] = 1.0F;
    controllerRightHandWorld.values[3][2] = -3.0F;
    controllerRightHandWorld.values[3][3] = 1.0F;
    const auto inverseSoldier =
        Translation(-2.0F, -1.0F, 3.0F);
    const auto controllerLeft =
        Translation(0.10F, 0.40F, 0.0F);
    const auto result =
        bfvr::stereo::ComputeOffHandAuthoredSupportPose(
            leftFromRightHand,
            controllerRightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    return Expect(
               result.has_value(),
               "valid authored visual socket was rejected") &&
        Expect(
            std::fabs(
                result->targetLocal.values[3][1] -
                0.40F) < 0.0001F,
            "authored support target used the wrong row-vector order") &&
        Expect(
            std::fabs(
                result->targetLocal.values[0][1] -
                1.0F) < 0.0001F,
            "authored support orientation did not follow the gun basis") &&
        Expect(
            std::fabs(
                result->controllerDistanceMetres -
                0.10F) < 0.0001F,
            "controller-to-authored-socket distance is incorrect");
}

bool CapturedClosePoseIsNoJumpAndFollowsRightHand() noexcept
{
    auto rightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    const auto inverseSoldier =
        Translation(-2.0F, -1.0F, 3.0F);
    const auto controllerLeft =
        Translation(-0.08F, -0.03F, 0.02F);
    const auto candidate =
        bfvr::stereo::ComputeOffHandCloseSupportCandidate(
            rightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    const auto relation =
        bfvr::stereo::CaptureOffHandCloseRelation(
            rightHandWorld,
            inverseSoldier,
            controllerLeft);
    const auto atCapture =
        relation.has_value()
        ? bfvr::stereo::
              ComputeOffHandCapturedCloseSupportPose(
                  *relation,
                  rightHandWorld,
                  inverseSoldier,
                  controllerLeft,
                  1.0F)
        : std::nullopt;
    if (!Expect(
            candidate.has_value() &&
                relation.has_value() &&
                atCapture.has_value(),
            "valid close visual cup could not be captured") ||
        !Expect(
            candidate->controllerDistanceMetres > 0.08F &&
                candidate->controllerDistanceMetres < 0.10F,
            "close candidate did not measure hand separation") ||
        !Expect(
            std::fabs(
                atCapture->targetLocal.values[3][0] -
                controllerLeft.values[3][0]) < 0.0001F &&
                std::fabs(
                    atCapture->targetLocal.values[3][1] -
                    controllerLeft.values[3][1]) < 0.0001F &&
                std::fabs(
                    atCapture->targetLocal.values[3][2] -
                    controllerLeft.values[3][2]) < 0.0001F,
            "close visual cup jumped when captured"))
    {
        return false;
    }

    rightHandWorld.values[3][0] += 0.50F;
    const auto followed =
        bfvr::stereo::ComputeOffHandCapturedCloseSupportPose(
            *relation,
            rightHandWorld,
            inverseSoldier,
            controllerLeft,
            1.0F);
    return Expect(
        followed.has_value() &&
            std::fabs(
                followed->targetLocal.values[3][0] -
                (controllerLeft.values[3][0] + 0.50F)) <
                0.0001F,
        "captured close cup did not follow the solved right hand");
}

bool CloseBindingCapturesAndIgnoresLeftNoise() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 7;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    input.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal =
        Translation(-0.08F, -0.03F, 0.02F);
    if (!Expect(
            binding.Update(input).state ==
                OffHandSupportState::Candidate,
            "close binding did not enter Candidate"))
    {
        return false;
    }

    input.timeSeconds = 1.05;
    const auto acquired = binding.Update(input);
    if (!Expect(
            acquired.supported &&
                acquired.enteredSupport,
            "close binding did not capture support") ||
        !Expect(
            std::fabs(
                acquired.targetLocal.values[3][0] -
                input.controllerLeftHandLocal.values[3][0]) <
                0.0001F,
            "close binding jumped on acquisition"))
    {
        return false;
    }
    bfvr::BFSoldierOffHandSteeringInput steeringInput = {};
    steeringInput.bindingId = input.bindingId;
    steeringInput.squeezeValue = input.squeezeValue;
    steeringInput.sessionFocused = true;
    steeringInput.leftGripTracked = true;
    steeringInput.leftSqueezeActive = true;
    steeringInput.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    steeringInput.controllerGunWorld =
        input.controllerRightHandWorld;
    steeringInput.predictedSupportWorld =
        Translation(2.0F, 1.0F, -2.6F);
    steeringInput.trackedLeftHandWorld =
        Translation(2.1F, 1.0F, -2.6F);
    steeringInput.maximumSwingRadians = 0.60F;
    bfvr::stereo::OffHandWeaponSteeringResult steering = {};
    if (!Expect(
            !binding.TryComputeSupportedWeaponSteering(
                steeringInput,
                steering),
            "captured-close pistol support was allowed to steer"))
    {
        return false;
    }

    input.timeSeconds = 1.06;
    input.controllerRightHandWorld.values[3][0] += 0.10F;
    input.controllerLeftHandLocal.values[3][0] += 0.15F;
    const auto followed = binding.Update(input);
    if (!Expect(
            followed.supported &&
                std::fabs(
                    followed.targetLocal.values[3][0] -
                    0.02F) < 0.0001F,
            "close binding did not follow only the right-hand delta"))
    {
        return false;
    }

    input.timeSeconds = 1.07;
    input.leftSqueezeActive = false;
    input.squeezeValue = 0.0F;
    const auto released = binding.Update(input);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "close binding did not release on squeeze-up");
}

bool AuthoredBindingAllowsOnlyCurrentSupportedSteering() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 11;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    input.leftHandFromRightHand =
        Translation(0.0F, 0.0F, 0.40F);
    input.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal =
        Translation(0.0F, 0.0F, 0.35F);
    static_cast<void>(binding.Update(input));
    input.timeSeconds = 1.05;
    const auto acquired = binding.Update(input);
    if (!Expect(
            acquired.supported,
            "authored binding did not acquire for steering"))
    {
        return false;
    }

    bfvr::BFSoldierOffHandSteeringInput steeringInput = {};
    steeringInput.bindingId = input.bindingId;
    steeringInput.squeezeValue = input.squeezeValue;
    steeringInput.sessionFocused = true;
    steeringInput.leftGripTracked = true;
    steeringInput.leftSqueezeActive = true;
    steeringInput.mode = input.mode;
    steeringInput.controllerGunWorld =
        input.controllerRightHandWorld;
    steeringInput.predictedSupportWorld =
        Translation(2.0F, 1.0F, -2.6F);
    steeringInput.trackedLeftHandWorld =
        Translation(2.1F, 1.0F, -2.6F);
    steeringInput.maximumSwingRadians = 0.60F;
    bfvr::stereo::OffHandWeaponSteeringResult steering = {};
    if (!Expect(
            binding.TryComputeSupportedWeaponSteering(
                steeringInput,
                steering),
            "supported authored span could not steer"))
    {
        return false;
    }

    steeringInput.leftSqueezeActive = false;
    return Expect(
        !binding.TryComputeSupportedWeaponSteering(
            steeringInput,
            steering),
        "current squeeze-up retained stale steering");
}

bool ToggleGripReleasesOnlyOnNextPress() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput input = {};
    input.bindingId = 13;
    input.timeSeconds = 1.0;
    input.squeezeValue = 1.0F;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.toggleGripStyle = true;
    input.mode = bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    input.leftHandFromRightHand = Translation(0.0F, 0.0F, 0.40F);
    input.controllerRightHandWorld = Translation(2.0F, 1.0F, -3.0F);
    input.inverseSoldierWorld = Translation(-2.0F, -1.0F, 3.0F);
    input.controllerLeftHandLocal = Translation(0.0F, 0.0F, 0.35F);
    static_cast<void>(binding.Update(input));
    input.timeSeconds = 1.05;
    if (!Expect(
            binding.Update(input).supported,
            "toggle grip did not acquire support"))
    {
        return false;
    }
    input.timeSeconds = 1.06;
    input.leftSqueezeActive = false;
    input.squeezeValue = 0.0F;
    if (!Expect(
            binding.Update(input).supported,
            "toggle grip released on physical squeeze-up"))
    {
        return false;
    }
    input.timeSeconds = 1.07;
    input.leftSqueezeActive = true;
    input.squeezeValue = 1.0F;
    const auto released = binding.Update(input);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "toggle grip did not release on the next press");
}

bool SoldierSteeringFrameUsesTrackedGripAndRejectsPistol() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding binding;
    bfvr::BFSoldierOffHandSupportInput support = {};
    support.bindingId = 19;
    support.timeSeconds = 1.0;
    support.squeezeValue = 1.0F;
    support.sessionFocused = true;
    support.leftGripTracked = true;
    support.leftSqueezeActive = true;
    support.mode =
        bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    support.leftHandFromRightHand =
        Translation(0.0F, 0.0F, 0.40F);
    support.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    support.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    support.controllerLeftHandLocal =
        Translation(0.05F, 0.0F, 0.40F);
    static_cast<void>(binding.Update(support));
    support.timeSeconds = 1.05;
    if (!binding.Update(support).supported)
    {
        return Expect(
            false,
            "authored frame binding did not acquire");
    }

    bfvr::BFSoldierOffHandWeaponSteeringInput input = {};
    input.bindingId = support.bindingId;
    input.sessionFocused = true;
    input.leftGripTracked = true;
    input.leftSqueezeActive = true;
    input.mode = support.mode;
    input.leftHand.squeezeValue = 1.0F;
    input.leftHand.gripPose.orientationW = 1.0F;
    input.leftHand.gripPose.positionX = 2.10F;
    input.leftHand.gripPose.positionY = 1.0F;
    input.leftHand.gripPose.positionZ = 2.60F;
    input.soldierWorld = Translation(0.0F, 0.0F, 0.0F);
    input.controllerGunWorld =
        support.controllerRightHandWorld;
    input.controllerRightHandWorld =
        support.controllerRightHandWorld;
    input.leftHandFromRightHand =
        support.leftHandFromRightHand;
    input.maximumSwingRadians = 0.60F;
    const auto rifle =
        bfvr::TryComputeBFSoldierOffHandWeaponSteering(
            binding,
            input);
    if (!Expect(
            rifle.has_value() &&
                std::fabs(
                    rifle->gunWorld.values[3][0] - 2.0F) <
                    0.0001F,
            "soldier steering frame did not preserve its pivot"))
    {
        return false;
    }
    input.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    return Expect(
        !bfvr::TryComputeBFSoldierOffHandWeaponSteering(
             binding,
             input).has_value(),
        "soldier steering frame accepted pistol mode");
}

bool SteeringUsesFixedPivotAndFullDirectionalSwing() noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    const float requestedSwing = 0.5F * kPi;
    const auto gun = Translation(2.0F, 1.0F, -3.0F);
    const auto predicted =
        Translation(2.0F, 1.0F, -2.6F);
    const auto tracked =
        Translation(2.4F, 1.0F, -3.0F);
    const auto result =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            tracked,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    if (!Expect(
            result.has_value(),
            "valid long support steering was rejected"))
    {
        return false;
    }

    const float sine = std::sin(requestedSwing);
    const float cosine = std::cos(requestedSwing);
    return Expect(
               std::fabs(
                   result->appliedSwingRadians -
                    requestedSwing) < 0.0001F,
               "full off-hand swing retained the obsolete 35-degree cap") &&
        Expect(
            std::fabs(result->gunWorld.values[3][0] - 2.0F) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[3][1] - 1.0F) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[3][2] + 3.0F) <
                    0.0001F,
            "off-hand steering moved the right-grip pivot") &&
        Expect(
            std::fabs(result->gunWorld.values[2][0] - sine) <
                    0.0001F &&
                std::fabs(result->gunWorld.values[2][2] - cosine) <
                    0.0001F,
            "full-range row-vector swing used the wrong direction") &&
        Expect(
            std::fabs(
                result->gunWorld.values[2][0] *
                        result->gunWorld.values[2][0] +
                    result->gunWorld.values[2][1] *
                        result->gunWorld.values[2][1] +
                    result->gunWorld.values[2][2] *
                        result->gunWorld.values[2][2] -
                    1.0F) < 0.0001F,
            "off-hand steering introduced weapon scale");
}

bool SteeringIgnoresRadialMismatchAndHandlesOppositeDirection() noexcept
{
    constexpr float kPi = 3.14159265358979323846F;
    const float requested = 10.0F * kPi / 180.0F;
    const auto gun = Translation(2.0F, 1.0F, -3.0F);
    const auto predicted =
        Translation(2.0F, 1.0F, -2.6F);
    const auto tracked = Translation(
        2.0F + 1.5F * std::sin(requested),
        1.0F,
        -3.0F + 1.5F * std::cos(requested));
    const auto result =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            tracked,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    if (!Expect(
            result.has_value() &&
                std::fabs(
                    result->appliedSwingRadians -
                    requested) < 0.0001F,
            "radial mismatch altered a valid support direction"))
    {
        return false;
    }

    const auto collapsed =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            gun,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    const auto opposite = Translation(2.0F, 1.0F, -3.4F);
    const auto oppositeResult =
        bfvr::stereo::ComputeBoundedOffHandWeaponSteering(
            gun,
            predicted,
            opposite,
            bfvr::stereo::kUnrestrictedOffHandWeaponSwingRadians,
            1.0F);
    return Expect(
               !collapsed.has_value(),
               "collapsed tracked support span was accepted") &&
        Expect(
            oppositeResult.has_value() &&
                std::fabs(oppositeResult->appliedSwingRadians - kPi) <
                    0.0001F &&
                std::fabs(oppositeResult->gunWorld.values[2][2] + 1.0F) <
                    0.0001F,
            "opposite support direction did not use a deterministic full swing");
}

bool AcquireByProximityAndRetainUntilExplicitRelease() noexcept
{
    OffHandSupportPolicy policy;
    if (!Expect(
            policy.Update(ValidSample(1.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "near authored socket did not enter Candidate"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.03, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "Candidate acquired before the hold interval"))
    {
        return false;
    }
    const auto acquired = policy.Update(ValidSample(1.05, 0.10F));
    if (!Expect(
            acquired.state == OffHandSupportState::Supported &&
                acquired.enteredSupport,
            "stable near grip did not enter Supported"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.06, 5.0F)).state ==
                OffHandSupportState::Supported,
            "held support auto-detached after pulling away"))
    {
        return false;
    }
    auto releaseSample = ValidSample(1.07, 5.0F);
    releaseSample.leftGripHeld = false;
    const auto released = policy.Update(releaseSample);
    return Expect(
        released.state == OffHandSupportState::Free &&
            released.exitedSupport,
        "support did not release on explicit squeeze-up");
}

bool BindingUsesModeSpecificAcquireRadii() noexcept
{
    bfvr::BFSoldierOffHandSupportBinding authoredBinding;
    bfvr::BFSoldierOffHandSupportInput authored = {};
    authored.bindingId = 101;
    authored.timeSeconds = 1.0;
    authored.squeezeValue = 1.0F;
    authored.sessionFocused = true;
    authored.leftGripTracked = true;
    authored.leftSqueezeActive = true;
    authored.mode =
        bfvr::BFSoldierOffHandSupportMode::AuthoredHandSpan;
    authored.leftHandFromRightHand =
        Translation(0.0F, 0.0F, 0.40F);
    authored.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    authored.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    authored.controllerLeftHandLocal =
        Translation(0.0F, 0.0F, 0.23F);
    if (!Expect(
            authoredBinding.Update(authored).state ==
                OffHandSupportState::Candidate,
            "17 cm authored support did not enter the 18 cm gate"))
    {
        return false;
    }
    authored.timeSeconds = 1.05;
    if (!Expect(
            authoredBinding.Update(authored).supported,
            "17 cm authored support did not acquire"))
    {
        return false;
    }

    bfvr::BFSoldierOffHandSupportBinding closeBinding;
    bfvr::BFSoldierOffHandSupportInput close = {};
    close.bindingId = 102;
    close.timeSeconds = 2.0;
    close.squeezeValue = 1.0F;
    close.sessionFocused = true;
    close.leftGripTracked = true;
    close.leftSqueezeActive = true;
    close.mode =
        bfvr::BFSoldierOffHandSupportMode::CapturedClose;
    close.controllerRightHandWorld =
        Translation(2.0F, 1.0F, -3.0F);
    close.inverseSoldierWorld =
        Translation(-2.0F, -1.0F, 3.0F);
    close.controllerLeftHandLocal =
        Translation(0.13F, 0.0F, 0.0F);
    if (!Expect(
            closeBinding.Update(close).state ==
                OffHandSupportState::Free,
            "13 cm close support entered the retained 12 cm gate"))
    {
        return false;
    }
    close.controllerLeftHandLocal =
        Translation(0.11F, 0.0F, 0.0F);
    if (!Expect(
            closeBinding.Update(close).state ==
                OffHandSupportState::Candidate,
            "11 cm close support did not enter the retained 12 cm gate"))
    {
        return false;
    }
    close.timeSeconds = 2.05;
    return Expect(
        closeBinding.Update(close).supported,
        "11 cm close support did not acquire");
}

bool RejectsUnsafeInputs() noexcept
{
    OffHandSupportPolicy policy;
    auto sample = ValidSample(1.0, 0.10F);
    sample.leftGripHeld = false;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "unheld grip became a support candidate"))
    {
        return false;
    }

    sample = ValidSample(1.1, 0.10F);
    sample.leftGripTracked = false;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "untracked grip became a support candidate"))
    {
        return false;
    }

    sample = ValidSample(1.2, 0.10F);
    sample.nativeLeftHandTargetActive = true;
    if (!Expect(
            policy.Update(sample).state == OffHandSupportState::Free,
            "native left-hand target ownership was ignored"))
    {
        return false;
    }

    sample = ValidSample(
        1.3,
        std::numeric_limits<float>::quiet_NaN());
    return Expect(
        policy.Update(sample).state == OffHandSupportState::Free,
        "non-finite support distance was accepted");
}

bool ReleasesOnTrackingOrBindingChange() noexcept
{
    OffHandSupportPolicy policy;
    static_cast<void>(policy.Update(ValidSample(1.0, 0.10F)));
    static_cast<void>(policy.Update(ValidSample(1.1, 0.10F)));
    auto sample = ValidSample(1.2, 0.10F);
    sample.sessionFocused = false;
    const auto focusLost = policy.Update(sample);
    if (!Expect(
            focusLost.state == OffHandSupportState::Free &&
                focusLost.exitedSupport,
            "focus loss did not release support"))
    {
        return false;
    }

    static_cast<void>(policy.Update(ValidSample(2.0, 0.10F, 1)));
    static_cast<void>(policy.Update(ValidSample(2.1, 0.10F, 1)));
    const auto changed = policy.Update(ValidSample(2.2, 0.10F, 2));
    return Expect(
        changed.state == OffHandSupportState::Candidate &&
            changed.exitedSupport,
        "item/soldier binding change retained old support");
}

bool RequiresContinuousAcquisition() noexcept
{
    OffHandSupportPolicy policy;
    static_cast<void>(policy.Update(ValidSample(1.0, 0.10F)));
    if (!Expect(
            policy.Update(ValidSample(1.02, 0.13F)).state ==
                OffHandSupportState::Free,
            "Candidate persisted outside the acquire radius"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(2.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "second acquisition did not enter Candidate"))
    {
        return false;
    }
    if (!Expect(
            policy.Update(ValidSample(1.0, 0.10F)).state ==
                OffHandSupportState::Candidate,
            "backward time did not safely restart Candidate"))
    {
        return false;
    }
    return Expect(
        policy.Update(ValidSample(1.03, 0.10F)).state ==
            OffHandSupportState::Candidate,
        "restarted Candidate reused elapsed time from the prior clock");
}

} // namespace

int main()
{
    const bool passed =
        ReconstructsAuthoredVisualSocket() &&
        LearnedLeftWristReferenceSurvivesItemAndTrackingChanges() &&
        PrimarySupportRelationRejectsRedeployDrift() &&
        CalibrationCapturesOnlyOneFreePrimaryPressEdge() &&
        CalibrationRejectsHeldSupportAndOtherItems() &&
        CalibrationDirectlyFlushesItsDedicatedAudit() &&
        AcceptedOverridesRequireExactTemplatesAndNativeFingerprint() &&
        RuntimeOverrideReadsNativeWeaponTemplateProperties() &&
        CapturedClosePoseIsNoJumpAndFollowsRightHand() &&
        CloseBindingCapturesAndIgnoresLeftNoise() &&
        AuthoredBindingAllowsOnlyCurrentSupportedSteering() &&
        ToggleGripReleasesOnlyOnNextPress() &&
        SoldierSteeringFrameUsesTrackedGripAndRejectsPistol() &&
        SteeringUsesFixedPivotAndFullDirectionalSwing() &&
        SteeringIgnoresRadialMismatchAndHandlesOppositeDirection() &&
        AcquireByProximityAndRetainUntilExplicitRelease() &&
        BindingUsesModeSpecificAcquireRadii() &&
        RejectsUnsafeInputs() &&
        ReleasesOnTrackingOrBindingChange() &&
        RequiresContinuousAcquisition();
    if (passed)
    {
        std::puts("Off-hand support policy tests passed.");
        return 0;
    }
    return 1;
}
