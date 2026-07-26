# Baseline local-host scenario

## Status

`Aberdeen / GPM_CQ / BF1942` remains the candidate baseline for profile
`bf1942-win32-decbb52f`, but automatic map loading is not valid in this local
installation. The owner reports that every BFVR launch has required manual map
selection/loading; command-line arguments can start the game but must never be
treated as proof that a map loaded. A tester must manually select the intended
offline map before collecting or judging a result. Aberdeen has not yet
completed the deployment/vehicle/death/respawn/menu regression, so it is not
an attachment-approved test map.

## Asset evidence

The game root has the standard level archives, including:

- `Mods\bf1942\Archives\bf1942\levels\Aberdeen.rfa`
- `Mods\bf1942\Archives\bf1942\levels\Aberdeen_006.rfa`

The former investigation checked only `bf1942\levels\gc_mini_dant`, which
contains only `loadcounter.dat`; it did not represent the installed archive
set. The current `Settings\maplist.con` names `GC_Dantooine / GPM_CQ / GCMOD`,
so the test must use explicit launch arguments rather than treating that stale
entry as the baseline.

## Candidate command

These game arguments may be supplied to start the ordinary flat client or a
future verified observer:

```text
+hostServer 1 +level Aberdeen +gamePlayMode GPM_CQ +game BF1942
```

Do not add `+restart 1` when using `BFVRLoader`: the initial injected process
exits before renderer initialization and the loader does not own the restarted
client. BFVR never rewrites `maplist.con` to select this test. Regardless of
the arguments, manually select/load the map and do not accept an automated
launch as map-load evidence.

## Validation still required

The current renderer/copy evidence comes from a different, user-selected map.
The candidate is accepted only after an offline/local run reaches deployment,
spawns infantry, enters a land vehicle, dies and respawns, returns to a menu,
and exits flat without changing game files. Capture the BFVR log, the profile
hash result, and any game error dialog if it fails.
