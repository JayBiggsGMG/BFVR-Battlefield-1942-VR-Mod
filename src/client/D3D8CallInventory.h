#pragma once

#include "client/D3D8ObserverBridge.h"

namespace bfvr
{

// Narrow bridge from BFVRClient's lifecycle observer into the isolated D3D8
// inventory. The inventory owns its hooks and event records; the client only
// supplies read-only lifecycle, capture-eligibility, and logging services.
// Starts one forwarding-only worker. It does nothing until the caller's
// lifecycle and capture-eligibility callbacks admit a capture.
void StartD3D8CallInventoryProbe(const D3D8ObserverCallbacks& callbacks);

} // namespace bfvr
