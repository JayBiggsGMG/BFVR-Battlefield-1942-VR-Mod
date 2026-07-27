#include "client/CrosshairOverlay.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr std::ptrdiff_t kHudManagerSetShowCrosshairRva = 0x002A97B0;
constexpr std::array<BYTE, 25> kExpectedPrefix = {
    0x8B, 0x41, 0x08, 0x8B, 0x50, 0x0C, 0x8A, 0x44,
    0x24, 0x04, 0x88, 0x42, 0x08, 0x8B, 0x49, 0x0C,
    0x8B, 0x51, 0x0C, 0x88, 0x42, 0x08, 0xC2, 0x04,
    0x00};

class CrosshairOverlay
{
public:
    using SetShowCrosshairFn = void(__thiscall*)(void* manager, BOOL show);

    void Start(
        void* gameImage,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return;
        }
        appendLog_ = log;
        target_ = gameImage == nullptr
            ? nullptr
            : static_cast<std::byte*>(gameImage) +
                kHudManagerSetShowCrosshairRva;
        if (!HasExpectedPrefix())
        {
            WriteLog(
                L"Native crosshair suppression rejected target %p: the profiled WinPC HudManager::setShowCrossHair bytes differ.",
                target_);
            InterlockedExchange(&started_, 0);
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
                L"Native crosshair suppression could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS createStatus = MH_CreateHook(
            target_,
            reinterpret_cast<LPVOID>(&CrosshairOverlay::Hook),
            reinterpret_cast<LPVOID*>(&original_));
        if (createStatus != MH_OK || original_ == nullptr)
        {
            WriteLog(
                L"Native crosshair suppression could not create its HudManager hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookCreated_ = true;
        InterlockedExchangePointer(&active_, this);
        const MH_STATUS enableStatus = MH_EnableHook(target_);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Native crosshair suppression could not enable its HudManager hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookEnabled_ = true;
        WriteLog(
            L"Native flat crosshair suppression armed at 0x006A97B0; HudManager crosshair visibility requests are forced off without changing global HUD state or unrelated Ref2 draw families.");
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled_)
        {
            MH_DisableHook(target_);
            hookEnabled_ = false;
        }
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        WriteLog(
            L"Native crosshair suppression stopped: visibilityRequests=%ld forcedHidden=%ld.",
            InterlockedCompareExchange(&visibilityRequests_, 0, 0),
            InterlockedCompareExchange(&forcedHidden_, 0, 0));
        RemoveHook();
        InterlockedExchange(&started_, 0);
    }

private:
    static void __fastcall Hook(
        void* manager,
        void*,
        BOOL requestedVisible)
    {
        InterlockedIncrement(&callbackEntrants_);
        CrosshairOverlay* const overlay =
            static_cast<CrosshairOverlay*>(
                InterlockedCompareExchangePointer(
                    &active_,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->original_ != nullptr)
        {
            InterlockedIncrement(&overlay->visibilityRequests_);
            if (requestedVisible)
            {
                InterlockedIncrement(&overlay->forcedHidden_);
            }
            overlay->original_(manager, FALSE);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    bool HasExpectedPrefix() const noexcept
    {
        if (target_ == nullptr)
        {
            return false;
        }
        __try
        {
            return std::memcmp(
                       target_,
                       kExpectedPrefix.data(),
                       kExpectedPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        if (hookCreated_)
        {
            MH_RemoveHook(target_);
            hookCreated_ = false;
        }
        original_ = nullptr;
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
        std::array<wchar_t, 600> message = {};
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

    volatile LONG started_ = 0;
    volatile LONG visibilityRequests_ = 0;
    volatile LONG forcedHidden_ = 0;
    void* target_ = nullptr;
    SetShowCrosshairFn original_ = nullptr;
    bool hookCreated_ = false;
    bool hookEnabled_ = false;
    bool ownsMinHook_ = false;
    void (*appendLog_)(const wchar_t* message) = nullptr;

    static void* volatile active_;
    static volatile LONG callbackEntrants_;
};

void* volatile CrosshairOverlay::active_ = nullptr;
volatile LONG CrosshairOverlay::callbackEntrants_ = 0;
CrosshairOverlay g_crosshairOverlay;

} // namespace

namespace bfvr
{

void StartCrosshairOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_crosshairOverlay.Start(gameImage, appendLog);
}

void StopCrosshairOverlay()
{
    g_crosshairOverlay.Stop();
}

} // namespace bfvr
