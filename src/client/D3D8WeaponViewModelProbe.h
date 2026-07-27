#pragma once

#include "client/D3D8ObserverBridge.h"

namespace bfvr
{

// Captures a bounded, forwarding-only profile of the D3D8 draw families used
// during a spawned infantry window. It records only game-supplied setter
// arguments and already-issued draw calls after their original D3D8 call
// succeeds. The probe does not create a resource, call a game function, alter
// a D3D state, or change input/gameplay state.
void StartD3D8WeaponViewModelProbe(const D3D8ObserverCallbacks& callbacks);

} // namespace bfvr
