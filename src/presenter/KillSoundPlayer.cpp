#include "presenter/KillSoundPlayer.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr std::uint64_t kMaximumKillSoundBytes = 16U * 1024U * 1024U;

std::wstring JoinPath(
    const wchar_t* directory,
    const wchar_t* child)
{
    if (directory == nullptr || directory[0] == L'\0' ||
        child == nullptr || child[0] == L'\0')
    {
        return {};
    }
    std::wstring result(directory);
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result.append(child);
    return result;
}

bool ReadFileBytes(
    const std::wstring& path,
    std::vector<std::byte>& bytes) noexcept
{
    bytes.clear();
    const HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER size = {};
    bool success = GetFileSizeEx(file, &size) != FALSE &&
        size.QuadPart > 0 &&
        static_cast<std::uint64_t>(size.QuadPart) <=
            kMaximumKillSoundBytes &&
        size.QuadPart <= std::numeric_limits<DWORD>::max();
    if (success)
    {
        try
        {
            bytes.resize(static_cast<std::size_t>(size.QuadPart));
        }
        catch (...)
        {
            success = false;
        }
    }
    DWORD read = 0;
    if (success)
    {
        success = ReadFile(
                file,
                bytes.data(),
                static_cast<DWORD>(bytes.size()),
                &read,
                nullptr) != FALSE &&
            read == bytes.size();
    }
    CloseHandle(file);
    if (!success)
    {
        bytes.clear();
    }
    return success;
}

std::uint32_t ReadU32(
    const std::vector<std::byte>& bytes,
    std::size_t offset) noexcept
{
    std::uint32_t result = 0;
    if (offset <= bytes.size() && bytes.size() - offset >= sizeof(result))
    {
        std::memcpy(&result, bytes.data() + offset, sizeof(result));
    }
    return result;
}

bool IsFourCc(
    const std::vector<std::byte>& bytes,
    std::size_t offset,
    const char (&expected)[5]) noexcept
{
    return offset <= bytes.size() && bytes.size() - offset >= 4 &&
        std::memcmp(bytes.data() + offset, expected, 4) == 0;
}
} // namespace

namespace bfvr
{

class KillSoundPlayer::Impl
{
public:
    struct VoiceCallback final : IXAudio2VoiceCallback
    {
        explicit VoiceCallback(std::atomic_bool& completed) noexcept
            : completed_(completed)
        {
        }

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override
        {
            completed_.store(true, std::memory_order_release);
        }
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override
        {
            completed_.store(true, std::memory_order_release);
        }

    private:
        std::atomic_bool& completed_;
    };

    struct ActiveVoice
    {
        ActiveVoice() noexcept : callback(completed) {}

        std::atomic_bool completed = false;
        VoiceCallback callback;
        IXAudio2SourceVoice* voice = nullptr;
    };

    bool Initialize(
        const wchar_t* payloadDirectory,
        KillSoundLogCallback callback,
        void* context)
    {
        Shutdown();
        logCallback = callback;
        logContext = context;
        const std::wstring path = JoinPath(
            payloadDirectory,
            L"assets\\Sounds\\killsound.wav");
        std::vector<std::byte> fileBytes;
        bool parsed = false;
        if (!path.empty() && ReadFileBytes(path, fileBytes))
        {
            try
            {
                parsed = ParseWave(fileBytes);
            }
            catch (...)
            {
                parsed = false;
            }
        }
        if (!parsed)
        {
            WriteLog(
                L"Kill sound is unavailable because '%s' is missing or is not a supported PCM WAVE file.",
                path.empty() ? L"assets\\Sounds\\killsound.wav" : path.c_str());
            return false;
        }
        HRESULT result = XAudio2Create(&engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(result) || engine == nullptr)
        {
            WriteLog(
                L"Kill sound could not initialize XAudio2 (result=0x%08lX).",
                static_cast<unsigned long>(result));
            Shutdown();
            return false;
        }
        result = engine->CreateMasteringVoice(&masteringVoice);
        if (FAILED(result) || masteringVoice == nullptr)
        {
            WriteLog(
                L"Kill sound could not create the Windows output voice (result=0x%08lX).",
                static_cast<unsigned long>(result));
            Shutdown();
            return false;
        }
        ready = true;
        const auto* const format = reinterpret_cast<const WAVEFORMATEX*>(
            formatBytes.data());
        const double durationSeconds = format->nAvgBytesPerSec == 0
            ? 0.0
            : static_cast<double>(audioBytes.size()) /
                static_cast<double>(format->nAvgBytesPerSec);
        WriteLog(
            L"Kill sound loaded from '%s': %u Hz, %u channel(s), %u-bit PCM, %.3f seconds. Each confirmed kill creates an independent overlapping XAudio2 source voice; Windows output volume applies, while Battlefield's private master-volume scalar is not guessed.",
            path.c_str(),
            format->nSamplesPerSec,
            format->nChannels,
            format->wBitsPerSample,
            durationSeconds);
        return true;
    }

