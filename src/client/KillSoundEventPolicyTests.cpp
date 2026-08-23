#include "client/KillSoundEventPolicy.h"

#include <iostream>

int main()
{
    int playerManagerLocalPlayer = 0;
    int gameClientLocalPlayer = 0;
    int remotePlayer = 0;
    const bool passed =
        bfvr::ShouldPlayLocalKillSound(
            0x2A,
            3,
            7,
            9,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer,
            nullptr) &&
        bfvr::ShouldPlayLocalKillSound(
            0x2A,
            3,
            7,
            9,
            &gameClientLocalPlayer,
            nullptr,
            &gameClientLocalPlayer) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x29,
            3,
            7,
            9,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer,
            nullptr) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x2A,
            6,
            7,
            9,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer,
            nullptr) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x2A,
            3,
            7,
            7,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer,
            nullptr) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x2A,
            3,
            7,
            9,
            &remotePlayer,
            &playerManagerLocalPlayer,
            &gameClientLocalPlayer) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x2A,
            3,
            7,
            9,
            nullptr,
            &playerManagerLocalPlayer,
            &gameClientLocalPlayer) &&
        !bfvr::ShouldPlayLocalKillSound(
            0x2A, 3, 7, 9, &remotePlayer, nullptr, nullptr) &&
        bfvr::ShouldPlayServerLocalKillSound(
            3,
            &playerManagerLocalPlayer,
            &remotePlayer,
            &playerManagerLocalPlayer) &&
        !bfvr::ShouldPlayServerLocalKillSound(
            6,
            &playerManagerLocalPlayer,
            &remotePlayer,
            &playerManagerLocalPlayer) &&
        !bfvr::ShouldPlayServerLocalKillSound(
            3,
            &remotePlayer,
            &gameClientLocalPlayer,
            &playerManagerLocalPlayer) &&
        !bfvr::ShouldPlayServerLocalKillSound(
            3,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer,
            &playerManagerLocalPlayer) &&
        !bfvr::ShouldPlayServerLocalKillSound(
            3,
            &playerManagerLocalPlayer,
            nullptr,
            &playerManagerLocalPlayer) &&
        !bfvr::ShouldPlayServerLocalKillSound(
            3,
            &playerManagerLocalPlayer,
            &remotePlayer,
            nullptr) &&
        bfvr::ShouldSuppressCrossSourceKillSound(
            bfvr::KillSoundSource::ServerScore,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1100,
            bfvr::KillSoundSource::ClientEvent,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1000,
            2000) &&
        !bfvr::ShouldSuppressCrossSourceKillSound(
            bfvr::KillSoundSource::ClientEvent,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1100,
            bfvr::KillSoundSource::ClientEvent,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1000,
            2000) &&
        !bfvr::ShouldSuppressCrossSourceKillSound(
            bfvr::KillSoundSource::ServerScore,
            &playerManagerLocalPlayer,
            &gameClientLocalPlayer,
            1100,
            bfvr::KillSoundSource::ClientEvent,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1000,
            2000) &&
        !bfvr::ShouldSuppressCrossSourceKillSound(
            bfvr::KillSoundSource::ServerScore,
            &playerManagerLocalPlayer,
            &remotePlayer,
            3001,
            bfvr::KillSoundSource::ClientEvent,
            &playerManagerLocalPlayer,
            &remotePlayer,
            1000,
            2000) &&
        !bfvr::ShouldSuppressKillSoundBurst(1000, false, 0, 300) &&
        bfvr::ShouldSuppressKillSoundBurst(1100, true, 1000, 300) &&
        bfvr::ShouldSuppressKillSoundBurst(1300, true, 1000, 300) &&
        !bfvr::ShouldSuppressKillSoundBurst(1301, true, 1000, 300) &&
        !bfvr::ShouldSuppressKillSoundBurst(999, true, 1000, 300);
    if (!passed)
    {
        std::cerr << "BFVR local kill-sound event policy tests failed.\n";
        return 1;
    }
    std::cout << "BFVR local kill-sound event policy tests passed.\n";
    return 0;
}
