#include "client/KillSoundEventHook.h"

#include "client/ControllerHaptics.h"
#include "client/KillSoundEventPolicy.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::ptrdiff_t kGameClientHandleEventRva = 0x000933D0;
constexpr std::ptrdiff_t kGameClientFindPlayerRva = 0x00091980;
constexpr std::ptrdiff_t kGameServerHandleScoreRva = 0x000AD2D0;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kGameClientLocalPlayerOffset = 0x170;
constexpr std::size_t kPlayerManagerCurrentPlayerVtableOffset = 0x20;
constexpr std::size_t kScoreMessageSubtypeOffset = 0x0C;
constexpr std::size_t kScoreMessageKillerIdOffset = 0x10;
constexpr std::size_t kScoreMessageVictimIdOffset = 0x11;
constexpr ULONGLONG kCrossSourceDuplicateWindowMs = 2000;
constexpr ULONGLONG kMultiKillBurstWindowMs = 300;

constexpr BYTE kGameClientHandleEventPrefix[] = {
    0x81, 0xEC, 0x34, 0x0F, 0x00, 0x00, 0x53, 0x55,
    0x56, 0x8B, 0xB4, 0x24, 0x44, 0x0F, 0x00, 0x00,
    0x8B, 0x06, 0x57, 0x33, 0xDB, 0x53, 0x8B, 0xF9};
constexpr BYTE kGameClientFindPlayerPrefix[] = {
    0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x70, 0x01,
    0x00, 0x00, 0x85, 0xC9, 0x74, 0x19, 0x8B, 0x01,
    0xFF, 0x50, 0x44, 0x0F, 0xB6, 0x4C, 0x24, 0x0C};
