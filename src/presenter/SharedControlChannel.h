#pragma once

#include "presenter/SharedPresentationProtocol.h"

#include <string>

namespace bfvr::shared
{
class SharedControlChannel
{
public:
    SharedControlChannel() = default;
    ~SharedControlChannel();

    SharedControlChannel(const SharedControlChannel&) = delete;
    SharedControlChannel& operator=(const SharedControlChannel&) = delete;

    bool Create(const wchar_t* channelName, DWORD producerProcessId);
    bool Open(const wchar_t* channelName);
    void Close();

    [[nodiscard]] ControlBlock* Get() const noexcept;
    [[nodiscard]] DWORD LastErrorCode() const noexcept;
    [[nodiscard]] const std::wstring& Name() const noexcept;

    // Two auto-reset events act as cross-process doorbells. The shared
    // counters remain the source of truth, so either side can safely fall
    // back to bounded polling when an older companion has no event handles.
    [[nodiscard]] bool SignalProducerUpdate() const noexcept;
    [[nodiscard]] bool SignalPresenterUpdate() const noexcept;
    [[nodiscard]] DWORD WaitForProducerUpdate(DWORD timeoutMs) const noexcept;
    [[nodiscard]] DWORD WaitForPresenterUpdate(DWORD timeoutMs) const noexcept;
    [[nodiscard]] bool HasUpdateEvents() const noexcept;

private:
    bool Map(HANDLE mapping);
    void CreateUpdateEvents() noexcept;
    void OpenUpdateEvents() noexcept;
    void CloseUpdateEvents() noexcept;

    HANDLE mapping_ = nullptr;
    HANDLE producerUpdateEvent_ = nullptr;
    HANDLE presenterUpdateEvent_ = nullptr;
    ControlBlock* block_ = nullptr;
    DWORD lastError_ = ERROR_SUCCESS;
    std::wstring name_;
};

[[nodiscard]] bool IsCompatible(const ControlBlock* block) noexcept;
void PublishState(volatile LONG* destination, ProcessState state) noexcept;
[[nodiscard]] ProcessState ReadState(const volatile LONG* source) noexcept;
} // namespace bfvr::shared
