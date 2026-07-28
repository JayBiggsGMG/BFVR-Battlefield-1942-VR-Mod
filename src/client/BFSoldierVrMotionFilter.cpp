#include "client/BFSoldierVrMotionFilter.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kUpdateCameraShakeRva = 0x000FACD0;
constexpr std::ptrdiff_t kPitchRecoilRva = 0x000F6E70;
constexpr std::ptrdiff_t kYawRecoilRva = 0x000F6DE0;
constexpr std::size_t kCameraShakeMatrixOffset = 0x54C;

constexpr BYTE kUpdateCameraShakePrefix[] = {
    0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8B,
    0xF1, 0x8B, 0x46, 0x50, 0x85, 0xC0, 0x57, 0x0F, 0x85};
constexpr BYTE kRecoilPrefix[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x86, 0xE8, 0x03, 0x00, 0x00,
    0x83, 0xF8, 0xFF, 0x74, 0x71, 0x8B, 0x96, 0x1C, 0x01};

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t expectedLength) noexcept
{
    if (target == nullptr || expected == nullptr || expectedLength == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, expectedLength) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct LegacyRecoilState
{
    float pitch = 0.0F;
    float yaw = 0.0F;
    DWORD pitchUpdatedAt = 0;
    DWORD yawUpdatedAt = 0;
    const void* soldier = nullptr;
    LONG sequence = 0;
    bool pitchValid = false;
    bool yawValid = false;
};

SRWLOCK g_legacyRecoilLock = SRWLOCK_INIT;
LegacyRecoilState g_legacyRecoil = {};

void PublishLegacyPitchRecoil(void* soldier, float pitch) noexcept
{
    AcquireSRWLockExclusive(&g_legacyRecoilLock);
    if (g_legacyRecoil.soldier != soldier)
    {
        g_legacyRecoil = {};
        g_legacyRecoil.soldier = soldier;
    }
    g_legacyRecoil.pitch = pitch;
    g_legacyRecoil.pitchUpdatedAt = GetTickCount();
    g_legacyRecoil.pitchValid = true;
    ReleaseSRWLockExclusive(&g_legacyRecoilLock);
}

void PublishLegacyYawRecoil(void* soldier, float yaw) noexcept
{
    AcquireSRWLockExclusive(&g_legacyRecoilLock);
    if (g_legacyRecoil.soldier != soldier)
    {
        g_legacyRecoil = {};
        g_legacyRecoil.soldier = soldier;
    }
    g_legacyRecoil.yaw = yaw;
    g_legacyRecoil.yawUpdatedAt = GetTickCount();
    g_legacyRecoil.yawValid = true;
    InterlockedIncrement(&g_legacyRecoil.sequence);
    ReleaseSRWLockExclusive(&g_legacyRecoilLock);
}

void ClearLegacyRecoil() noexcept
{
    AcquireSRWLockExclusive(&g_legacyRecoilLock);
    g_legacyRecoil = {};
    ReleaseSRWLockExclusive(&g_legacyRecoilLock);
}

} // namespace

namespace bfvr
{

bool ReadFreshBFSoldierVrLegacyRecoil(
    BFSoldierVrLegacyRecoil& recoil,
    DWORD maximumAgeMs) noexcept
{
    recoil = {};
    if (maximumAgeMs == 0)
    {
        return false;
    }
    AcquireSRWLockShared(&g_legacyRecoilLock);
    const LegacyRecoilState snapshot = g_legacyRecoil;
    ReleaseSRWLockShared(&g_legacyRecoilLock);
    const DWORD now = GetTickCount();
    if (!snapshot.pitchValid || !snapshot.yawValid ||
        now - snapshot.pitchUpdatedAt > maximumAgeMs ||
        now - snapshot.yawUpdatedAt > maximumAgeMs ||
        !std::isfinite(snapshot.pitch) || !std::isfinite(snapshot.yaw))
    {
        return false;
    }
    recoil.pitch = snapshot.pitch;
    recoil.yaw = snapshot.yaw;
    recoil.soldier = snapshot.soldier;
    recoil.sequence = snapshot.sequence;
    return true;
}

class BFSoldierVrMotionFilter::Impl
{
public:
    using UpdateCameraShakeFn = void(__thiscall*)(void* soldier, float elapsedSeconds);
    using RecoilFn = float(__fastcall*)(void* soldier);

