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

private:
    bool Map(HANDLE mapping);

    HANDLE mapping_ = nullptr;
    ControlBlock* block_ = nullptr;
    DWORD lastError_ = ERROR_SUCCESS;
    std::wstring name_;
};

[[nodiscard]] bool IsCompatible(const ControlBlock* block) noexcept;
void PublishState(volatile LONG* destination, ProcessState state) noexcept;
[[nodiscard]] ProcessState ReadState(const volatile LONG* source) noexcept;
} // namespace bfvr::shared