constexpr BYTE kGameServerHandleScorePrefix[] = {
    0xA1, 0xAC, 0x1E, 0x97, 0x00, 0x81, 0xEC, 0x00,
    0x01, 0x00, 0x00, 0x53, 0x8B, 0x9C, 0x24, 0x10,
    0x01, 0x00, 0x00, 0x55, 0x8B, 0xA8, 0x1C, 0x0A};

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t length) noexcept
{
    if (target == nullptr || expected == nullptr || length == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, length) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

class KillSoundEventHook
{
public:
    using HandleEventFn =
        unsigned int(__thiscall*)(void* gameClient, void* event);
    using FindPlayerFn =
        void*(__thiscall*)(void* gameClient, unsigned char playerId);
    using HandleScoreFn = void(__thiscall*)(
        void* gameServer,
        void* scoringPlayer,
        std::int32_t scoreSubtype,
        void* victimPlayer,
        std::uint32_t weaponTemplateId,
        std::uint32_t scoreDetail);
    using EventTypeFn = unsigned int(__thiscall*)(void* event);
    using CurrentPlayerFn = void*(__thiscall*)(void* playerManager);

    void Start(
        void* image,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return;
        }
        appendLog_ = log;
        gameImage_ = static_cast<std::byte*>(image);
        handleEventTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kGameClientHandleEventRva;
        handleScoreTarget_ = gameImage_ == nullptr
            ? nullptr
            : gameImage_ + kGameServerHandleScoreRva;
        findPlayer_ = gameImage_ == nullptr
            ? nullptr
            : reinterpret_cast<FindPlayerFn>(
                gameImage_ + kGameClientFindPlayerRva);
        if (!HasExpectedPrefix(
                handleEventTarget_,
                kGameClientHandleEventPrefix,
                sizeof(kGameClientHandleEventPrefix)) ||
            !HasExpectedPrefix(
                reinterpret_cast<const void*>(findPlayer_),
                kGameClientFindPlayerPrefix,
                sizeof(kGameClientFindPlayerPrefix)) ||
            !HasExpectedPrefix(
                handleScoreTarget_,
                kGameServerHandleScorePrefix,
                sizeof(kGameServerHandleScorePrefix)))
        {
            WriteLog(
                L"Kill sound disabled because a profiled score target did not match: clientHandler=%p findPlayer=%p serverHandler=%p.",
                handleEventTarget_,
                reinterpret_cast<void*>(findPlayer_),
                handleScoreTarget_);
            ResetTargets();
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS clientCreateStatus = MH_CreateHook(
            handleEventTarget_,
            reinterpret_cast<LPVOID>(&KillSoundEventHook::HandleEventHook),
            reinterpret_cast<LPVOID*>(&originalHandleEvent_));
        if (clientCreateStatus != MH_OK || originalHandleEvent_ == nullptr)
        {
            WriteLog(
                L"Kill sound could not create its forwarding GameClient event hook (status=%d).",
                static_cast<int>(clientCreateStatus));
            ResetTargets();
            InterlockedExchange(&started_, 0);
            return;
        }
        clientHookCreated_ = true;

        const MH_STATUS serverCreateStatus = MH_CreateHook(
            handleScoreTarget_,
            reinterpret_cast<LPVOID>(&KillSoundEventHook::HandleScoreHook),
            reinterpret_cast<LPVOID*>(&originalHandleScore_));
        if (serverCreateStatus != MH_OK || originalHandleScore_ == nullptr)
        {
            WriteLog(
                L"Kill sound could not create its forwarding GameServer score hook (status=%d).",
                static_cast<int>(serverCreateStatus));
            RemoveCreatedHooks();
            ResetTargets();
            InterlockedExchange(&started_, 0);
            return;
        }
        serverHookCreated_ = true;

        InterlockedExchangePointer(&active_, this);
        const MH_STATUS clientEnableStatus = MH_EnableHook(handleEventTarget_);
        if (clientEnableStatus == MH_OK)
        {
            clientHookEnabled_ = true;
        }
        const MH_STATUS serverEnableStatus = clientHookEnabled_
            ? MH_EnableHook(handleScoreTarget_)
            : MH_ERROR_DISABLED;
        if (serverEnableStatus == MH_OK)
        {
            serverHookEnabled_ = true;
        }
        if (!clientHookEnabled_ || !serverHookEnabled_)
        {
            WriteLog(
                L"Kill sound could not enable both forwarding score hooks (clientStatus=%d serverStatus=%d).",
                static_cast<int>(clientEnableStatus),
                static_cast<int>(serverEnableStatus));
            DisableEnabledHooks();
            InterlockedCompareExchangePointer(&active_, nullptr, this);
            RemoveCreatedHooks();
            ResetTargets();
            InterlockedExchange(&started_, 0);
            return;
        }

        WriteLog(
            L"Confirmed local-kill observers armed at GameClient::handleGameEventManagerEvent 0x004933D0 and GameServer::handleScore 0x004AD2D0. The client path preserves confirmed MP behavior; the server path covers SP/listen-server ordinary kill type 3 and rejects teamkill type 6. Both use PlayerManager current-player identity, forward native handlers unchanged, suppress identical cross-source duplicates, and collapse confirmed kills within 300 ms into one audible multi-kill burst.");
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return;
        }
        DisableEnabledHooks();
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        RemoveCreatedHooks();
        WriteLog(
            L"Confirmed local-kill observer report: clientEvents=%ld clientKills=%ld serverScores=%ld serverKills=%ld published=%ld multiKillBurstSuppressions=%ld crossSourceDuplicates=%ld playerManagerMatches=%ld gameClientFallbackMatches=%ld inspectionFaults=%ld.",
            observedClientEvents_,
            confirmedClientKills_,
            observedServerScores_,
            confirmedServerKills_,
            publishedKills_,
            multiKillBurstSuppressions_,
            crossSourceDuplicates_,
            playerManagerMatches_,
            gameClientFallbackMatches_,
            inspectionFaults_);
        ResetTargets();
        InterlockedExchange(&started_, 0);
    }

private:
    static unsigned int __fastcall HandleEventHook(
        void* gameClient,
        void*,
        void* event)
    {
        InterlockedIncrement(&callbackEntrants_);
        KillSoundEventHook* const hook = ActiveHook();
        if (hook == nullptr || hook->originalHandleEvent_ == nullptr)
        {
            InterlockedDecrement(&callbackEntrants_);
            return 0;
        }
        InterlockedIncrement(&hook->observedClientEvents_);
        void* killerPlayer = nullptr;
        void* victimPlayer = nullptr;
        const bool localKill = hook->IsConfirmedClientLocalKill(
            gameClient,
            event,
            killerPlayer,
            victimPlayer);
        const unsigned int result =
            hook->originalHandleEvent_(gameClient, event);
        if (localKill)
        {
            InterlockedIncrement(&hook->confirmedClientKills_);
            hook->PublishKill(
                bfvr::KillSoundSource::ClientEvent,
                killerPlayer,
                victimPlayer);
        }
        InterlockedDecrement(&callbackEntrants_);
        return result;
    }

    static void __fastcall HandleScoreHook(
        void* gameServer,
        void*,
        void* scoringPlayer,
        std::int32_t scoreSubtype,
        void* victimPlayer,
        std::uint32_t weaponTemplateId,
        std::uint32_t scoreDetail)
    {
        InterlockedIncrement(&callbackEntrants_);
        KillSoundEventHook* const hook = ActiveHook();
        if (hook == nullptr || hook->originalHandleScore_ == nullptr)
        {
            InterlockedDecrement(&callbackEntrants_);
            return;
        }
        InterlockedIncrement(&hook->observedServerScores_);
        const bool localKill = hook->IsConfirmedServerLocalKill(
            scoreSubtype,
            scoringPlayer,
            victimPlayer);
        hook->originalHandleScore_(
            gameServer,
            scoringPlayer,
            scoreSubtype,
            victimPlayer,
            weaponTemplateId,
            scoreDetail);
        if (localKill)
        {
            InterlockedIncrement(&hook->confirmedServerKills_);
            hook->PublishKill(
                bfvr::KillSoundSource::ServerScore,
                scoringPlayer,
                victimPlayer);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    static KillSoundEventHook* ActiveHook() noexcept
    {
        return static_cast<KillSoundEventHook*>(
            InterlockedCompareExchangePointer(&active_, nullptr, nullptr));
    }

    void* CurrentPlayer() const
    {
        if (gameImage_ == nullptr)
        {
            return nullptr;
        }
        void* const playerManager = *reinterpret_cast<void**>(
            gameImage_ + kPlayerManagerGlobalRva);
        if (playerManager == nullptr)
        {
            return nullptr;
        }
        void** const vtable = *reinterpret_cast<void***>(playerManager);
        const std::size_t currentPlayerIndex =
            kPlayerManagerCurrentPlayerVtableOffset / sizeof(void*);
        if (vtable == nullptr || vtable[currentPlayerIndex] == nullptr)
        {
            return nullptr;
        }
        return reinterpret_cast<CurrentPlayerFn>(vtable[currentPlayerIndex])(
            playerManager);
    }

    bool IsConfirmedClientLocalKill(
        void* gameClient,
        void* event,
        void*& killerPlayer,
        void*& victimPlayer) noexcept
    {
        bool confirmed = false;
        killerPlayer = nullptr;
        victimPlayer = nullptr;
        __try
        {
            if (gameClient == nullptr || event == nullptr ||
                findPlayer_ == nullptr)
            {
                return false;
            }
            void** const vtable = *reinterpret_cast<void***>(event);
            if (vtable == nullptr || vtable[0] == nullptr)
            {
                return false;
            }
            const auto eventType =
                reinterpret_cast<EventTypeFn>(vtable[0])(event);
            if (eventType != bfvr::kGameEventTypeScoreMessage)
            {
                return false;
            }
            const auto* const bytes = static_cast<const std::byte*>(event);
            const std::int32_t subtype = *reinterpret_cast<const std::int32_t*>(
                bytes + kScoreMessageSubtypeOffset);
            if (subtype != bfvr::kScoreMessageSubtypeKill)
            {
                return false;
            }
            const std::uint8_t killerId = *reinterpret_cast<const std::uint8_t*>(
                bytes + kScoreMessageKillerIdOffset);
            const std::uint8_t victimId = *reinterpret_cast<const std::uint8_t*>(
                bytes + kScoreMessageVictimIdOffset);
            void* const gameClientLocalPlayer = *reinterpret_cast<void**>(
                static_cast<std::byte*>(gameClient) +
                kGameClientLocalPlayerOffset);
            void* const playerManagerCurrentPlayer = CurrentPlayer();
            killerPlayer = findPlayer_(gameClient, killerId);
            victimPlayer = findPlayer_(gameClient, victimId);
            confirmed = bfvr::ShouldPlayLocalKillSound(
                eventType,
                subtype,
                killerId,
                victimId,
                killerPlayer,
                playerManagerCurrentPlayer,
                gameClientLocalPlayer);
            if (confirmed)
            {
                if (killerPlayer == playerManagerCurrentPlayer)
                {
                    InterlockedIncrement(&playerManagerMatches_);
                }
                else
                {
                    InterlockedIncrement(&gameClientFallbackMatches_);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&inspectionFaults_);
            confirmed = false;
            killerPlayer = nullptr;
            victimPlayer = nullptr;
        }
        return confirmed;
    }

    bool IsConfirmedServerLocalKill(
        std::int32_t scoreSubtype,
        void* scoringPlayer,
        void* victimPlayer) noexcept
    {
        bool confirmed = false;
        __try
        {
            confirmed = bfvr::ShouldPlayServerLocalKillSound(
                scoreSubtype,
                scoringPlayer,
                victimPlayer,
                CurrentPlayer());
            if (confirmed)
            {
                InterlockedIncrement(&playerManagerMatches_);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&inspectionFaults_);
            confirmed = false;
        }
        return confirmed;
    }

    void PublishKill(
        bfvr::KillSoundSource source,
        void* killerPlayer,
        void* victimPlayer)
    {
        const ULONGLONG now = GetTickCount64();
        while (InterlockedCompareExchange(&publicationLock_, 1, 0) != 0)
        {
            Sleep(0);
        }
        const bool duplicate = bfvr::ShouldSuppressCrossSourceKillSound(
            source,
            killerPlayer,
            victimPlayer,
            now,
            lastPublishedSource_,
            lastPublishedKiller_,
            lastPublishedVictim_,
            lastPublishedTick_,
            kCrossSourceDuplicateWindowMs);
        bool multiKillBurst = false;
        if (!duplicate)
        {
            lastPublishedSource_ = source;
            lastPublishedKiller_ = killerPlayer;
            lastPublishedVictim_ = victimPlayer;
            lastPublishedTick_ = now;
            multiKillBurst = bfvr::ShouldSuppressKillSoundBurst(
                now,
                hasAudibleKill_,
                lastAudibleKillTick_,
                kMultiKillBurstWindowMs);
            if (!multiKillBurst)
            {
                hasAudibleKill_ = true;
                lastAudibleKillTick_ = now;
            }
        }
        InterlockedExchange(&publicationLock_, 0);

        if (duplicate)
        {
            InterlockedIncrement(&crossSourceDuplicates_);
            return;
        }
        if (multiKillBurst)
        {
            InterlockedIncrement(&multiKillBurstSuppressions_);
            return;
        }
        bfvr::NotifyLocalPlayerKillSound();
        InterlockedIncrement(&publishedKills_);
    }

    void DisableEnabledHooks()
    {
        if (serverHookEnabled_)
        {
            MH_DisableHook(handleScoreTarget_);
            serverHookEnabled_ = false;
        }
        if (clientHookEnabled_)
        {
            MH_DisableHook(handleEventTarget_);
            clientHookEnabled_ = false;
        }
    }

    void RemoveCreatedHooks()
    {
        if (serverHookCreated_)
        {
            MH_RemoveHook(handleScoreTarget_);
            serverHookCreated_ = false;
        }
        if (clientHookCreated_)
        {
            MH_RemoveHook(handleEventTarget_);
            clientHookCreated_ = false;
        }
        originalHandleScore_ = nullptr;
        originalHandleEvent_ = nullptr;
    }

    void ResetTargets()
    {
        gameImage_ = nullptr;
        handleEventTarget_ = nullptr;
        handleScoreTarget_ = nullptr;
        findPlayer_ = nullptr;
        lastPublishedSource_ = bfvr::KillSoundSource::None;
        lastPublishedKiller_ = nullptr;
        lastPublishedVictim_ = nullptr;
        lastPublishedTick_ = 0;
        hasAudibleKill_ = false;
        lastAudibleKillTick_ = 0;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog_ == nullptr || format == nullptr)
        {
            return;
        }
        std::array<wchar_t, 1000> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(), message.size(), _TRUNCATE, format, arguments);
        va_end(arguments);
        appendLog_(message.data());
    }

    inline static PVOID volatile active_ = nullptr;
    inline static volatile LONG callbackEntrants_ = 0;
    std::byte* gameImage_ = nullptr;
    void* handleEventTarget_ = nullptr;
    void* handleScoreTarget_ = nullptr;
    HandleEventFn originalHandleEvent_ = nullptr;
    HandleScoreFn originalHandleScore_ = nullptr;
    FindPlayerFn findPlayer_ = nullptr;
    void (*appendLog_)(const wchar_t* message) = nullptr;
    volatile LONG started_ = 0;
    volatile LONG observedClientEvents_ = 0;
    volatile LONG confirmedClientKills_ = 0;
    volatile LONG observedServerScores_ = 0;
    volatile LONG confirmedServerKills_ = 0;
    volatile LONG publishedKills_ = 0;
    volatile LONG multiKillBurstSuppressions_ = 0;
    volatile LONG crossSourceDuplicates_ = 0;
    volatile LONG playerManagerMatches_ = 0;
    volatile LONG gameClientFallbackMatches_ = 0;
    volatile LONG inspectionFaults_ = 0;
    volatile LONG publicationLock_ = 0;
    bfvr::KillSoundSource lastPublishedSource_ =
        bfvr::KillSoundSource::None;
    void* lastPublishedKiller_ = nullptr;
    void* lastPublishedVictim_ = nullptr;
    ULONGLONG lastPublishedTick_ = 0;
    bool hasAudibleKill_ = false;
    ULONGLONG lastAudibleKillTick_ = 0;
    bool clientHookCreated_ = false;
    bool clientHookEnabled_ = false;
    bool serverHookCreated_ = false;
    bool serverHookEnabled_ = false;
};

KillSoundEventHook g_hook;
} // namespace

namespace bfvr
{

void StartKillSoundEventHook(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_hook.Start(gameImage, appendLog);
}

void StopKillSoundEventHook()
{
    g_hook.Stop();
}

} // namespace bfvr
