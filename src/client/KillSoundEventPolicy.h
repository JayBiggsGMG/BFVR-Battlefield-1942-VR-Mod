#pragma once

#include <cstdint>

namespace bfvr
{

constexpr std::uint32_t kGameEventTypeScoreMessage = 0x2A;
constexpr std::int32_t kScoreMessageSubtypeKill = 3;
constexpr std::int32_t kScoreMessageSubtypeTeamKill = 6;

enum class KillSoundSource : std::uint8_t
{
    None = 0,
    ClientEvent = 1,
    ServerScore = 2,
};

// A kill sound belongs only to the ordinary score-message kill whose killer
// resolves to either Battlefield's PlayerManager current BFPlayer or the
// GameClient-local fallback. Team-kill, death, suicide, and unresolved-player
// score variants remain silent.
[[nodiscard]] constexpr bool ShouldPlayLocalKillSound(
    std::uint32_t eventType,
    std::int32_t scoreSubtype,
    std::uint8_t killerId,
    std::uint8_t victimId,
    const void* killerPlayer,
    const void* playerManagerCurrentPlayer,
    const void* gameClientLocalPlayer) noexcept
{
    return eventType == kGameEventTypeScoreMessage &&
        scoreSubtype == kScoreMessageSubtypeKill &&
        killerId != victimId && killerPlayer != nullptr &&
        (killerPlayer == playerManagerCurrentPlayer ||
            killerPlayer == gameClientLocalPlayer);
}

// Single-player and listen-server scoring is confirmed before the client score
// message boundary at GameServer::handleScore. Its normal-kill and team-kill
// codes are distinct, so only an ordinary kill by the canonical current player
// against another resolved player is eligible.
[[nodiscard]] constexpr bool ShouldPlayServerLocalKillSound(
    std::int32_t scoreSubtype,
    const void* scoringPlayer,
    const void* victimPlayer,
    const void* playerManagerCurrentPlayer) noexcept
{
    return scoreSubtype == kScoreMessageSubtypeKill &&
        scoringPlayer != nullptr && victimPlayer != nullptr &&
        scoringPlayer != victimPlayer &&
        scoringPlayer == playerManagerCurrentPlayer;
}

// A listen server can expose the same authoritative kill first at the server
// score boundary and then at the received-client boundary. Suppression is
// intentionally pair-specific and cross-source so rapid kills of different
// victims, or repeated events from one source, remain independently audible.
[[nodiscard]] constexpr bool ShouldSuppressCrossSourceKillSound(
    KillSoundSource source,
    const void* killerPlayer,
    const void* victimPlayer,
    std::uint64_t nowMs,
    KillSoundSource previousSource,
    const void* previousKillerPlayer,
    const void* previousVictimPlayer,
    std::uint64_t previousTimeMs,
    std::uint64_t duplicateWindowMs) noexcept
{
    return source != KillSoundSource::None &&
        previousSource != KillSoundSource::None &&
        source != previousSource &&
        killerPlayer == previousKillerPlayer &&
        victimPlayer == previousVictimPlayer &&
        nowMs >= previousTimeMs &&
        nowMs - previousTimeMs <= duplicateWindowMs;
}

// Battlefield reports a grenade or similar multi-kill as several confirmed
// kills. Play the first confirmation and collapse only the immediately
// following burst so simultaneous voices do not stack into one loud sound.
[[nodiscard]] constexpr bool ShouldSuppressKillSoundBurst(
    std::uint64_t nowMs,
    bool hasPreviousAudibleKill,
    std::uint64_t previousAudibleTimeMs,
    std::uint64_t burstWindowMs) noexcept
{
    return hasPreviousAudibleKill && nowMs >= previousAudibleTimeMs &&
        nowMs - previousAudibleTimeMs <= burstWindowMs;
}

} // namespace bfvr
