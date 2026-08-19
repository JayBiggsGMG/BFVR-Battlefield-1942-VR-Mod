#pragma once

#include "../BFVRVersion.inc"

#define BFVR_WIDEN_VERSION_INNER(value) L##value
#define BFVR_WIDEN_VERSION(value) BFVR_WIDEN_VERSION_INNER(value)

namespace bfvr
{
inline constexpr wchar_t kVersionString[] =
    BFVR_WIDEN_VERSION(BFVR_VERSION_STRING);
inline constexpr wchar_t kVersionCredit[] =
    L"BFVR v" BFVR_WIDEN_VERSION(BFVR_VERSION_STRING)
    L" - JayBiggsGaming";
} // namespace bfvr
