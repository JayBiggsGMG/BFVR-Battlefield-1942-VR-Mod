#include "client/BFSoldierOffHandCalibration.h"

#include "client/BFSoldierNativeArmMath.h"
#include "client/BFSoldierOffHandOverrides.h"

#include <array>
#include <cmath>
#include <cwchar>

namespace
{

constexpr wchar_t kCalibrationEnvironment[] =
    L"BFVR_OFFHAND_CALIBRATION";
constexpr wchar_t kCalibrationLogEnvironment[] =
    L"BFVR_OFFHAND_CALIBRATION_LOG";
constexpr std::size_t kObjectInstanceTemplateOffset = 0x4C;
constexpr std::size_t kFireArmsTemplateZoomFovOffset = 0x3DC;
constexpr std::size_t kFireArmsTemplateSoldierCameraPositionOffset = 0x3F0;

using Matrix4 = bfvr::stereo::Matrix4;
using bfvr::native_arm_math::Invert;
using bfvr::native_arm_math::IsFinite;
using bfvr::native_arm_math::Multiply;

bool ReadWeaponFingerprint(
    const void* object,
    bfvr::BFSoldierOffHandWeaponFingerprint& fingerprint,
    const void*& objectTemplateAddress) noexcept
{
    fingerprint = {};
    objectTemplateAddress = nullptr;
    if (object == nullptr)
    {
        return false;
    }
    __try
    {
        const auto* const objectBytes = static_cast<const std::byte*>(object);
        const auto* const objectTemplate =
            *reinterpret_cast<const std::byte* const*>(
                objectBytes + kObjectInstanceTemplateOffset);
        objectTemplateAddress = objectTemplate;
        if (objectTemplate == nullptr)
        {
            return false;
        }
        fingerprint.zoomFov = *reinterpret_cast<const float*>(
            objectTemplate + kFireArmsTemplateZoomFovOffset);
        for (std::size_t index = 0;
             index < fingerprint.soldierCameraPosition.size();
             ++index)
        {
            fingerprint.soldierCameraPosition[index] =
                *reinterpret_cast<const float*>(
                    objectTemplate +
                    kFireArmsTemplateSoldierCameraPositionOffset +
                    index * sizeof(float));
        }
        return std::isfinite(fingerprint.zoomFov) &&
            std::isfinite(fingerprint.soldierCameraPosition[0]) &&
            std::isfinite(fingerprint.soldierCameraPosition[1]) &&
            std::isfinite(fingerprint.soldierCameraPosition[2]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        fingerprint = {};
        return false;
    }
}

void LogRejectedCapture(
    void (*appendLog)(const wchar_t* message),
    const wchar_t* reason,
    const void* activeItem,
    const LONG activeItemIndex) noexcept
{
    if (appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 512> message = {};
    _snwprintf_s(
        message.data(), message.size(), _TRUNCATE,
        L"OFFHAND_CALIBRATION_REJECT left-stick capture: "
        L"reason=%ls item=%p activeItemIndex=%ld. Release left support, keep "
        L"both grips tracked, and equip a primary slot-3 weapon.",
        reason, activeItem, activeItemIndex);
    appendLog(message.data());
}

void AppendCalibrationAuditFile(
    const wchar_t* auditPath,
    const wchar_t* message) noexcept
{
    if (auditPath == nullptr || auditPath[0] == L'\0' || message == nullptr)
    {
        return;
    }
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    std::array<wchar_t, 2304> line = {};
    if (_snwprintf_s(
            line.data(), line.size(), _TRUNCATE,
            L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid:%lu] %ls\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, GetCurrentProcessId(),
            message) < 0)
    {
        return;
    }
    HANDLE file = CreateFileW(
        auditPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const DWORD byteCount = static_cast<DWORD>(
        wcslen(line.data()) * sizeof(wchar_t));
    DWORD written = 0;
    const BOOL writeSucceeded = WriteFile(
        file, line.data(), byteCount, &written, nullptr);
    if (writeSucceeded && written == byteCount)
    {
        FlushFileBuffers(file);
    }
    CloseHandle(file);
}

void LogCapturedRelation(
    void (*appendLog)(const wchar_t* message),
    const wchar_t* auditPath,
    const LONG sequence,
    const void* soldier,
    const void* skeleton,
    const void* activeItem,
    const Matrix4& nativeRelation,
    const Matrix4& calibratedRelation) noexcept
{
    std::array<wchar_t, 2048> message = {};
    _snwprintf_s(
        message.data(), message.size(), _TRUNCATE,
        L"OFFHAND_CALIBRATION_CAPTURE sequence=%ld soldier=%p skeleton=%p "
        L"item=%p activeItemIndex=3 "
        L"native=[%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f] "
        L"calibrated=[%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f] "
        L"translationDelta=(%.7f,%.7f,%.7f). The calibrated relation is hot-applied "
        L"to acquisition, visual wrist placement, steering, and fire for this exact "
        L"live item only; no persistent weapon data was written.",
        sequence, soldier, skeleton, activeItem,
        nativeRelation.values[0][0], nativeRelation.values[0][1], nativeRelation.values[0][2],
        nativeRelation.values[1][0], nativeRelation.values[1][1], nativeRelation.values[1][2],
        nativeRelation.values[2][0], nativeRelation.values[2][1], nativeRelation.values[2][2],
        nativeRelation.values[3][0], nativeRelation.values[3][1], nativeRelation.values[3][2],
        calibratedRelation.values[0][0], calibratedRelation.values[0][1], calibratedRelation.values[0][2],
        calibratedRelation.values[1][0], calibratedRelation.values[1][1], calibratedRelation.values[1][2],
        calibratedRelation.values[2][0], calibratedRelation.values[2][1], calibratedRelation.values[2][2],
        calibratedRelation.values[3][0], calibratedRelation.values[3][1], calibratedRelation.values[3][2],
        calibratedRelation.values[3][0] - nativeRelation.values[3][0],
        calibratedRelation.values[3][1] - nativeRelation.values[3][1],
        calibratedRelation.values[3][2] - nativeRelation.values[3][2]);
    if (appendLog != nullptr)
    {
        appendLog(message.data());
    }
    AppendCalibrationAuditFile(auditPath, message.data());
}

void LogAppliedOverride(
    void (*appendLog)(const wchar_t* message),
    const wchar_t* auditPath,
    const void* activeItem,
    const bfvr::BFSoldierOffHandWeaponFingerprint& fingerprint,
    const Matrix4& nativeRelation,
    const Matrix4& calibratedRelation) noexcept
{
    std::array<wchar_t, 1024> message = {};
    _snwprintf_s(
        message.data(), message.size(), _TRUNCATE,
        L"OFFHAND_CALIBRATION_OVERRIDE_APPLIED item=%p activeItemIndex=3 "
        L"zoomFov=%.6f soldierCameraPosition=(%.6f,%.6f,%.6f) "
        L"nativeTranslation=(%.7f,%.7f,%.7f) "
        L"calibratedTranslation=(%.7f,%.7f,%.7f).",
        activeItem,
        fingerprint.zoomFov,
        fingerprint.soldierCameraPosition[0],
        fingerprint.soldierCameraPosition[1],
        fingerprint.soldierCameraPosition[2],
        nativeRelation.values[3][0],
        nativeRelation.values[3][1],
        nativeRelation.values[3][2],
        calibratedRelation.values[3][0],
        calibratedRelation.values[3][1],
        calibratedRelation.values[3][2]);
    if (appendLog != nullptr)
    {
        appendLog(message.data());
    }
    AppendCalibrationAuditFile(auditPath, message.data());
}

void LogOverrideProbe(
    void (*appendLog)(const wchar_t* message),
    const wchar_t* auditPath,
    const void* activeItem,
    const void* objectTemplate,
    const LONG activeItemIndex,
    const bool fingerprintReadable,
    const bfvr::BFSoldierOffHandWeaponFingerprint& fingerprint,
    const Matrix4& nativeRelation,
    const bool matched) noexcept
{
    std::array<wchar_t, 1792> message = {};
    _snwprintf_s(
        message.data(), message.size(), _TRUNCATE,
        L"OFFHAND_CALIBRATION_OVERRIDE_PROBE item=%p template=%p "
        L"activeItemIndex=%ld fingerprintReadable=%d matched=%d "
        L"zoomFov=%.7f soldierCameraPosition=(%.7f,%.7f,%.7f) "
        L"native=[%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f|%.7f %.7f %.7f].",
        activeItem, objectTemplate, activeItemIndex,
        fingerprintReadable ? 1 : 0, matched ? 1 : 0,
        fingerprint.zoomFov,
        fingerprint.soldierCameraPosition[0],
        fingerprint.soldierCameraPosition[1],
        fingerprint.soldierCameraPosition[2],
        nativeRelation.values[0][0], nativeRelation.values[0][1], nativeRelation.values[0][2],
        nativeRelation.values[1][0], nativeRelation.values[1][1], nativeRelation.values[1][2],
        nativeRelation.values[2][0], nativeRelation.values[2][1], nativeRelation.values[2][2],
        nativeRelation.values[3][0], nativeRelation.values[3][1], nativeRelation.values[3][2]);
    if (appendLog != nullptr)
    {
        appendLog(message.data());
    }
    AppendCalibrationAuditFile(auditPath, message.data());
}

} // namespace

namespace bfvr
{

void BFSoldierOffHandCalibration::ConfigureFromEnvironment(
    void (*appendLog)(const wchar_t* message)) noexcept
{
    wchar_t value[2] = {};
    const bool enabled =
        GetEnvironmentVariableW(
            kCalibrationEnvironment,
            value,
            static_cast<DWORD>(std::size(value))) == 1 &&
        value[0] == L'1';
    std::array<wchar_t, MAX_PATH> auditPath = {};
    const DWORD auditPathLength = GetEnvironmentVariableW(
        kCalibrationLogEnvironment,
        auditPath.data(),
        static_cast<DWORD>(auditPath.size()));
    if (auditPathLength == 0 || auditPathLength >= auditPath.size())
    {
        auditPath.fill(L'\0');
    }
    AcquireSRWLockExclusive(&lock_);
    // Persistent override probes are temporary development diagnostics. Keep
    // their observer/file sinks disconnected unless capture or an explicit
    // audit path was requested for this process.
    appendLog_ = enabled || auditPath[0] != L'\0' ? appendLog : nullptr;
    auditPath_ = auditPath;
    enabled_ = enabled;
    leftStickWasDown_ = false;
    calibrationValid_ = false;
    lastProbedPersistentItem_ = nullptr;
    lastLoggedPersistentItem_ = nullptr;
    ReleaseSRWLockExclusive(&lock_);
    if (enabled && appendLog != nullptr)
    {
        appendLog(
            L"Experimental off-hand calibration is ENABLED for this process. "
            L"With a primary weapon equipped and left support released, place "
            L"the free visual left hand at the desired socket and click the "
            L"left stick. Each capture is logged and hot-applied only to that "
            L"exact live item lifetime.");
    }
}

void BFSoldierOffHandCalibration::ConfigureForTesting(
    const bool enabled,
    const wchar_t* auditPath) noexcept
{
    AcquireSRWLockExclusive(&lock_);
    appendLog_ = nullptr;
    auditPath_.fill(L'\0');
    if (auditPath != nullptr)
    {
        wcsncpy_s(
            auditPath_.data(), auditPath_.size(), auditPath, _TRUNCATE);
    }
    enabled_ = enabled;
    leftStickWasDown_ = false;
    calibrationValid_ = false;
    lastProbedPersistentItem_ = nullptr;
    lastLoggedPersistentItem_ = nullptr;
    ReleaseSRWLockExclusive(&lock_);
}

void BFSoldierOffHandCalibration::UpdateCapture(
    const BFSoldierOffHandCalibrationInput& input) noexcept
{
    Matrix4 nativeRelation = input.nativeLeftHandFromRightHand;
    Matrix4 calibratedRelation = {};
    void (*appendLog)(const wchar_t* message) = nullptr;
    std::array<wchar_t, MAX_PATH> auditPath = {};
    const wchar_t* rejection = nullptr;
    LONG sequence = 0;
    bool captured = false;

    AcquireSRWLockExclusive(&lock_);
    appendLog = appendLog_;
    auditPath = auditPath_;
    const bool pressed = input.leftStickDown && !leftStickWasDown_;
    leftStickWasDown_ = input.leftStickDown;
    if (!enabled_ || !pressed)
    {
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    if (input.activeItemIndex != 3 || input.soldier == nullptr ||
        input.skeleton == nullptr || input.activeItem == nullptr)
    {
        rejection = L"not-primary-slot-3";
    }
    else if (!input.leftGripTracked)
    {
        rejection = L"left-grip-not-tracked";
    }
    else if (input.leftSqueezeHeld || input.supportHeld)
    {
        rejection = L"left-support-not-released";
    }
    else if (!IsFinite(input.freeLeftHandLocal) ||
             !IsFinite(input.controllerRightHandWorld) ||
             !IsFinite(input.inverseSoldierWorld) ||
             !IsFinite(nativeRelation))
    {
        rejection = L"non-finite-pose";
    }
    else
    {
        const Matrix4 rightHandLocal = Multiply(
            input.controllerRightHandWorld,
            input.inverseSoldierWorld);
        const auto inverseRightHandLocal = Invert(rightHandLocal);
        if (!inverseRightHandLocal.has_value())
        {
            rejection = L"right-hand-inverse-failed";
        }
        else
        {
            calibratedRelation = Multiply(
                input.freeLeftHandLocal,
                *inverseRightHandLocal);
            if (!IsFinite(calibratedRelation))
            {
                rejection = L"calibrated-relation-invalid";
            }
            else
            {
                soldier_ = input.soldier;
                skeleton_ = input.skeleton;
                activeItem_ = input.activeItem;
                calibratedLeftHandFromRightHand_ = calibratedRelation;
                calibrationValid_ = true;
                sequence = ++captureSequence_;
                captured = true;
            }
        }
    }
    ReleaseSRWLockExclusive(&lock_);

    if (captured)
    {
        LogCapturedRelation(
            appendLog, auditPath.data(), sequence, input.soldier, input.skeleton,
            input.activeItem, nativeRelation, calibratedRelation);
    }
    else if (rejection != nullptr)
    {
        LogRejectedCapture(
            appendLog, rejection, input.activeItem, input.activeItemIndex);
    }
}

Matrix4 BFSoldierOffHandCalibration::Resolve(
    void* soldier,
    void* skeleton,
    const void* activeItem,
    const LONG activeItemIndex,
    const Matrix4& nativeLeftHandFromRightHand) noexcept
{
    Matrix4 result = nativeLeftHandFromRightHand;
    BFSoldierOffHandWeaponFingerprint weaponFingerprint = {};
    const void* objectTemplateAddress = nullptr;
    const bool fingerprintReadable = ReadWeaponFingerprint(
        activeItem, weaponFingerprint, objectTemplateAddress);
    std::optional<Matrix4> persistentOverride;
    if (fingerprintReadable)
    {
        persistentOverride = ResolveBFSoldierOffHandOverride(
            weaponFingerprint,
            activeItemIndex,
            nativeLeftHandFromRightHand);
    }
    bool logPersistentProbe = false;
    bool logPersistentOverride = false;
    void (*appendLog)(const wchar_t* message) = nullptr;
    std::array<wchar_t, MAX_PATH> auditPath = {};
    AcquireSRWLockExclusive(&lock_);
    if (activeItem != nullptr && activeItemIndex == 3 &&
        lastProbedPersistentItem_ != activeItem &&
        (appendLog_ != nullptr || auditPath_[0] != L'\0'))
    {
        lastProbedPersistentItem_ = activeItem;
        appendLog = appendLog_;
        auditPath = auditPath_;
        logPersistentProbe = true;
    }
    if (persistentOverride.has_value())
    {
        result = *persistentOverride;
        if (lastLoggedPersistentItem_ != activeItem &&
            (appendLog_ != nullptr || auditPath_[0] != L'\0'))
        {
            lastLoggedPersistentItem_ = activeItem;
            appendLog = appendLog_;
            auditPath = auditPath_;
            logPersistentOverride = true;
        }
    }
    if (enabled_ && calibrationValid_ && activeItemIndex == 3 &&
        soldier_ == soldier && skeleton_ == skeleton &&
        activeItem_ == activeItem)
    {
        result = calibratedLeftHandFromRightHand_;
    }
    ReleaseSRWLockExclusive(&lock_);
    if (logPersistentProbe)
    {
        LogOverrideProbe(
            appendLog,
            auditPath.data(),
            activeItem,
            objectTemplateAddress,
            activeItemIndex,
            fingerprintReadable,
            weaponFingerprint,
            nativeLeftHandFromRightHand,
            persistentOverride.has_value());
    }
    if (logPersistentOverride)
    {
        LogAppliedOverride(
            appendLog,
            auditPath.data(),
            activeItem,
            weaponFingerprint,
            nativeLeftHandFromRightHand,
            *persistentOverride);
    }
    return result;
}

void BFSoldierOffHandCalibration::Reset() noexcept
{
    AcquireSRWLockExclusive(&lock_);
    appendLog_ = nullptr;
    auditPath_.fill(L'\0');
    soldier_ = nullptr;
    skeleton_ = nullptr;
    activeItem_ = nullptr;
    lastProbedPersistentItem_ = nullptr;
    lastLoggedPersistentItem_ = nullptr;
    calibratedLeftHandFromRightHand_ = {};
    captureSequence_ = 0;
    enabled_ = false;
    leftStickWasDown_ = false;
    calibrationValid_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

} // namespace bfvr