    bool Create(
        void* image,
        BFSoldierVrMotionFilterLogCallback callback)
    {
        gameImage = static_cast<std::byte*>(image);
        logCallback = callback;
        updateCameraShakeTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kUpdateCameraShakeRva;
        pitchRecoilTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kPitchRecoilRva;
        yawRecoilTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kYawRecoilRva;

        if (!HasExpectedPrefix(
                updateCameraShakeTarget,
                kUpdateCameraShakePrefix,
                sizeof(kUpdateCameraShakePrefix)) ||
            !HasExpectedPrefix(
                pitchRecoilTarget,
                kRecoilPrefix,
                sizeof(kRecoilPrefix)) ||
            !HasExpectedPrefix(
                yawRecoilTarget,
                kRecoilPrefix,
                sizeof(kRecoilPrefix)))
        {
            WriteLog(
                L"VR player-motion filter rejected the profiled WinPC targets: updateShake=%p pitchRecoil=%p yawRecoil=%p.",
                updateCameraShakeTarget,
                pitchRecoilTarget,
                yawRecoilTarget);
            return false;
        }

        const MH_STATUS shakeStatus = MH_CreateHook(
            updateCameraShakeTarget,
            reinterpret_cast<LPVOID>(&Impl::UpdateCameraShakeHook),
            reinterpret_cast<LPVOID*>(&originalUpdateCameraShake));
        const MH_STATUS pitchStatus = shakeStatus == MH_OK &&
            originalUpdateCameraShake != nullptr
            ? MH_CreateHook(
                pitchRecoilTarget,
                reinterpret_cast<LPVOID>(&Impl::PitchRecoilHook),
                reinterpret_cast<LPVOID*>(&originalPitchRecoil))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS yawStatus = pitchStatus == MH_OK &&
            originalPitchRecoil != nullptr
            ? MH_CreateHook(
                yawRecoilTarget,
                reinterpret_cast<LPVOID>(&Impl::YawRecoilHook),
                reinterpret_cast<LPVOID*>(&originalYawRecoil))
            : MH_ERROR_NOT_CREATED;
        created = shakeStatus == MH_OK &&
            pitchStatus == MH_OK &&
            yawStatus == MH_OK &&
            originalUpdateCameraShake != nullptr &&
            originalPitchRecoil != nullptr &&
            originalYawRecoil != nullptr;
        if (!created)
        {
            WriteLog(
                L"VR player-motion filter could not create all hooks: updateShake=%d pitchRecoil=%d yawRecoil=%d.",
                static_cast<int>(shakeStatus),
                static_cast<int>(pitchStatus),
                static_cast<int>(yawStatus));
            RemoveCreatedHooks();
            return false;
        }
        active = this;
        return true;
    }

    bool Enable()
    {
        if (!created || active != this)
        {
            return false;
        }
        const MH_STATUS shakeStatus = MH_EnableHook(updateCameraShakeTarget);
        const MH_STATUS pitchStatus = shakeStatus == MH_OK
            ? MH_EnableHook(pitchRecoilTarget)
            : MH_ERROR_DISABLED;
        const MH_STATUS yawStatus = pitchStatus == MH_OK
            ? MH_EnableHook(yawRecoilTarget)
            : MH_ERROR_DISABLED;
        enabled = shakeStatus == MH_OK &&
            pitchStatus == MH_OK &&
            yawStatus == MH_OK;
        if (!enabled)
        {
            if (shakeStatus == MH_OK)
            {
                MH_DisableHook(updateCameraShakeTarget);
            }
            if (pitchStatus == MH_OK)
            {
                MH_DisableHook(pitchRecoilTarget);
            }
            WriteLog(
                L"VR player-motion filter could not enable all hooks: updateShake=%d pitchRecoil=%d yawRecoil=%d.",
                static_cast<int>(shakeStatus),
                static_cast<int>(pitchStatus),
                static_cast<int>(yawStatus));
            return false;
        }
        WriteLog(
            L"VR player-motion filter armed: BFSoldier pitch/yaw recoil returns zero only to the legacy camera path, and each generated camera-shake matrix is replaced with identity after BF1942 updates its native state. Weapon recoil state, animation, firing, spread, and controller-directed aim remain native.");
        return true;
    }

    void DisableAndRemove()
    {
        if (enabled)
        {
            MH_DisableHook(yawRecoilTarget);
            MH_DisableHook(pitchRecoilTarget);
            MH_DisableHook(updateCameraShakeTarget);
            enabled = false;
        }
        RemoveCreatedHooks();
        if (active == this)
        {
            active = nullptr;
        }
        originalUpdateCameraShake = nullptr;
        originalPitchRecoil = nullptr;
        originalYawRecoil = nullptr;
        updateCameraShakeTarget = nullptr;
        pitchRecoilTarget = nullptr;
        yawRecoilTarget = nullptr;
        gameImage = nullptr;
        ClearLegacyRecoil();
    }

    void LogSummary() const
    {
        WriteLog(
            L"VR player-motion filter summary: shakeUpdates=%ld shakeMatricesNeutralized=%ld pitchRecoilQueries=%ld yawRecoilQueries=%ld matrixWriteFailures=%ld.",
            updateCameraShakeCalls,
            shakeMatricesNeutralized,
            pitchRecoilCalls,
            yawRecoilCalls,
            shakeMatrixWriteFailures);
    }

private:
    static void __fastcall UpdateCameraShakeHook(
        void* soldier,
        void*,
        float elapsedSeconds)
    {
        Impl* const self = active;
        if (self == nullptr || self->originalUpdateCameraShake == nullptr)
        {
            return;
        }
        self->originalUpdateCameraShake(soldier, elapsedSeconds);
        InterlockedIncrement(&self->updateCameraShakeCalls);
        if (!self->WriteIdentityShakeMatrix(soldier))
        {
            InterlockedIncrement(&self->shakeMatrixWriteFailures);
            return;
        }
        InterlockedIncrement(&self->shakeMatricesNeutralized);
    }

