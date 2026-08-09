#include "client/HandWeaponRecoilRuntime.h"

#include "stereo/HandWeaponRecoilMath.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace
{

constexpr DWORD kNativeSampleQuietPeriodMs = 75;
constexpr DWORD kMaximumShotToNativeSampleAgeMs = 1500;
constexpr float kMaximumAbsoluteRecoilAngleDegrees = 45.0F;
constexpr LONG kMaximumStepLogs = 16;

struct RuntimeState
{
    bfvr::stereo::HandWeaponRecoilState recoil = {};
    const void* soldier = nullptr;
    const void* weapon = nullptr;
    DWORD lastShotAt = 0;
    DWORD lastNativeAt = 0;
    DWORD releasedAt = 0;
    DWORD recoveryAdvancedAt = 0;
    float pitchScale = bfvr::kOneHandPitchRecoilScale;
    float yawScale = bfvr::kOneHandYawRecoilScale;
    std::uint64_t nextNativeSequence = 0;
    LONG shotSequence = 0;
    int lastRecoilIndex = 0;
    bool fireHeld = false;
    bool fireStateKnown = false;
    bool started = false;
};

SRWLOCK g_lock = SRWLOCK_INIT;
RuntimeState g_state = {};
bfvr::HandWeaponRecoilLogCallback g_appendLog = nullptr;
volatile LONG g_nativeStepsApplied = 0;
volatile LONG g_nativeStepsRejected = 0;
volatile LONG g_identityMismatches = 0;
volatile LONG g_recoveryUpdates = 0;
volatile LONG g_recoveryCompletions = 0;
volatile LONG g_loggedSteps = 0;

void WriteLog(const wchar_t* format, ...) noexcept
{
    const auto callback = g_appendLog;
    if (callback == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1000> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);
    callback(message.data());
}

void ResetMotionLocked(
    const void* soldier,
    const void* weapon,
    const DWORD now) noexcept
{
    g_state.recoil = {};
    g_state.soldier = soldier;
    g_state.weapon = weapon;
    g_state.lastShotAt = now;
    g_state.lastNativeAt = 0;
    g_state.releasedAt = g_state.fireHeld ? 0 : now;
    g_state.recoveryAdvancedAt = now;
    g_state.lastRecoilIndex = 0;
}

void AdvanceRecoveryLocked(const DWORD now) noexcept
{
    if (!g_state.started || g_state.fireHeld ||
        bfvr::stereo::IsHandWeaponRecoilAtIdentity(g_state.recoil))
    {
        return;
    }
    const DWORD quietAfter = std::max(
        g_state.lastNativeAt + kNativeSampleQuietPeriodMs,
        g_state.lastShotAt + kNativeSampleQuietPeriodMs);
    const DWORD eligibleAt = std::max(
        std::max(g_state.releasedAt, quietAfter),
        g_state.recoveryAdvancedAt);
    if (now <= eligibleAt)
    {
        return;
    }
    const bool wasActive =
        !bfvr::stereo::IsHandWeaponRecoilAtIdentity(g_state.recoil);
    const float elapsedSeconds =
        static_cast<float>(now - eligibleAt) * 0.001F;
    if (!bfvr::stereo::RecoverHandWeaponRecoilToIdentity(
            g_state.recoil,
            elapsedSeconds,
            bfvr::kHandWeaponRecoilReturnHalfLifeSeconds,
            bfvr::kHandWeaponRecoilReturnHalfLifeSeconds))
    {
        return;
    }
    g_state.recoveryAdvancedAt = now;
    InterlockedIncrement(&g_recoveryUpdates);
    if (wasActive &&
        bfvr::stereo::IsHandWeaponRecoilAtIdentity(g_state.recoil))
    {
        InterlockedIncrement(&g_recoveryCompletions);
    }
}

} // namespace