    bool Play()
    {
        if (!ready || engine == nullptr || formatBytes.empty() ||
            audioBytes.empty())
        {
            return false;
        }
        Poll();
        std::unique_ptr<ActiveVoice> activeVoice;
        try
        {
            activeVoice = std::make_unique<ActiveVoice>();
        }
        catch (...)
        {
            return false;
        }
        HRESULT result = engine->CreateSourceVoice(
            &activeVoice->voice,
            reinterpret_cast<const WAVEFORMATEX*>(formatBytes.data()),
            0,
            XAUDIO2_DEFAULT_FREQ_RATIO,
            &activeVoice->callback);
        if (FAILED(result) || activeVoice->voice == nullptr)
        {
            return false;
        }
        XAUDIO2_BUFFER buffer = {};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(audioBytes.size());
        buffer.pAudioData = reinterpret_cast<const BYTE*>(audioBytes.data());
        result = activeVoice->voice->SubmitSourceBuffer(&buffer);
        if (SUCCEEDED(result))
        {
            result = activeVoice->voice->Start();
        }
        if (FAILED(result))
        {
            activeVoice->voice->DestroyVoice();
            activeVoice->voice = nullptr;
            return false;
        }
        try
        {
            voices.push_back(std::move(activeVoice));
        }
        catch (...)
        {
            activeVoice->voice->DestroyVoice();
            return false;
        }
        ++playedCount;
        return true;
    }

    void Poll()
    {
        voices.erase(
            std::remove_if(
                voices.begin(),
                voices.end(),
                [](const std::unique_ptr<ActiveVoice>& activeVoice) {
                    if (activeVoice == nullptr ||
                        !activeVoice->completed.load(
                            std::memory_order_acquire))
                    {
                        return false;
                    }
                    if (activeVoice->voice != nullptr)
                    {
                        activeVoice->voice->DestroyVoice();
                        activeVoice->voice = nullptr;
                    }
                    return true;
                }),
            voices.end());
    }

    void Shutdown()
    {
        ready = false;
        for (const std::unique_ptr<ActiveVoice>& activeVoice : voices)
        {
            if (activeVoice != nullptr && activeVoice->voice != nullptr)
            {
                activeVoice->voice->DestroyVoice();
                activeVoice->voice = nullptr;
            }
        }
        voices.clear();
        if (masteringVoice != nullptr)
        {
            masteringVoice->DestroyVoice();
            masteringVoice = nullptr;
        }
        if (engine != nullptr)
        {
            engine->Release();
            engine = nullptr;
        }
        formatBytes.clear();
        audioBytes.clear();
    }

