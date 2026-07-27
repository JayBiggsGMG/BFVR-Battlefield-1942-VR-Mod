#pragma once

#include "client/D3D8ObserverBridge.h"

namespace bfvr
{

// Performs one bounded, opt-in ownership test for the previously classified
// shared fixed-function first-person-weapon draw family. The test changes only
// its temporary D3D8 World transform for a short run, then restores BF1942's
// original transform before returning from every intercepted draw. It does not
// create resources, call game functions, or modify input, camera, weapon,
// projectile, or network state.
void StartD3D8WeaponTransformOwnershipProbe(
    const D3D8ObserverCallbacks& callbacks);

} // namespace bfvr
