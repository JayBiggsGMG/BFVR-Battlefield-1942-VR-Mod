#include "client/BFSoldierFirstPersonArmProbe.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace
{

constexpr std::ptrdiff_t kUpdateCameraShakeRva = 0x000FACD0;
constexpr std::ptrdiff_t kUpdateVisibleSetsRva = 0x000F7F20;
constexpr std::ptrdiff_t kUpdateAnimationsRva = 0x000FB150;
constexpr std::ptrdiff_t kAnimatedMeshDrawRva = 0x001AEEC0;
constexpr std::ptrdiff_t kActiveItemQueryInterfaceRva = 0x0013B820;
constexpr std::size_t kSoldierTemplateOffset = 0x4C;
constexpr std::size_t kSoldierFirstPersonStateOffset = 0x290;
constexpr std::size_t kSoldierAnimationSkeletonOffset = 0x298;
constexpr std::size_t kSoldierCollisionSkeletonOffset = 0x29C;
constexpr std::size_t kSoldierChildBindingBeginOffset = 0x2A4;
constexpr std::size_t kSoldierChildBindingEndOffset = 0x2A8;
constexpr std::size_t kSoldierActiveItemIndexOffset = 0x3E8;
constexpr std::size_t kTemplateFirstPersonPartBeginOffset = 0x36C;
constexpr std::size_t kTemplateFirstPersonPartEndOffset = 0x370;
constexpr std::size_t kTemplateRightHandBoneOffset = 0x33C;
constexpr DWORD kObservationWindowMs = 120000;
constexpr DWORD kReportIntervalMs = 100;
constexpr DWORD kWorkerHeartbeatIntervalMs = 15000;
constexpr std::size_t kChildBindingRecordStride = 0x10;
constexpr std::size_t kMaximumChildBindingRecords = 64;
constexpr std::size_t kAnimatedMeshTemplateOffset = 0x24;
constexpr std::size_t kAnimatedMeshSkeletonOffset = 0x18C;
constexpr std::size_t kAnimatedMeshTemplateNameOffset = 0x34;
constexpr std::size_t kAnimatedMeshFieldOfViewOffset = 0xF0;
constexpr std::size_t kAnimatedMeshProjectionOffset = 0xF4;
constexpr std::size_t kMaximumFirstPersonMeshRecords = 16;
constexpr DWORD kMeshCorrelationWindowMs = 3000;

constexpr BYTE kUpdateCameraShakePrefix[] = {
    0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8B,
    0xF1, 0x8B, 0x46, 0x50, 0x85, 0xC0, 0x57, 0x0F, 0x85};
constexpr BYTE kUpdateVisibleSetsPrefix[] = {
    0x51, 0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x0D,
    0xE0, 0xCB, 0x8D, 0x00, 0x8B, 0x07, 0x8B, 0x5F, 0x4C};
constexpr BYTE kUpdateAnimationsPrefix[] = {
    0x81, 0xEC, 0x78, 0x01, 0x00, 0x00, 0x55, 0x8B,
    0xE9, 0x8B, 0x85, 0x98, 0x02, 0x00, 0x00, 0x85,
    0xC0, 0x0F, 0x84, 0x6D, 0x0C, 0x00, 0x00, 0x8D};
constexpr BYTE kAnimatedMeshDrawPrefix[] = {
    0xA1, 0x44, 0x9A, 0x95, 0x00, 0x83, 0xEC, 0x18,
    0x85, 0xC0, 0x55, 0x8B, 0xE9, 0x7C, 0x36, 0x8B,
    0x45, 0x24};
constexpr BYTE kActiveItemQueryInterfacePrefix[] = {
    0x8B, 0x15, 0xD8, 0x0E, 0x90, 0x00, 0x8B, 0xC1,
    0x8B, 0x4C, 0x24, 0x04, 0x3B, 0xCA, 0x74, 0x40,
    0x3B, 0x0D, 0xC4, 0xCB, 0x8D, 0x00, 0x74, 0x38};

struct ChildBindingRecord
{
    void* receiver = nullptr;
    void* receiverVtable = nullptr;
    DWORD receiverFlags = 0;
    LONG rootBoneIndex = -1;
    LONG boneSpan = -1;
    BOOL readable = FALSE;
};

struct FirstPersonMeshRecord
{
    void* mesh = nullptr;
    void* meshTemplate = nullptr;
    void* candidateSkeleton = nullptr;
    BOOL matchesLocalAnimationSkeleton = FALSE;
    BOOL matchesLocalCollisionSkeleton = FALSE;
    void* templateNamePointer = nullptr;
    std::array<char, 48> templateNameInlinePreview = {};
    std::array<char, 96> templateNamePointerPreview = {};
    BOOL templateNameInlineReadable = FALSE;
    BOOL templateNamePointerReadable = FALSE;
    float fieldOfView = 0.0F;
    std::array<float, 16> projection = {};
    DWORD threadId = 0;
    BOOL readable = FALSE;
};

struct RightHandPoseRecord
{
    void* skeleton = nullptr;
    LONG boneIndex = -1;
    std::array<float, 16> matrix = {};
    DWORD threadId = 0;
    BOOL readable = FALSE;
};

struct ActiveItemAnimatedBundleQueryRecord
{
    void* item = nullptr;
    void* itemVtable = nullptr;
    DWORD interfaceId = 0;
    void* animatedBundleInterface = nullptr;
    void* animatedBundleInterfaceVtable = nullptr;
    DWORD threadId = 0;
    BOOL readable = FALSE;
};

struct ActiveItemLookupDispatchRecord
{
    void* lookupObject = nullptr;
    void* lookupObjectVtable = nullptr;
    void* lookupTarget = nullptr;
    std::array<BYTE, 16> lookupTargetPrefix = {};
    BOOL readable = FALSE;
};

struct FirstPersonArmTrace
{
    void* cameraSoldier = nullptr;
    void* soldier = nullptr;
    void* soldierTemplate = nullptr;
    void* animationSkeleton = nullptr;
    void* collisionSkeleton = nullptr;
    void* animationBoneRecords = nullptr;
    DWORD firstPersonState = 0;
    LONG rightHandBoneIndex = -1;
    LONG activeItemIndex = -1;
    DWORD threadId = 0;
    void* childBindingBegin = nullptr;
    void* childBindingEnd = nullptr;
    volatile LONG childBindingCount = 0;
    volatile LONG childBindingOverflow = 0;
    volatile LONG firstPersonMeshCount = 0;
    volatile LONG firstPersonMeshOverflow = 0;
    volatile LONG rightHandPoseCaptured = 0;
    volatile LONG activeItemLookupDispatchAttempted = 0;
    volatile LONG activeItemAnimatedBundleQueryCount = 0;
    volatile LONG activeItemAnimatedBundleQueryOverflow = 0;
    volatile LONG readFailures = 0;
    std::array<ChildBindingRecord, kMaximumChildBindingRecords> childBindings = {};
    std::array<FirstPersonMeshRecord, kMaximumFirstPersonMeshRecords>
        firstPersonMeshes = {};
    RightHandPoseRecord rightHandPose = {};
    std::array<ActiveItemAnimatedBundleQueryRecord, 16>
        activeItemAnimatedBundleQueries = {};
    ActiveItemLookupDispatchRecord activeItemLookupDispatch = {};
};

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    const std::size_t expectedLength) noexcept
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

