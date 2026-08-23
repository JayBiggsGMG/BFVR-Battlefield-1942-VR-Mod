#pragma once

#include <memory>

namespace bfvr
{

using KillSoundLogCallback = void (*)(void* context, const wchar_t* message);

// Owns one persistent PCM asset and creates an independent XAudio2 source
// voice for every confirmed kill. Completed voices are reclaimed from the
// presenter thread; a later kill never restarts an earlier one.
class KillSoundPlayer
{
public:
    KillSoundPlayer();
    ~KillSoundPlayer();

    KillSoundPlayer(const KillSoundPlayer&) = delete;
    KillSoundPlayer& operator=(const KillSoundPlayer&) = delete;

    bool Initialize(
        const wchar_t* payloadDirectory,
        KillSoundLogCallback logCallback,
        void* logContext);
    bool Play();
    void Poll();
    void Shutdown();
    [[nodiscard]] bool IsReady() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bfvr