    bool ParseWave(const std::vector<std::byte>& bytes)
    {
        formatBytes.clear();
        audioBytes.clear();
        if (bytes.size() < 12 || !IsFourCc(bytes, 0, "RIFF") ||
            !IsFourCc(bytes, 8, "WAVE"))
        {
            return false;
        }
        std::size_t formatOffset = 0;
        std::size_t formatSize = 0;
        std::size_t dataOffset = 0;
        std::size_t dataSize = 0;
        for (std::size_t offset = 12; offset + 8 <= bytes.size();)
        {
            const std::uint32_t chunkSize = ReadU32(bytes, offset + 4);
            const std::size_t payloadOffset = offset + 8;
            if (payloadOffset > bytes.size() ||
                chunkSize > bytes.size() - payloadOffset)
            {
                return false;
            }
            if (IsFourCc(bytes, offset, "fmt "))
            {
                formatOffset = payloadOffset;
                formatSize = chunkSize;
            }
            else if (IsFourCc(bytes, offset, "data"))
            {
                dataOffset = payloadOffset;
                dataSize = chunkSize;
            }
            const std::size_t paddedSize =
                static_cast<std::size_t>(chunkSize) + (chunkSize & 1U);
            if (paddedSize > bytes.size() - payloadOffset)
            {
                break;
            }
            offset = payloadOffset + paddedSize;
        }
        if (formatSize < 16 || dataSize == 0 ||
            dataSize > std::numeric_limits<UINT32>::max())
        {
            return false;
        }
        const std::size_t storedFormatSize = (std::max)(
            formatSize,
            sizeof(WAVEFORMATEX));
        formatBytes.assign(storedFormatSize, std::byte{});
        std::memcpy(formatBytes.data(), bytes.data() + formatOffset, formatSize);
        auto* const format =
            reinterpret_cast<WAVEFORMATEX*>(formatBytes.data());
        if (formatSize == 16)
        {
            format->cbSize = 0;
        }
        const bool supportedTag =
            format->wFormatTag == WAVE_FORMAT_PCM ||
            format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            format->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        if (!supportedTag || format->nChannels == 0 ||
            format->nChannels > XAUDIO2_MAX_AUDIO_CHANNELS ||
            format->nSamplesPerSec < XAUDIO2_MIN_SAMPLE_RATE ||
            format->nSamplesPerSec > XAUDIO2_MAX_SAMPLE_RATE ||
            format->nBlockAlign == 0 || format->nAvgBytesPerSec == 0 ||
            format->wBitsPerSample == 0 ||
            dataSize % format->nBlockAlign != 0)
        {
            formatBytes.clear();
            return false;
        }
        audioBytes.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + dataSize));
        return true;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr || format == nullptr)
        {
            return;
        }
        std::array<wchar_t, 1200> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(), message.size(), _TRUNCATE, format, arguments);
        va_end(arguments);
        logCallback(logContext, message.data());
    }

    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
    std::vector<std::byte> formatBytes;
    std::vector<std::byte> audioBytes;
    std::vector<std::unique_ptr<ActiveVoice>> voices;
    KillSoundLogCallback logCallback = nullptr;
    void* logContext = nullptr;
    std::uint64_t playedCount = 0;
    bool ready = false;
};

KillSoundPlayer::KillSoundPlayer() : impl_(std::make_unique<Impl>()) {}

KillSoundPlayer::~KillSoundPlayer()
{
    Shutdown();
}

bool KillSoundPlayer::Initialize(
    const wchar_t* payloadDirectory,
    KillSoundLogCallback logCallback,
    void* logContext)
{
    return impl_ != nullptr && impl_->Initialize(
        payloadDirectory, logCallback, logContext);
}

bool KillSoundPlayer::Play()
{
    return impl_ != nullptr && impl_->Play();
}

void KillSoundPlayer::Poll()
{
    if (impl_ != nullptr)
    {
        impl_->Poll();
    }
}

void KillSoundPlayer::Shutdown()
{
    if (impl_ != nullptr)
    {
        impl_->Shutdown();
    }
}

bool KillSoundPlayer::IsReady() const noexcept
{
    return impl_ != nullptr && impl_->ready;
}

} // namespace bfvr
