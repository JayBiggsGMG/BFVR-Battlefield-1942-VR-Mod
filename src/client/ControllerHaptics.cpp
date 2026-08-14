#include "client/ControllerHaptics.h"

#include "client/ScopedOffHandSupportPoseCache.h"
#include "presenter/SharedPresentationProtocol.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>

namespace
{
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr DWORD kSupportMaximumAgeMs = 150;

PVOID volatile g_controlBlock = nullptr;
void* g_observedLocalPlayer = nullptr;
bool g_previousAlive = false;
bool g_aliveStateKnown = false;
LONG g_consumedMenuHighlightSequence = 0;
LONG g_consumedMenuOkSequence = 0;
LONG g_consumedMenuCancelSequence = 0;

bfvr::shared::ControlBlock* CurrentControlBlock() noexcept
{
    return static_cast<bfvr::shared::ControlBlock*>(
        InterlockedCompareExchangePointer(&g_controlBlock, nullptr, nullptr));
}
} // namespace

namespace bfvr
{

void RegisterControllerHapticTransport(
    shared::ControlBlock* controlBlock) noexcept
{
    if (controlBlock != nullptr)
    {
        InterlockedExchange(
            &controlBlock->localPlayerLifeState,
            static_cast<LONG>(shared::LocalPlayerLifeState::Unknown));
        g_consumedMenuHighlightSequence = InterlockedCompareExchange(
            &controlBlock->nativeMenuSoundHighlightSequence, 0, 0);
        g_consumedMenuOkSequence = InterlockedCompareExchange(
            &controlBlock->nativeMenuSoundOkSequence, 0, 0);
        g_consumedMenuCancelSequence = InterlockedCompareExchange(
            &controlBlock->nativeMenuSoundCancelSequence, 0, 0);
    }
    else
    {
        g_consumedMenuHighlightSequence = 0;
        g_consumedMenuOkSequence = 0;
        g_consumedMenuCancelSequence = 0;
    }
    InterlockedExchangePointer(&g_controlBlock, controlBlock);
    g_observedLocalPlayer = nullptr;
    g_previousAlive = false;
    g_aliveStateKnown = false;
}

void NotifyControllerWeaponFired() noexcept
{
    shared::ControlBlock* const block = CurrentControlBlock();
    if (block == nullptr)
    {
        return;
    }
    const bool bothHands = IsFreshCurrentOffHandSupportHeld(
        kSupportMaximumAgeMs);
    InterlockedIncrement(
        bothHands
            ? &block->hapticShotBothSequence
            : &block->hapticShotRightSequence);
}

void NotifyControllerNativeMenuHover() noexcept
{
    if (shared::ControlBlock* const block = CurrentControlBlock())
    {
        InterlockedIncrement(&block->hapticNativeMenuHoverSequence);
    }
}

void NotifyLocalPlayerKillSound() noexcept
{
    if (shared::ControlBlock* const block = CurrentControlBlock())
    {
        InterlockedIncrement(&block->killSoundSequence);
    }
}

NativeMenuSoundRequests ConsumeNativeMenuSoundRequests() noexcept
{
    NativeMenuSoundRequests result = {};
    shared::ControlBlock* const block = CurrentControlBlock();
    if (block == nullptr)
    {
        return result;
    }
    const auto consume = [](volatile LONG* source, LONG& consumed) {
        const LONG available = InterlockedCompareExchange(source, 0, 0);
        const ULONG pending = (std::min)(
            static_cast<ULONG>(available) -
                static_cast<ULONG>(consumed),
            64UL);
        consumed = available;
        return static_cast<std::uint32_t>(pending);
    };
    result.highlight = consume(
        &block->nativeMenuSoundHighlightSequence,
        g_consumedMenuHighlightSequence);
    result.ok = consume(
        &block->nativeMenuSoundOkSequence,
        g_consumedMenuOkSequence);
    result.cancel = consume(
        &block->nativeMenuSoundCancelSequence,
        g_consumedMenuCancelSequence);
    return result;
}

void PollControllerHapticDeath(void* gameImage) noexcept
{
    if (gameImage == nullptr)
    {
        return;
    }
    void* localPlayer = nullptr;
    bool alive = false;
    bool readable = false;
    __try
    {
        auto* const manager = *reinterpret_cast<void**>(
            static_cast<std::byte*>(gameImage) +
            kPlayerManagerGlobalRva);
        if (manager != nullptr)
        {
            localPlayer = *reinterpret_cast<void**>(
                static_cast<std::byte*>(manager) +
                kPlayerManagerLocalPlayerOffset);
            if (localPlayer != nullptr)
            {
                alive = *(static_cast<const BYTE*>(localPlayer) +
                    kBFPlayerIsAliveOffset) != 0;
                readable = true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        readable = false;
    }

    if (!readable)
    {
        if (shared::ControlBlock* const block = CurrentControlBlock())
        {
            InterlockedExchange(
                &block->localPlayerLifeState,
                static_cast<LONG>(shared::LocalPlayerLifeState::Unknown));
        }
        g_observedLocalPlayer = nullptr;
        g_aliveStateKnown = false;
        return;
    }
    shared::ControlBlock* const block = CurrentControlBlock();
    if (block != nullptr)
    {
        InterlockedExchange(
            &block->localPlayerLifeState,
            static_cast<LONG>(alive
                    ? shared::LocalPlayerLifeState::Alive
                    : shared::LocalPlayerLifeState::Dead));
    }
    if (!g_aliveStateKnown || localPlayer != g_observedLocalPlayer)
    {
        g_observedLocalPlayer = localPlayer;
        g_previousAlive = alive;
        g_aliveStateKnown = true;
        return;
    }
    if (g_previousAlive && !alive)
    {
        if (block != nullptr)
        {
            MemoryBarrier();
            InterlockedIncrement(&block->hapticDeathSequence);
        }
    }
    g_previousAlive = alive;
}

} // namespace bfvr
