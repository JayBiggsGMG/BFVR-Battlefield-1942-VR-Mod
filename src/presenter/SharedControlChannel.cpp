#include "presenter/SharedControlChannel.h"

#include <cstring>

namespace bfvr::shared
{
namespace
{
std::wstring MakeUpdateEventName(
    const std::wstring& channelName,
    const wchar_t* suffix)
{
    std::wstring result = channelName;
    result += suffix;
    return result;
}
} // namespace

SharedControlChannel::~SharedControlChannel()
{
    Close();
}

bool SharedControlChannel::Create(const wchar_t* channelName, DWORD producerProcessId)
{
    Close();
    if (channelName == nullptr || *channelName == L'\0' || producerProcessId == 0)
    {
        lastError_ = ERROR_INVALID_PARAMETER;
        return false;
    }

    name_ = channelName;
    mapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(ControlBlock)),
        name_.c_str());
    if (mapping_ == nullptr)
    {
        lastError_ = GetLastError();
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        lastError_ = ERROR_ALREADY_EXISTS;
        Close();
        return false;
    }
    if (!Map(mapping_))
    {
        Close();
        return false;
    }

    std::memset(block_, 0, sizeof(*block_));
    block_->magic = kProtocolMagic;
    block_->version = kProtocolVersion;
    block_->byteSize = static_cast<DWORD>(sizeof(ControlBlock));
    block_->producerProcessId = producerProcessId;
    CreateUpdateEvents();
    PublishState(&block_->producerState, ProcessState::Starting);
    return true;
}

bool SharedControlChannel::Open(const wchar_t* channelName)
{
    Close();
    if (channelName == nullptr || *channelName == L'\0')
    {
        lastError_ = ERROR_INVALID_PARAMETER;
        return false;
    }

    name_ = channelName;
    mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name_.c_str());
    if (mapping_ == nullptr)
    {
        lastError_ = GetLastError();
        return false;
    }
    if (!Map(mapping_) || !IsCompatible(block_))
    {
        if (lastError_ == ERROR_SUCCESS)
        {
            lastError_ = ERROR_REVISION_MISMATCH;
        }
        Close();
        return false;
    }
    OpenUpdateEvents();
    return true;
}

void SharedControlChannel::Close()
{
    CloseUpdateEvents();
    if (block_ != nullptr)
    {
        UnmapViewOfFile(block_);
        block_ = nullptr;
    }
    if (mapping_ != nullptr)
    {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    name_.clear();
}

ControlBlock* SharedControlChannel::Get() const noexcept
{
    return block_;
}

DWORD SharedControlChannel::LastErrorCode() const noexcept
{
    return lastError_;
}

const std::wstring& SharedControlChannel::Name() const noexcept
{
    return name_;
}

bool SharedControlChannel::SignalProducerUpdate() const noexcept
{
    return producerUpdateEvent_ != nullptr &&
        SetEvent(producerUpdateEvent_) != FALSE;
}

bool SharedControlChannel::SignalPresenterUpdate() const noexcept
{
    return presenterUpdateEvent_ != nullptr &&
        SetEvent(presenterUpdateEvent_) != FALSE;
}

DWORD SharedControlChannel::WaitForProducerUpdate(DWORD timeoutMs) const noexcept
{
    return producerUpdateEvent_ == nullptr
        ? WAIT_FAILED
        : WaitForSingleObject(producerUpdateEvent_, timeoutMs);
}

DWORD SharedControlChannel::WaitForPresenterUpdate(DWORD timeoutMs) const noexcept
{
    return presenterUpdateEvent_ == nullptr
        ? WAIT_FAILED
        : WaitForSingleObject(presenterUpdateEvent_, timeoutMs);
}

bool SharedControlChannel::HasUpdateEvents() const noexcept
{
    return producerUpdateEvent_ != nullptr &&
        presenterUpdateEvent_ != nullptr;
}

bool SharedControlChannel::Map(HANDLE mapping)
{
    block_ = static_cast<ControlBlock*>(MapViewOfFile(
        mapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(ControlBlock)));
    if (block_ == nullptr)
    {
        lastError_ = GetLastError();
        return false;
    }
    lastError_ = ERROR_SUCCESS;
    return true;
}

void SharedControlChannel::CreateUpdateEvents() noexcept
{
    const std::wstring producerName =
        MakeUpdateEventName(name_, L"-ProducerUpdate");
    const std::wstring presenterName =
        MakeUpdateEventName(name_, L"-PresenterUpdate");
    producerUpdateEvent_ = CreateEventW(
        nullptr,
        FALSE,
        FALSE,
        producerName.c_str());
    presenterUpdateEvent_ = CreateEventW(
        nullptr,
        FALSE,
        FALSE,
        presenterName.c_str());
    if (producerUpdateEvent_ == nullptr || presenterUpdateEvent_ == nullptr)
    {
        CloseUpdateEvents();
    }
}

void SharedControlChannel::OpenUpdateEvents() noexcept
{
    const std::wstring producerName =
        MakeUpdateEventName(name_, L"-ProducerUpdate");
    const std::wstring presenterName =
        MakeUpdateEventName(name_, L"-PresenterUpdate");
    producerUpdateEvent_ = OpenEventW(
        SYNCHRONIZE | EVENT_MODIFY_STATE,
        FALSE,
        producerName.c_str());
    presenterUpdateEvent_ = OpenEventW(
        SYNCHRONIZE | EVENT_MODIFY_STATE,
        FALSE,
        presenterName.c_str());
    if (producerUpdateEvent_ == nullptr || presenterUpdateEvent_ == nullptr)
    {
        CloseUpdateEvents();
    }
}

void SharedControlChannel::CloseUpdateEvents() noexcept
{
    if (producerUpdateEvent_ != nullptr)
    {
        CloseHandle(producerUpdateEvent_);
        producerUpdateEvent_ = nullptr;
    }
    if (presenterUpdateEvent_ != nullptr)
    {
        CloseHandle(presenterUpdateEvent_);
        presenterUpdateEvent_ = nullptr;
    }
}

bool IsCompatible(const ControlBlock* block) noexcept
{
    return block != nullptr &&
        block->magic == kProtocolMagic &&
        block->version == kProtocolVersion &&
        block->byteSize == sizeof(ControlBlock);
}

void PublishState(volatile LONG* destination, ProcessState state) noexcept
{
    MemoryBarrier();
    InterlockedExchange(destination, static_cast<LONG>(state));
}

ProcessState ReadState(const volatile LONG* source) noexcept
{
    if (source == nullptr)
    {
        return ProcessState::Failed;
    }
    return static_cast<ProcessState>(
        InterlockedCompareExchange(const_cast<volatile LONG*>(source), 0, 0));
}
} // namespace bfvr::shared