namespace bfvr
{

void StartHandWeaponRecoilRuntime(
    const HandWeaponRecoilLogCallback appendLog) noexcept
{
    AcquireSRWLockExclusive(&g_lock);
    g_state = {};
    g_state.started = true;
    g_appendLog = appendLog;
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedExchange(&g_nativeStepsApplied, 0);
    InterlockedExchange(&g_nativeStepsRejected, 0);
    InterlockedExchange(&g_identityMismatches, 0);
    InterlockedExchange(&g_recoveryUpdates, 0);
    InterlockedExchange(&g_recoveryCompletions, 0);
    InterlockedExchange(&g_loggedSteps, 0);
    WriteLog(
        L"Handweapon recoil runtime armed: native BF1942 paired pitch/yaw steps drive only the held gun and matching fire pose; two-hand scale=(%.2f,%.2f), one-hand scale=(%.2f,%.2f), residual return half-life=%.3f s, safety bound=%.1f deg. No camera/view API is reachable from this module.",
        kTwoHandPitchRecoilScale,
        kTwoHandYawRecoilScale,
        kOneHandPitchRecoilScale,
        kOneHandYawRecoilScale,
        kHandWeaponRecoilReturnHalfLifeSeconds,
        kMaximumAbsoluteRecoilAngleDegrees);
}

void StopHandWeaponRecoilRuntime() noexcept
{
    LogHandWeaponRecoilSummary();
    AcquireSRWLockExclusive(&g_lock);
    g_state = {};
    g_appendLog = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
}

void LogHandWeaponRecoilSummary() noexcept
{
    bfvr::stereo::HandWeaponRecoilAngles angles = {};
    AcquireSRWLockShared(&g_lock);
    angles = g_state.recoil.angles;
    ReleaseSRWLockShared(&g_lock);
    WriteLog(
        L"Handweapon recoil summary: nativeApplied=%ld nativeRejected=%ld identityMismatches=%ld recoveryUpdates=%ld recoveryCompletions=%ld finalAngles=(%.6f,%.6f).",
        InterlockedCompareExchange(&g_nativeStepsApplied, 0, 0),
        InterlockedCompareExchange(&g_nativeStepsRejected, 0, 0),
        InterlockedCompareExchange(&g_identityMismatches, 0, 0),
        InterlockedCompareExchange(&g_recoveryUpdates, 0, 0),
        InterlockedCompareExchange(&g_recoveryCompletions, 0, 0),
        angles.pitchDegrees,
        angles.yawDegrees);
}

void NotifyHandWeaponRecoilShot(
    const void* soldier,
    const void* weapon,
    const bool bothHands) noexcept
{
    if (soldier == nullptr || weapon == nullptr)
    {
        return;
    }
    const DWORD now = GetTickCount();
    LONG shotSequence = 0;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_state.started)
    {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    AdvanceRecoveryLocked(now);
    const bool sameAcceptedBoundary =
        g_state.soldier == soldier && g_state.weapon == weapon &&
        g_state.lastShotAt == now;
    if (g_state.soldier != soldier || g_state.weapon != weapon)
    {
        ResetMotionLocked(soldier, weapon, now);
    }
    g_state.lastShotAt = now;
    g_state.pitchScale = bothHands
        ? kTwoHandPitchRecoilScale
        : kOneHandPitchRecoilScale;
    g_state.yawScale = bothHands
        ? kTwoHandYawRecoilScale
        : kOneHandYawRecoilScale;
    shotSequence = sameAcceptedBoundary
        ? g_state.shotSequence
        : ++g_state.shotSequence;
    ReleaseSRWLockExclusive(&g_lock);

    if (shotSequence <= 8)
    {
        WriteLog(
            L"Handweapon recoil shot latched: sequence=%ld soldier=%p weapon=%p support=%ls scale=(%.2f,%.2f).",
            shotSequence,
            soldier,
            weapon,
            bothHands ? L"two-hand" : L"one-hand",
            bothHands ? kTwoHandPitchRecoilScale : kOneHandPitchRecoilScale,
            bothHands ? kTwoHandYawRecoilScale : kOneHandYawRecoilScale);
    }
}

void PublishHandWeaponRecoilFireHeld(const bool held) noexcept
{
    const DWORD now = GetTickCount();
    AcquireSRWLockExclusive(&g_lock);
    if (!g_state.started)
    {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    AdvanceRecoveryLocked(now);
    if (!held && (!g_state.fireStateKnown || g_state.fireHeld))
    {
        g_state.releasedAt = now;
        g_state.recoveryAdvancedAt = now;
    }
    g_state.fireHeld = held;
    g_state.fireStateKnown = true;
    ReleaseSRWLockExclusive(&g_lock);
}

void PublishPairedNativeHandWeaponRecoil(
    const void* soldier,
    const float pitchDegrees,
    const float yawDegrees,
    const int recoilIndex) noexcept
{
    const DWORD now = GetTickCount();
    stereo::HandWeaponRecoilAngles accumulated = {};
    float pitchScale = 0.0F;
    float yawScale = 0.0F;
    std::uint64_t nativeSequence = 0;
    stereo::HandWeaponRecoilStepStatus status =
        stereo::HandWeaponRecoilStepStatus::Rejected;
    bool eligible = false;

    AcquireSRWLockExclusive(&g_lock);
    if (g_state.started && soldier != nullptr &&
        g_state.soldier == soldier && g_state.weapon != nullptr &&
        now - g_state.lastShotAt <= kMaximumShotToNativeSampleAgeMs)
    {
        eligible = true;
        nativeSequence = ++g_state.nextNativeSequence;
        pitchScale = g_state.pitchScale;
        yawScale = g_state.yawScale;
        status = stereo::AccumulateHandWeaponRecoilStep(
            g_state.recoil,
            nativeSequence,
            pitchDegrees,
            yawDegrees,
            pitchScale,
            yawScale,
            kMaximumAbsoluteRecoilAngleDegrees);
        if (status == stereo::HandWeaponRecoilStepStatus::Applied)
        {
            g_state.lastNativeAt = now;
            g_state.recoveryAdvancedAt = now;
            g_state.lastRecoilIndex = recoilIndex;
            accumulated = g_state.recoil.angles;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (status == stereo::HandWeaponRecoilStepStatus::Applied)
    {
        InterlockedIncrement(&g_nativeStepsApplied);
        if (InterlockedIncrement(&g_loggedSteps) <= kMaximumStepLogs)
        {
            WriteLog(
                L"Handweapon native recoil applied: sequence=%llu index=%d step=(%.7f,%.7f) scale=(%.2f,%.2f) accumulated=(%.7f,%.7f) soldier=%p.",
                static_cast<unsigned long long>(nativeSequence),
                recoilIndex,
                pitchDegrees,
                yawDegrees,
                pitchScale,
                yawScale,
                accumulated.pitchDegrees,
                accumulated.yawDegrees,
                soldier);
        }
    }
    else if (eligible)
    {
        InterlockedIncrement(&g_nativeStepsRejected);
    }
}

std::optional<stereo::Matrix4> MakeCurrentHandWeaponRecoilPose(
    const stereo::Matrix4& rawGunWorld,
    const void* soldier,
    const void* weapon) noexcept
{
    stereo::HandWeaponRecoilAngles angles = {};
    bool matches = false;
    bool hasBoundIdentity = false;
    AcquireSRWLockExclusive(&g_lock);
    hasBoundIdentity = g_state.started && g_state.soldier != nullptr &&
        g_state.weapon != nullptr;
    if (g_state.started && soldier != nullptr && weapon != nullptr &&
        g_state.soldier == soldier && g_state.weapon == weapon)
    {
        AdvanceRecoveryLocked(GetTickCount());
        angles = g_state.recoil.angles;
        matches = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!matches)
    {
        if (hasBoundIdentity)
        {
            InterlockedIncrement(&g_identityMismatches);
        }
        return std::nullopt;
    }
    return stereo::ApplyHandWeaponRecoilToGunPose(rawGunWorld, angles);
}

} // namespace bfvr
