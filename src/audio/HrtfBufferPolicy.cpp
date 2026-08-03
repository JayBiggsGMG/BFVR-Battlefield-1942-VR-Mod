#include "audio/HrtfBufferPolicy.h"

namespace bfvr::audio
{
bool ShouldCenterHrtfBuffer(
    const HrtfBufferPolicyInput& input) noexcept
{
    return !input.primary &&
        !input.threeDimensional &&
        input.panControl &&
        input.channels == 1 &&
        input.pan != 0;
}
} // namespace bfvr::audio