template <std::size_t N>
bool CopyPrintableAsciiPreview(
    const char* source,
    std::array<char, N>& destination) noexcept
{
    static_assert(N >= 2, "preview storage must leave space for a terminator");
    destination.fill('\0');
    if (source == nullptr)
    {
        return false;
    }
    __try
    {
        for (std::size_t index = 0; index + 1 < destination.size(); ++index)
        {
            const unsigned char value = static_cast<unsigned char>(source[index]);
            if (value == 0)
            {
                return index != 0;
            }
            if (value < 0x20U || value > 0x7EU)
            {
                destination.fill('\0');
                return false;
            }
            destination[index] = static_cast<char>(value);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        destination.fill('\0');
        return false;
    }
}

class BFSoldierFirstPersonArmProbe
{
public:
    using UpdateCameraShakeFn = void(__thiscall*)(void*, float);
    using UpdateVisibleSetsFn = void(__thiscall*)(void*);
    using UpdateAnimationsFn = void(__thiscall*)(void*, float);
    using AnimatedMeshDrawFn = void(__thiscall*)(void*, void*, float);
    using ActiveItemQueryInterfaceFn = void*(__thiscall*)(void*, DWORD);

    void Start(
        void* image,
        void (*log)(const wchar_t* message),
        void (*signalCompletion)())
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            WriteLog(L"First-person-arm ownership probe ignored a duplicate start request.");
            return;
        }
        gameImage_ = static_cast<std::byte*>(image);
        appendLog_ = log;
        signalCompletion_ = signalCompletion;
        updateCameraShakeTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kUpdateCameraShakeRva;
        updateVisibleSetsTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kUpdateVisibleSetsRva;
        updateAnimationsTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kUpdateAnimationsRva;
        animatedMeshDrawTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kAnimatedMeshDrawRva;
        activeItemQueryInterfaceTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kActiveItemQueryInterfaceRva;
        if (!HasExpectedPrefix(
                updateCameraShakeTarget_,
                kUpdateCameraShakePrefix,
                sizeof(kUpdateCameraShakePrefix)) ||
            !HasExpectedPrefix(
                updateVisibleSetsTarget_,
                kUpdateVisibleSetsPrefix,
                sizeof(kUpdateVisibleSetsPrefix)) ||
            !HasExpectedPrefix(
                updateAnimationsTarget_,
                kUpdateAnimationsPrefix,
                sizeof(kUpdateAnimationsPrefix)) ||
            !HasExpectedPrefix(
                animatedMeshDrawTarget_,
                kAnimatedMeshDrawPrefix,
                sizeof(kAnimatedMeshDrawPrefix)) ||
            !HasExpectedPrefix(
                activeItemQueryInterfaceTarget_,
                kActiveItemQueryInterfacePrefix,
                sizeof(kActiveItemQueryInterfacePrefix)))
        {
            WriteLog(
                L"First-person-arm ownership probe rejected profiled targets updateCameraShake=%p updateVisibleSets=%p updateAnimations=%p animatedMeshDraw=%p activeItemQueryInterface=%p: one or more WinPC prefixes differ.",
                updateCameraShakeTarget_,
                updateVisibleSetsTarget_,
                updateAnimationsTarget_,
                animatedMeshDrawTarget_,
                activeItemQueryInterfaceTarget_);
            SignalCompletion();
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook_ = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"First-person-arm ownership probe could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            SignalCompletion();
            return;
        }

        const MH_STATUS createCameraShake = MH_CreateHook(
            updateCameraShakeTarget_,
            reinterpret_cast<LPVOID>(&BFSoldierFirstPersonArmProbe::UpdateCameraShakeHook),
            reinterpret_cast<LPVOID*>(&originalUpdateCameraShake_));
        const MH_STATUS createVisibleSets = createCameraShake == MH_OK
            ? MH_CreateHook(
                updateVisibleSetsTarget_,
                reinterpret_cast<LPVOID>(&BFSoldierFirstPersonArmProbe::UpdateVisibleSetsHook),
                reinterpret_cast<LPVOID*>(&originalUpdateVisibleSets_))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createUpdateAnimations = createVisibleSets == MH_OK
            ? MH_CreateHook(
                updateAnimationsTarget_,
                reinterpret_cast<LPVOID>(&BFSoldierFirstPersonArmProbe::UpdateAnimationsHook),
                reinterpret_cast<LPVOID*>(&originalUpdateAnimations_))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createAnimatedMeshDraw = createUpdateAnimations == MH_OK
            ? MH_CreateHook(
                animatedMeshDrawTarget_,
                reinterpret_cast<LPVOID>(&BFSoldierFirstPersonArmProbe::AnimatedMeshDrawHook),
                reinterpret_cast<LPVOID*>(&originalAnimatedMeshDraw_))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createActiveItemQueryInterface = createAnimatedMeshDraw == MH_OK
            ? MH_CreateHook(
                activeItemQueryInterfaceTarget_,
                reinterpret_cast<LPVOID>(&BFSoldierFirstPersonArmProbe::ActiveItemQueryInterfaceHook),
                reinterpret_cast<LPVOID*>(&originalActiveItemQueryInterface_))
            : MH_ERROR_NOT_CREATED;
        if (createCameraShake != MH_OK || createVisibleSets != MH_OK ||
            createUpdateAnimations != MH_OK ||
            createAnimatedMeshDraw != MH_OK ||
            createActiveItemQueryInterface != MH_OK ||
            originalUpdateCameraShake_ == nullptr ||
            originalUpdateVisibleSets_ == nullptr ||
            originalUpdateAnimations_ == nullptr ||
            originalAnimatedMeshDraw_ == nullptr ||
            originalActiveItemQueryInterface_ == nullptr)
        {
            WriteLog(
                L"First-person-arm ownership probe could not create forwarding hooks (cameraShake=%d visibleSets=%d updateAnimations=%d animatedMeshDraw=%d activeItemQueryInterface=%d).",
                static_cast<int>(createCameraShake),
                static_cast<int>(createVisibleSets),
                static_cast<int>(createUpdateAnimations),
                static_cast<int>(createAnimatedMeshDraw),
                static_cast<int>(createActiveItemQueryInterface));
            RemoveHooks();
            SignalCompletion();
            return;
        }
        cameraShakeHookCreated_ = true;
        visibleSetsHookCreated_ = true;
        updateAnimationsHookCreated_ = true;
        animatedMeshDrawHookCreated_ = true;
        activeItemQueryInterfaceHookCreated_ = true;

        active_ = this;
        const MH_STATUS enableCameraShake = MH_EnableHook(updateCameraShakeTarget_);
        const MH_STATUS enableVisibleSets = enableCameraShake == MH_OK
            ? MH_EnableHook(updateVisibleSetsTarget_)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableUpdateAnimations = enableVisibleSets == MH_OK
            ? MH_EnableHook(updateAnimationsTarget_)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableAnimatedMeshDraw = enableUpdateAnimations == MH_OK
            ? MH_EnableHook(animatedMeshDrawTarget_)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableActiveItemQueryInterface = enableAnimatedMeshDraw == MH_OK
            ? MH_EnableHook(activeItemQueryInterfaceTarget_)
            : MH_ERROR_ENABLED;
        if (enableCameraShake != MH_OK || enableVisibleSets != MH_OK ||
            enableUpdateAnimations != MH_OK ||
            enableAnimatedMeshDraw != MH_OK ||
            enableActiveItemQueryInterface != MH_OK)
        {
            WriteLog(
                L"First-person-arm ownership probe could not enable forwarding hooks (cameraShake=%d visibleSets=%d updateAnimations=%d animatedMeshDraw=%d activeItemQueryInterface=%d).",
                static_cast<int>(enableCameraShake),
                static_cast<int>(enableVisibleSets),
                static_cast<int>(enableUpdateAnimations),
                static_cast<int>(enableAnimatedMeshDraw),
                static_cast<int>(enableActiveItemQueryInterface));
            RemoveHooks();
            SignalCompletion();
            return;
        }
        cameraShakeHookEnabled_ = true;
        visibleSetsHookEnabled_ = true;
        updateAnimationsHookEnabled_ = true;
        animatedMeshDrawHookEnabled_ = true;
        activeItemQueryInterfaceHookEnabled_ = true;

        HANDLE worker = CreateThread(
            nullptr,
            0,
            &BFSoldierFirstPersonArmProbe::Run,
            this,
            0,
            nullptr);
        if (worker == nullptr)
        {
            WriteLog(
                L"First-person-arm ownership probe could not start its reporting worker (%lu).",
                GetLastError());
            RemoveHooks();
            SignalCompletion();
            return;
        }
        CloseHandle(worker);
        WriteLog(
            L"First-person-arm ownership probe enabled isolated forwarding hooks at 0x004FACD0, 0x004F7F20, 0x004FB150, 0x005AEEC0, and 0x0053B820. It records the game-selected local soldier/root bindings, one post-animation right-hand matrix, in-update active-item AnimatedBundle query candidates, and the existing narrow-projection AnimatedMesh draw instances through guarded reads; it invokes no game methods, uses no debug-register instructions, and writes no BF1942 data.");
    }

