#pragma once

#include "client/D3D8ObserverBridge.h"

namespace bfvr
{

// Starts one forwarding-only state-write census. It only records the original
// setter calls already made by the game during one Present-to-Present frame.
void StartD3D8StateCensusProbe(const D3D8ObserverCallbacks& callbacks);

} // namespace bfvr