    static float __fastcall PitchRecoilHook(void* soldier, void*)
    {
        Impl* const self = active;
        if (self == nullptr || self->originalPitchRecoil == nullptr)
        {
            return 0.0F;
        }
        // Preserve the engine's state maintenance, then keep its artificial
        // recoil angle out of the player camera. The held weapon still owns
        // its native recoil state and presentation animation.
        const float pitch = self->originalPitchRecoil(soldier);
        PublishLegacyPitchRecoil(soldier, pitch);
        InterlockedIncrement(&self->pitchRecoilCalls);
        if (std::fabs(pitch) > 0.000001F &&
            InterlockedIncrement(&self->loggedRecoilImpulses) <= 8)
        {
            self->WriteLog(
                L"Captured native BFSoldier pitch recoil impulse %.7f for soldier=%p.",
                pitch,
                soldier);
        }
        return 0.0F;
    }

    static float __fastcall YawRecoilHook(void* soldier, void*)
    {
        Impl* const self = active;
        if (self == nullptr || self->originalYawRecoil == nullptr)
        {
            return 0.0F;
        }
        const float yaw = self->originalYawRecoil(soldier);
        PublishLegacyYawRecoil(soldier, yaw);
        InterlockedIncrement(&self->yawRecoilCalls);
        if (std::fabs(yaw) > 0.000001F &&
            InterlockedIncrement(&self->loggedRecoilImpulses) <= 8)
        {
            self->WriteLog(
                L"Captured native BFSoldier yaw recoil impulse %.7f for soldier=%p.",
                yaw,
                soldier);
        }
        return 0.0F;
    }

    bool WriteIdentityShakeMatrix(void* soldier) noexcept
    {
        if (soldier == nullptr)
        {
            return false;
        }
        constexpr std::array<float, 16> kIdentity = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
        __try
        {
            std::memcpy(
                static_cast<std::byte*>(soldier) + kCameraShakeMatrixOffset,
                kIdentity.data(),
                sizeof(kIdentity));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveCreatedHooks()
    {
        // Creation can fail after one or two MinHook entries succeeded. Remove
        // every non-null target so the caller never inherits a partial hook
        // set, regardless of the aggregate created flag.
        if (yawRecoilTarget != nullptr)
        {
            MH_RemoveHook(yawRecoilTarget);
        }
        if (pitchRecoilTarget != nullptr)
        {
            MH_RemoveHook(pitchRecoilTarget);
        }
        if (updateCameraShakeTarget != nullptr)
        {
            MH_RemoveHook(updateCameraShakeTarget);
        }
        created = false;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr)
        {
            return;
        }
        std::array<wchar_t, 900> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        logCallback(message.data());
    }

    static Impl* active;
    std::byte* gameImage = nullptr;
    void* updateCameraShakeTarget = nullptr;
    void* pitchRecoilTarget = nullptr;
    void* yawRecoilTarget = nullptr;
    UpdateCameraShakeFn originalUpdateCameraShake = nullptr;
    RecoilFn originalPitchRecoil = nullptr;
    RecoilFn originalYawRecoil = nullptr;
    BFSoldierVrMotionFilterLogCallback logCallback = nullptr;
    volatile LONG updateCameraShakeCalls = 0;
    volatile LONG shakeMatricesNeutralized = 0;
    volatile LONG pitchRecoilCalls = 0;
    volatile LONG yawRecoilCalls = 0;
    volatile LONG loggedRecoilImpulses = 0;
    volatile LONG shakeMatrixWriteFailures = 0;
    bool created = false;
    bool enabled = false;
};

BFSoldierVrMotionFilter::Impl* BFSoldierVrMotionFilter::Impl::active = nullptr;

BFSoldierVrMotionFilter::BFSoldierVrMotionFilter()
    : impl_(std::make_unique<Impl>())
{
}

BFSoldierVrMotionFilter::~BFSoldierVrMotionFilter() = default;

bool BFSoldierVrMotionFilter::Create(
    void* gameImage,
    BFSoldierVrMotionFilterLogCallback logCallback)
{
    return impl_ != nullptr && impl_->Create(gameImage, logCallback);
}

bool BFSoldierVrMotionFilter::Enable()
{
    return impl_ != nullptr && impl_->Enable();
}

void BFSoldierVrMotionFilter::DisableAndRemove()
{
    if (impl_ != nullptr)
    {
        impl_->DisableAndRemove();
    }
}

void BFSoldierVrMotionFilter::LogSummary() const
{
    if (impl_ != nullptr)
    {
        impl_->LogSummary();
    }
}

} // namespace bfvr
