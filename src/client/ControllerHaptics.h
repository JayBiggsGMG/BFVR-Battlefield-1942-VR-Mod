#pragma once

#include <cstdint>

namespace bfvr::shared
{
struct ControlBlock;
}

namespace bfvr
{

// Registers the current x86-to-x64 presentation transport. The caller must
// unregister it before unmapping the ControlBlock.
void RegisterControllerHapticTransport(
    shared::ControlBlock* controlBlock) noexcept;

// Publishes one authoritative local weapon-fire event. The established
// weapon-support binding, rather than raw controller buttons, selects hands.
void NotifyControllerWeaponFired() noexcept;

// Publishes one authoritative native BfMenu hover/highlight event. The caller
// is a direct hook on the game's UI event function, not an audio observer.
void NotifyControllerNativeMenuHover() noexcept;

// Publishes one server/game-client-confirmed local-player kill for the x64
// audio owner. This is independent of shot input and hit-marker rendering.
void NotifyLocalPlayerKillSound() noexcept;

struct NativeMenuSoundRequests
{
    std::uint32_t highlight = 0;
    std::uint32_t ok = 0;
    std::uint32_t cancel = 0;
};

// Consumes bounded presenter-authored menu feedback requests on the x86 game
// thread. The caller retains the live BfMenu pointer and chooses the exact
// verified native wrapper to invoke.
[[nodiscard]] NativeMenuSoundRequests
ConsumeNativeMenuSoundRequests() noexcept;

// Polls the profiled local BFPlayer alive byte once per presented game frame
// and publishes exactly one death event for an alive-to-dead transition.
void PollControllerHapticDeath(void* gameImage) noexcept;

} // namespace bfvr
