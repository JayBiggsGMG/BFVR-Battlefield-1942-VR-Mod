#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace bfvr
{

// Recognizes the BF42++ DirectSound proxy family by its PE structure and
// stable BF42++ markers. No exact file hash is used as an acceptance rule.
[[nodiscard]] bool IsBf42PlusPlusProxyImage(
    std::span<const std::byte> image) noexcept;

[[nodiscard]] bool IsBf42PlusPlusProxyFile(
    const std::wstring& path) noexcept;

} // namespace bfvr
