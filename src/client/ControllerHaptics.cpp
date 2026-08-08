#include "client/ControllerHaptics.h"

#include "client/ScopedOffHandSupportPoseCache.h"
#include "presenter/SharedPresentationProtocol.h"

#include <windows.h>

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
        g_observedLocalPlayer = nullptr;
        g_aliveStateKnown = false;
        return;
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
        if (shared::ControlBlock* const block = CurrentControlBlock())
        {
            InterlockedIncrement(&block->hapticDeathSequence);
        }
    }
    g_previousAlive = alive;
}

} // namespace bfvr