private:
    static void __fastcall UpdateCameraShakeHook(
        void* soldier,
        void*,
        float elapsedSeconds)
    {
        BFSoldierFirstPersonArmProbe* const probe = active_;
        if (probe == nullptr || probe->originalUpdateCameraShake_ == nullptr)
        {
            return;
        }
        InterlockedExchangePointer(&probe->lastCameraSoldier_, soldier);
        probe->originalUpdateCameraShake_(soldier, elapsedSeconds);
    }

    static void __fastcall UpdateVisibleSetsHook(void* soldier, void*)
    {
        BFSoldierFirstPersonArmProbe* const probe = active_;
        if (probe == nullptr || probe->originalUpdateVisibleSets_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&probe->activeCallbacks_);
        const bool captured = probe->CaptureLocalFirstPersonRoots(soldier);
        __try
        {
            probe->originalUpdateVisibleSets_(soldier);
        }
        __finally
        {
            if (captured)
            {
                probe->WriteLog(
                    L"First-person-arm ownership probe captured one local root set; it is beginning the bounded renderer-correlation interval.");
            }
            InterlockedDecrement(&probe->activeCallbacks_);
        }
    }

    static void __fastcall UpdateAnimationsHook(
        void* soldier,
        void*,
        float elapsedSeconds)
    {
        BFSoldierFirstPersonArmProbe* const probe = active_;
        if (probe == nullptr || probe->originalUpdateAnimations_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&probe->activeAnimationCallbacks_);
        const bool traceThisSoldier =
            soldier == probe->trace_.soldier &&
            InterlockedCompareExchange(&probe->rootTraceCaptured_, 0, 0) != 0 &&
            InterlockedCompareExchange(&probe->traceCompleted_, 0, 0) == 0;
        if (traceThisSoldier)
        {
            InterlockedIncrement(&probe->activeItemQueryScopeDepth_);
            InterlockedExchangePointer(&probe->activeItemQueryScopeSoldier_, soldier);
            probe->CaptureActiveItemLookupDispatch(
                static_cast<const std::byte*>(soldier));
        }
        __try
        {
            probe->originalUpdateAnimations_(soldier, elapsedSeconds);
        }
        __finally
        {
            if (traceThisSoldier &&
                InterlockedDecrement(&probe->activeItemQueryScopeDepth_) == 0)
            {
                InterlockedExchangePointer(&probe->activeItemQueryScopeSoldier_, nullptr);
            }
            probe->CapturePostAnimationRightHandPose(soldier);
            InterlockedDecrement(&probe->activeAnimationCallbacks_);
        }
    }

    static void* __fastcall ActiveItemQueryInterfaceHook(
        void* item,
        void*,
        DWORD interfaceId)
    {
        BFSoldierFirstPersonArmProbe* const probe = active_;
        if (probe == nullptr || probe->originalActiveItemQueryInterface_ == nullptr)
        {
            return nullptr;
        }
        InterlockedIncrement(&probe->activeActiveItemQueryCallbacks_);
        void* animatedBundleInterface = nullptr;
        __try
        {
            animatedBundleInterface = probe->originalActiveItemQueryInterface_(item, interfaceId);
            probe->CaptureActiveItemAnimatedBundleQuery(
                item,
                interfaceId,
                animatedBundleInterface);
        }
        __finally
        {
            InterlockedDecrement(&probe->activeActiveItemQueryCallbacks_);
        }
        return animatedBundleInterface;
    }

    static void __fastcall AnimatedMeshDrawHook(
        void* animatedMesh,
        void*,
        void* renderContext,
        float lodDistance)
    {
        BFSoldierFirstPersonArmProbe* const probe = active_;
        if (probe == nullptr || probe->originalAnimatedMeshDraw_ == nullptr)
        {
            return;
        }
        InterlockedIncrement(&probe->activeMeshCallbacks_);
        probe->CaptureNarrowProjectionAnimatedMesh(animatedMesh);
        __try
        {
            probe->originalAnimatedMeshDraw_(
                animatedMesh,
                renderContext,
                lodDistance);
        }
        __finally
        {
            InterlockedDecrement(&probe->activeMeshCallbacks_);
        }
    }

    static DWORD WINAPI Run(void* parameter)
    {
        auto* const probe = static_cast<BFSoldierFirstPersonArmProbe*>(parameter);
        if (probe != nullptr)
        {
            probe->WriteLog(
                L"First-person-arm ownership probe reporting worker started; it is awaiting a local first-person visibility traversal.");
            __try
            {
                probe->ReportUntilComplete();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                probe->WriteLog(
                    L"First-person-arm ownership probe reporting worker faulted with exception=0x%08lX; it is removing hooks without changing BF1942 state.",
                    static_cast<unsigned long>(GetExceptionCode()));
                probe->RemoveHooks();
                probe->SignalCompletion();
            }
        }
        return 0;
    }

    bool CaptureLocalFirstPersonRoots(void* soldier) noexcept
    {
        if (soldier == nullptr ||
            soldier != InterlockedCompareExchangePointer(&lastCameraSoldier_, nullptr, nullptr) ||
            InterlockedCompareExchange(&rootTraceCaptured_, 0, 0) != 0 ||
            InterlockedCompareExchange(&traceCompleted_, 0, 0) != 0 ||
            InterlockedCompareExchange(&captureInProgress_, 1, 0) != 0)
        {
            return false;
        }

        bool firstPerson = false;
        __try
        {
            firstPerson = *reinterpret_cast<const BYTE*>(
                static_cast<const std::byte*>(soldier) +
                kSoldierFirstPersonStateOffset) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedExchange(&captureInProgress_, 0);
            InterlockedIncrement(&trace_.readFailures);
            return false;
        }
        if (!firstPerson)
        {
            InterlockedExchange(&captureInProgress_, 0);
            return false;
        }

        trace_ = {};
        trace_.cameraSoldier =
            InterlockedCompareExchangePointer(&lastCameraSoldier_, nullptr, nullptr);
        trace_.soldier = soldier;
        trace_.threadId = GetCurrentThreadId();
        if (!CaptureTraceRoots())
        {
            InterlockedExchange(&captureInProgress_, 0);
            InterlockedIncrement(&trace_.readFailures);
            return false;
        }
        InterlockedExchange(
            &rootTraceCapturedAt_,
            static_cast<LONG>(GetTickCount()));
        InterlockedExchange(&rootTraceCaptured_, 1);
        InterlockedExchange(&captureInProgress_, 0);
        return true;
    }

    bool CaptureTraceRoots() noexcept
    {
        __try
        {
            const auto* const soldier = static_cast<const std::byte*>(trace_.soldier);
            trace_.firstPersonState = static_cast<DWORD>(
                std::to_integer<unsigned char>(
                    soldier[kSoldierFirstPersonStateOffset]));
            trace_.soldierTemplate = *reinterpret_cast<void* const*>(
                soldier + kSoldierTemplateOffset);
            trace_.animationSkeleton = *reinterpret_cast<void* const*>(
                soldier + kSoldierAnimationSkeletonOffset);
            trace_.collisionSkeleton = *reinterpret_cast<void* const*>(
                soldier + kSoldierCollisionSkeletonOffset);
            trace_.activeItemIndex = *reinterpret_cast<const LONG*>(
                soldier + kSoldierActiveItemIndexOffset);
            if (trace_.animationSkeleton != nullptr)
            {
                trace_.animationBoneRecords = *reinterpret_cast<void* const*>(
                    trace_.animationSkeleton);
            }
            if (trace_.soldierTemplate != nullptr)
            {
                const auto* const soldierTemplate =
                    static_cast<const std::byte*>(trace_.soldierTemplate);
                trace_.rightHandBoneIndex = *reinterpret_cast<const LONG*>(
                    soldierTemplate + kTemplateRightHandBoneOffset);
                const std::uintptr_t partBegin = reinterpret_cast<std::uintptr_t>(
                    *reinterpret_cast<void* const*>(
                        soldierTemplate + kTemplateFirstPersonPartBeginOffset));
                const std::uintptr_t partEnd = reinterpret_cast<std::uintptr_t>(
                    *reinterpret_cast<void* const*>(
                        soldierTemplate + kTemplateFirstPersonPartEndOffset));
                if (partEnd < partBegin ||
                    partEnd - partBegin > 256U * sizeof(LONG) * 2U)
                {
                    return false;
                }
            }
            CaptureChildBindingRecords(soldier);
            return trace_.soldierTemplate != nullptr &&
                trace_.animationSkeleton != nullptr &&
                trace_.animationBoneRecords != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void CaptureActiveItemLookupDispatch(const std::byte* soldier) noexcept
    {
        if (soldier == nullptr)
        {
            return;
        }
        if (InterlockedCompareExchange(
                &trace_.activeItemLookupDispatchAttempted,
                1,
                0) != 0)
        {
            return;
        }
        __try
        {
            ActiveItemLookupDispatchRecord record = {};
            record.lookupObject = const_cast<std::byte*>(soldier) + 0x11C;
            record.lookupObjectVtable = *reinterpret_cast<void* const*>(
                record.lookupObject);
            if (record.lookupObjectVtable == nullptr)
            {
                return;
            }
            record.lookupTarget = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(record.lookupObjectVtable) + 0x14);
            if (record.lookupTarget == nullptr)
            {
                return;
            }
            std::memcpy(
                record.lookupTargetPrefix.data(),
                record.lookupTarget,
                record.lookupTargetPrefix.size());
            record.readable = TRUE;
            trace_.activeItemLookupDispatch = record;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&trace_.readFailures);
        }
    }

    void CaptureChildBindingRecords(const std::byte* soldier) noexcept
    {
        if (soldier == nullptr)
        {
            return;
        }
        __try
        {
            const auto* const begin = *reinterpret_cast<const std::byte* const*>(
                soldier + kSoldierChildBindingBeginOffset);
            const auto* const end = *reinterpret_cast<const std::byte* const*>(
                soldier + kSoldierChildBindingEndOffset);
            trace_.childBindingBegin = const_cast<std::byte*>(begin);
            trace_.childBindingEnd = const_cast<std::byte*>(end);
            const std::uintptr_t beginAddress = reinterpret_cast<std::uintptr_t>(begin);
            const std::uintptr_t endAddress = reinterpret_cast<std::uintptr_t>(end);
            if (begin == nullptr || end == nullptr || endAddress < beginAddress)
            {
                InterlockedIncrement(&trace_.readFailures);
                return;
            }
            const std::size_t byteCount = static_cast<std::size_t>(
                endAddress - beginAddress);
            if (byteCount % kChildBindingRecordStride != 0 ||
                byteCount > kMaximumChildBindingRecords * kChildBindingRecordStride)
            {
                InterlockedIncrement(&trace_.readFailures);
                return;
            }

            for (std::size_t offset = 0; offset < byteCount;
                 offset += kChildBindingRecordStride)
            {
                const std::byte* const record = begin + offset;
                const LONG slot = InterlockedIncrement(&trace_.childBindingCount) - 1;
                if (slot < 0 || slot >= static_cast<LONG>(trace_.childBindings.size()))
                {
                    InterlockedExchange(&trace_.childBindingOverflow, 1);
                    return;
                }

                ChildBindingRecord& binding =
                    trace_.childBindings[static_cast<std::size_t>(slot)];
                binding.receiver = *reinterpret_cast<void* const*>(record + 4);
                binding.rootBoneIndex = *reinterpret_cast<const LONG*>(record + 8);
                binding.boneSpan = *reinterpret_cast<const LONG*>(record + 12);
                if (binding.receiver != nullptr)
                {
                    const auto* const receiver =
                        static_cast<const std::byte*>(binding.receiver);
                    binding.receiverVtable = *reinterpret_cast<void* const*>(receiver);
                    binding.receiverFlags = *reinterpret_cast<const DWORD*>(receiver + 4);
                }
                binding.readable = TRUE;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&trace_.readFailures);
        }
    }

    void CaptureNarrowProjectionAnimatedMesh(void* animatedMesh) noexcept
    {
        if (animatedMesh == nullptr ||
            InterlockedCompareExchange(&rootTraceCaptured_, 0, 0) == 0 ||
            InterlockedCompareExchange(&traceCompleted_, 0, 0) != 0)
        {
            return;
        }
        __try
        {
            const auto* const mesh = static_cast<const std::byte*>(animatedMesh);
            FirstPersonMeshRecord candidate = {};
            candidate.mesh = animatedMesh;
            candidate.meshTemplate = *reinterpret_cast<void* const*>(
                mesh + kAnimatedMeshTemplateOffset);
            candidate.candidateSkeleton = *reinterpret_cast<void* const*>(
                mesh + kAnimatedMeshSkeletonOffset);
            candidate.matchesLocalAnimationSkeleton =
                candidate.candidateSkeleton == trace_.animationSkeleton;
            candidate.matchesLocalCollisionSkeleton =
                candidate.candidateSkeleton == trace_.collisionSkeleton;
            if (candidate.meshTemplate != nullptr)
            {
                const auto* const templateStorage =
                    static_cast<const std::byte*>(candidate.meshTemplate) +
                    kAnimatedMeshTemplateNameOffset;
                candidate.templateNamePointer = *reinterpret_cast<void* const*>(
                    templateStorage);
                candidate.templateNameInlineReadable = CopyPrintableAsciiPreview(
                    reinterpret_cast<const char*>(templateStorage),
                    candidate.templateNameInlinePreview);
                candidate.templateNamePointerReadable = CopyPrintableAsciiPreview(
                    static_cast<const char*>(candidate.templateNamePointer),
                    candidate.templateNamePointerPreview);
            }
            candidate.fieldOfView = *reinterpret_cast<const float*>(
                mesh + kAnimatedMeshFieldOfViewOffset);
            std::memcpy(
                candidate.projection.data(),
                mesh + kAnimatedMeshProjectionOffset,
                sizeof(candidate.projection));
            candidate.threadId = GetCurrentThreadId();
            candidate.readable = TRUE;
            if (!std::isfinite(candidate.fieldOfView) ||
                !std::isfinite(candidate.projection[0]) ||
                !std::isfinite(candidate.projection[5]) ||
                candidate.projection[0] < 2.0F ||
                candidate.projection[5] < 3.5F)
            {
                return;
            }

            const LONG existingCount = InterlockedCompareExchange(
                &trace_.firstPersonMeshCount,
                0,
                0);
            const LONG boundedExistingCount =
                existingCount < static_cast<LONG>(trace_.firstPersonMeshes.size())
                ? existingCount
                : static_cast<LONG>(trace_.firstPersonMeshes.size());
            for (LONG index = 0; index < boundedExistingCount; ++index)
            {
                if (trace_.firstPersonMeshes[static_cast<std::size_t>(index)].mesh ==
                    candidate.mesh)
                {
                    return;
                }
            }

            const LONG slot = InterlockedIncrement(&trace_.firstPersonMeshCount) - 1;
            if (slot < 0 || slot >= static_cast<LONG>(trace_.firstPersonMeshes.size()))
            {
                InterlockedExchange(&trace_.firstPersonMeshOverflow, 1);
                return;
            }
            trace_.firstPersonMeshes[static_cast<std::size_t>(slot)] = candidate;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&trace_.readFailures);
        }
    }

    void CaptureActiveItemAnimatedBundleQuery(
        void* item,
        const DWORD interfaceId,
        void* animatedBundleInterface) noexcept
    {
        if (item == nullptr ||
            animatedBundleInterface == nullptr ||
            InterlockedCompareExchange(&rootTraceCaptured_, 0, 0) == 0 ||
            InterlockedCompareExchange(&traceCompleted_, 0, 0) != 0 ||
            InterlockedCompareExchangePointer(
                &activeItemQueryScopeSoldier_,
                nullptr,
                nullptr) != trace_.soldier ||
            GetCurrentThreadId() != trace_.threadId)
        {
            return;
        }
        __try
        {
            const LONG existingCount = InterlockedCompareExchange(
                &trace_.activeItemAnimatedBundleQueryCount,
                0,
                0);
            const LONG boundedExistingCount =
                existingCount < static_cast<LONG>(trace_.activeItemAnimatedBundleQueries.size())
                ? existingCount
                : static_cast<LONG>(trace_.activeItemAnimatedBundleQueries.size());
            for (LONG index = 0; index < boundedExistingCount; ++index)
            {
                const ActiveItemAnimatedBundleQueryRecord& existing =
                    trace_.activeItemAnimatedBundleQueries[static_cast<std::size_t>(index)];
                if (existing.item == item &&
                    existing.interfaceId == interfaceId &&
                    existing.animatedBundleInterface == animatedBundleInterface)
                {
                    return;
                }
            }

            const LONG slot = InterlockedIncrement(
                &trace_.activeItemAnimatedBundleQueryCount) - 1;
            if (slot < 0 ||
                slot >= static_cast<LONG>(trace_.activeItemAnimatedBundleQueries.size()))
            {
                InterlockedExchange(&trace_.activeItemAnimatedBundleQueryOverflow, 1);
                return;
            }

            ActiveItemAnimatedBundleQueryRecord record = {};
            record.item = item;
            record.itemVtable = *reinterpret_cast<void* const*>(item);
            record.interfaceId = interfaceId;
            record.animatedBundleInterface = animatedBundleInterface;
            record.animatedBundleInterfaceVtable = *reinterpret_cast<void* const*>(
                animatedBundleInterface);
            record.threadId = GetCurrentThreadId();
            record.readable = TRUE;
            trace_.activeItemAnimatedBundleQueries[static_cast<std::size_t>(slot)] = record;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&trace_.readFailures);
        }
    }

    void CapturePostAnimationRightHandPose(void* soldier) noexcept
    {
        if (soldier == nullptr ||
            soldier != trace_.soldier ||
            InterlockedCompareExchange(&rootTraceCaptured_, 0, 0) == 0 ||
            InterlockedCompareExchange(&traceCompleted_, 0, 0) != 0 ||
            InterlockedCompareExchange(&trace_.rightHandPoseCaptured, 1, 0) != 0)
        {
            return;
        }
        __try
        {
            const auto* const soldierBytes = static_cast<const std::byte*>(soldier);
            const void* const skeleton = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierAnimationSkeletonOffset);
            const void* const soldierTemplate = *reinterpret_cast<void* const*>(
                soldierBytes + kSoldierTemplateOffset);
            if (skeleton == nullptr || soldierTemplate == nullptr)
            {
                InterlockedExchange(&trace_.rightHandPoseCaptured, 0);
                return;
            }
            const LONG boneIndex = *reinterpret_cast<const LONG*>(
                static_cast<const std::byte*>(soldierTemplate) +
                kTemplateRightHandBoneOffset);
            const void* const boneRecords = *reinterpret_cast<void* const*>(skeleton);
            if (boneRecords == nullptr || boneIndex < 0 || boneIndex >= 256)
            {
                InterlockedExchange(&trace_.rightHandPoseCaptured, 0);
                return;
            }
            RightHandPoseRecord record = {};
            record.skeleton = const_cast<void*>(skeleton);
            record.boneIndex = boneIndex;
            std::memcpy(
                record.matrix.data(),
                static_cast<const std::byte*>(boneRecords) +
                    static_cast<std::size_t>(boneIndex) * 0xE8 + 0x48,
                sizeof(record.matrix));
            record.threadId = GetCurrentThreadId();
            record.readable = TRUE;
            for (const float value : record.matrix)
            {
                if (!std::isfinite(value))
                {
                    InterlockedExchange(&trace_.rightHandPoseCaptured, 0);
                    return;
                }
            }
            trace_.rightHandPose = record;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedExchange(&trace_.rightHandPoseCaptured, 0);
            InterlockedIncrement(&trace_.readFailures);
        }
    }

    void CompleteTrace()
    {
        if (InterlockedCompareExchange(&traceCompleted_, 1, 0) != 0)
        {
            return;
        }
        WriteLog(
            L"First-person-arm skeleton trace: cameraSoldier=%p soldier=%p thread=%lu firstPersonState=%lu template=%p skeletons=[animated=%p collision=%p] boneRecords=%p rightHandIndex=%ld activeItemIndex=%ld childBindings=[%p..%p count=%ld overflow=%ld] readFailures=%ld.",
            trace_.cameraSoldier,
            trace_.soldier,
            trace_.threadId,
            trace_.firstPersonState,
            trace_.soldierTemplate,
            trace_.animationSkeleton,
            trace_.collisionSkeleton,
            trace_.animationBoneRecords,
            trace_.rightHandBoneIndex,
            trace_.activeItemIndex,
            trace_.childBindingBegin,
            trace_.childBindingEnd,
            InterlockedCompareExchange(&trace_.childBindingCount, 0, 0),
            InterlockedCompareExchange(&trace_.childBindingOverflow, 0, 0),
            InterlockedCompareExchange(&trace_.readFailures, 0, 0));
        const ActiveItemLookupDispatchRecord& lookup = trace_.activeItemLookupDispatch;
        if (lookup.readable)
        {
            WriteLog(
                L"First-person-arm active-item lookup dispatch object=%p vtable=%p target=%p prefix=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X readable=%d.",
                lookup.lookupObject,
                lookup.lookupObjectVtable,
                lookup.lookupTarget,
                static_cast<unsigned int>(lookup.lookupTargetPrefix[0]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[1]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[2]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[3]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[4]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[5]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[6]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[7]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[8]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[9]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[10]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[11]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[12]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[13]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[14]),
                static_cast<unsigned int>(lookup.lookupTargetPrefix[15]),
                lookup.readable ? 1 : 0);
        }
        const LONG bindingCount = InterlockedCompareExchange(&trace_.childBindingCount, 0, 0);
        const LONG reportedCount = bindingCount < static_cast<LONG>(trace_.childBindings.size())
            ? bindingCount
            : static_cast<LONG>(trace_.childBindings.size());
        for (LONG index = 0; index < reportedCount; ++index)
        {
            const ChildBindingRecord& binding =
                trace_.childBindings[static_cast<std::size_t>(index)];
            WriteLog(
                L"First-person-arm child binding index=%ld receiver=%p vtable=%p flags=0x%08lX rootBone=%ld span=%ld readable=%d.",
                index,
                binding.receiver,
                binding.receiverVtable,
                static_cast<unsigned long>(binding.receiverFlags),
                binding.rootBoneIndex,
                binding.boneSpan,
                binding.readable ? 1 : 0);
        }
        const LONG meshCount = InterlockedCompareExchange(
            &trace_.firstPersonMeshCount,
            0,
            0);
        const LONG reportedMeshCount =
            meshCount < static_cast<LONG>(trace_.firstPersonMeshes.size())
            ? meshCount
            : static_cast<LONG>(trace_.firstPersonMeshes.size());
        for (LONG index = 0; index < reportedMeshCount; ++index)
        {
            const FirstPersonMeshRecord& mesh =
                trace_.firstPersonMeshes[static_cast<std::size_t>(index)];
            WriteLog(
                L"First-person-arm narrow AnimatedMesh index=%ld mesh=%p template=%p candidateSkeleton=%p matchesLocal=[animated=%d collision=%d] templateName=[storage=%p inline=%hs pointer=%p external=%hs] fov=%.6f projection=[m00=%.6f m11=%.6f] thread=%lu readable=%d.",
                index,
                mesh.mesh,
                mesh.meshTemplate,
                mesh.candidateSkeleton,
                mesh.matchesLocalAnimationSkeleton ? 1 : 0,
                mesh.matchesLocalCollisionSkeleton ? 1 : 0,
                static_cast<const void*>(mesh.templateNameInlinePreview.data()),
                mesh.templateNameInlineReadable
                    ? mesh.templateNameInlinePreview.data()
                    : "<unreadable>",
                mesh.templateNamePointer,
                mesh.templateNamePointerReadable
                    ? mesh.templateNamePointerPreview.data()
                    : "<unreadable>",
                static_cast<double>(mesh.fieldOfView),
                static_cast<double>(mesh.projection[0]),
                static_cast<double>(mesh.projection[5]),
                mesh.threadId,
                mesh.readable ? 1 : 0);
        }
        if (InterlockedCompareExchange(&trace_.rightHandPoseCaptured, 0, 0) != 0)
        {
            const RightHandPoseRecord& pose = trace_.rightHandPose;
            WriteLog(
                L"First-person-arm post-update right-hand matrix skeleton=%p boneIndex=%ld matrix=[%.6f %.6f %.6f %.6f | %.6f %.6f %.6f %.6f | %.6f %.6f %.6f %.6f | %.6f %.6f %.6f %.6f] thread=%lu readable=%d.",
                pose.skeleton,
                pose.boneIndex,
                static_cast<double>(pose.matrix[0]), static_cast<double>(pose.matrix[1]), static_cast<double>(pose.matrix[2]), static_cast<double>(pose.matrix[3]),
                static_cast<double>(pose.matrix[4]), static_cast<double>(pose.matrix[5]), static_cast<double>(pose.matrix[6]), static_cast<double>(pose.matrix[7]),
                static_cast<double>(pose.matrix[8]), static_cast<double>(pose.matrix[9]), static_cast<double>(pose.matrix[10]), static_cast<double>(pose.matrix[11]),
                static_cast<double>(pose.matrix[12]), static_cast<double>(pose.matrix[13]), static_cast<double>(pose.matrix[14]), static_cast<double>(pose.matrix[15]),
                pose.threadId,
                pose.readable ? 1 : 0);
        }
        const LONG activeItemQueryCount = InterlockedCompareExchange(
            &trace_.activeItemAnimatedBundleQueryCount,
            0,
            0);
        const LONG reportedActiveItemQueryCount =
            activeItemQueryCount <
                    static_cast<LONG>(trace_.activeItemAnimatedBundleQueries.size())
            ? activeItemQueryCount
            : static_cast<LONG>(trace_.activeItemAnimatedBundleQueries.size());
        for (LONG index = 0; index < reportedActiveItemQueryCount; ++index)
        {
            const ActiveItemAnimatedBundleQueryRecord& query =
                trace_.activeItemAnimatedBundleQueries[static_cast<std::size_t>(index)];
            WriteLog(
                L"First-person-arm in-update item AnimatedBundle query index=%ld item=%p itemVtable=%p interfaceId=0x%08lX interface=%p interfaceVtable=%p thread=%lu readable=%d.",
                index,
                query.item,
                query.itemVtable,
                static_cast<unsigned long>(query.interfaceId),
                query.animatedBundleInterface,
                query.animatedBundleInterfaceVtable,
                query.threadId,
                query.readable ? 1 : 0);
        }
        WriteLog(
            L"This safe trace correlates native local-soldier roots with narrow-projection AnimatedMesh instances and records in-update AnimatedBundle query candidates, but does not call a receiver, prove a concrete item class, or authorize a pose change.");
    }

    void ReportUntilComplete()
    {
        const DWORD startedAt = GetTickCount();
        DWORD nextHeartbeatAt = startedAt + kWorkerHeartbeatIntervalMs;
        for (;;)
        {
            Sleep(kReportIntervalMs);
            const DWORD now = GetTickCount();
            const LONG rootCaptured = InterlockedCompareExchange(
                &rootTraceCaptured_,
                0,
                0);
            const DWORD rootCapturedAt = static_cast<DWORD>(
                InterlockedCompareExchange(&rootTraceCapturedAt_, 0, 0));
            if (rootCaptured != 0 &&
                now - rootCapturedAt >= kMeshCorrelationWindowMs)
            {
                CompleteTrace();
            }
            else if (rootCaptured == 0 &&
                     now - startedAt >= kObservationWindowMs)
            {
                WriteLog(
                    L"First-person-arm ownership probe timed out without a local first-person visibility traversal. It is removing its forwarding hooks without changing BF1942 state.");
                RemoveHooks();
                SignalCompletion();
                return;
            }
            if (now - nextHeartbeatAt < 0x80000000UL)
            {
                WriteLog(
                    L"First-person-arm ownership probe reporting worker is still awaiting local roots after %lu ms.",
                    static_cast<unsigned long>(now - startedAt));
                nextHeartbeatAt += kWorkerHeartbeatIntervalMs;
            }
            if (InterlockedCompareExchange(&traceCompleted_, 0, 0) != 0 &&
                InterlockedCompareExchange(&activeCallbacks_, 0, 0) == 0 &&
                InterlockedCompareExchange(&activeAnimationCallbacks_, 0, 0) == 0 &&
                InterlockedCompareExchange(&activeMeshCallbacks_, 0, 0) == 0 &&
                InterlockedCompareExchange(&activeActiveItemQueryCallbacks_, 0, 0) == 0)
            {
                WriteLog(
                    L"First-person-arm ownership probe completed its local root/mesh correlation window and is removing its forwarding hooks.");
                RemoveHooks();
                SignalCompletion();
                return;
            }
        }
    }

    void SignalCompletion() const
    {
        if (signalCompletion_ != nullptr)
        {
            signalCompletion_();
        }
    }

    void RemoveHooks()
    {
        if (activeItemQueryInterfaceHookEnabled_)
        {
            MH_DisableHook(activeItemQueryInterfaceTarget_);
            activeItemQueryInterfaceHookEnabled_ = false;
        }
        if (animatedMeshDrawHookEnabled_)
        {
            MH_DisableHook(animatedMeshDrawTarget_);
            animatedMeshDrawHookEnabled_ = false;
        }
        if (visibleSetsHookEnabled_)
        {
            MH_DisableHook(updateVisibleSetsTarget_);
            visibleSetsHookEnabled_ = false;
        }
        if (updateAnimationsHookEnabled_)
        {
            MH_DisableHook(updateAnimationsTarget_);
            updateAnimationsHookEnabled_ = false;
        }
        if (cameraShakeHookEnabled_)
        {
            MH_DisableHook(updateCameraShakeTarget_);
            cameraShakeHookEnabled_ = false;
        }
        if (visibleSetsHookCreated_)
        {
            MH_RemoveHook(updateVisibleSetsTarget_);
            visibleSetsHookCreated_ = false;
        }
        if (cameraShakeHookCreated_)
        {
            MH_RemoveHook(updateCameraShakeTarget_);
            cameraShakeHookCreated_ = false;
        }
        if (updateAnimationsHookCreated_)
        {
            MH_RemoveHook(updateAnimationsTarget_);
            updateAnimationsHookCreated_ = false;
        }
        if (animatedMeshDrawHookCreated_)
        {
            MH_RemoveHook(animatedMeshDrawTarget_);
            animatedMeshDrawHookCreated_ = false;
        }
        if (activeItemQueryInterfaceHookCreated_)
        {
            MH_RemoveHook(activeItemQueryInterfaceTarget_);
            activeItemQueryInterfaceHookCreated_ = false;
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
        originalUpdateCameraShake_ = nullptr;
        originalUpdateVisibleSets_ = nullptr;
        originalUpdateAnimations_ = nullptr;
        originalAnimatedMeshDraw_ = nullptr;
        originalActiveItemQueryInterface_ = nullptr;
        if (ownsMinHook_)
        {
            MH_Uninitialize();
            ownsMinHook_ = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog_ == nullptr)
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
        appendLog_(message.data());
    }

    static BFSoldierFirstPersonArmProbe* active_;

    std::byte* gameImage_ = nullptr;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    void (*signalCompletion_)() = nullptr;
    void* updateCameraShakeTarget_ = nullptr;
    void* updateVisibleSetsTarget_ = nullptr;
    void* updateAnimationsTarget_ = nullptr;
    void* animatedMeshDrawTarget_ = nullptr;
    void* activeItemQueryInterfaceTarget_ = nullptr;
    UpdateCameraShakeFn originalUpdateCameraShake_ = nullptr;
    UpdateVisibleSetsFn originalUpdateVisibleSets_ = nullptr;
    UpdateAnimationsFn originalUpdateAnimations_ = nullptr;
    AnimatedMeshDrawFn originalAnimatedMeshDraw_ = nullptr;
    ActiveItemQueryInterfaceFn originalActiveItemQueryInterface_ = nullptr;
    volatile PVOID lastCameraSoldier_ = nullptr;
    volatile PVOID activeItemQueryScopeSoldier_ = nullptr;
    volatile LONG started_ = 0;
    volatile LONG activeCallbacks_ = 0;
    volatile LONG activeAnimationCallbacks_ = 0;
    volatile LONG activeMeshCallbacks_ = 0;
    volatile LONG activeActiveItemQueryCallbacks_ = 0;
    volatile LONG activeItemQueryScopeDepth_ = 0;
    volatile LONG captureInProgress_ = 0;
    volatile LONG rootTraceCaptured_ = 0;
    volatile LONG rootTraceCapturedAt_ = 0;
    volatile LONG traceCompleted_ = 0;
    FirstPersonArmTrace trace_ = {};
    bool ownsMinHook_ = false;
    bool cameraShakeHookCreated_ = false;
    bool visibleSetsHookCreated_ = false;
    bool updateAnimationsHookCreated_ = false;
    bool animatedMeshDrawHookCreated_ = false;
    bool activeItemQueryInterfaceHookCreated_ = false;
    bool cameraShakeHookEnabled_ = false;
    bool visibleSetsHookEnabled_ = false;
    bool updateAnimationsHookEnabled_ = false;
    bool animatedMeshDrawHookEnabled_ = false;
    bool activeItemQueryInterfaceHookEnabled_ = false;
};

BFSoldierFirstPersonArmProbe* BFSoldierFirstPersonArmProbe::active_ = nullptr;
BFSoldierFirstPersonArmProbe g_firstPersonArmProbe = {};

} // namespace

namespace bfvr
{

void StartBFSoldierFirstPersonArmProbe(
    void* gameImage,
    void (*appendLog)(const wchar_t* message),
    void (*signalCompletion)())
{
    g_firstPersonArmProbe.Start(gameImage, appendLog, signalCompletion);
}

} // namespace bfvr
