# BFVR AI Contributor Instructions

These instructions apply to the complete BFVR repository.

## Required reading

Before changing code, read these files in order:

1. `README.md`
2. `docs/AI_DEVELOPER_HANDOFF.md`
3. `docs/DEVELOPMENT.md`
4. Relevant portions of `devREADME.md`

`devREADME.md` is a historical technical record. Current source and passing
tests take precedence when history and implementation differ.

## Repository boundary

- This repository is the complete public BFVR project.
- Do not add Battlefield 1942 game files, BF42++, proprietary game assets,
  private logs, local build output, or personal test installations.
- The separate public reverse-engineering repository linked from the handoff is
  supplementary and may be older than BFVR. Do not silently copy assumptions
  from it into production code.

## Change rules

- Preserve working behavior unless the task explicitly changes it.
- Keep source files modular and below approximately 2,500 lines.
- Prefer signature-backed, fail-closed compatibility logic over hard-coded
  executable hashes or unconditional absolute-address writes.
- Never block an unfamiliar `BF1942.exe` merely because its hash is new.
- Never inject both a recognized bundled BF42++ proxy and standalone
  `bf42++.dll`.
- Keep player diagnostics off by default. Expensive diagnostics must remain
  explicitly opt-in as `normal` or `deep`.
- Add or update deterministic tests for changed policies and pure math.
- Update `CHANGELOG.md` for user-visible changes and the handoff when an
  architectural invariant changes.
- Do not build, tag, publish, or upload a release unless the exact staged
  installer payload has passed the release checklist.

## Verification

Build and test both architectures described in `docs/DEVELOPMENT.md`:

- Win32 client, launcher, translator, and tests.
- x64 OpenXR presenter.

Run the complete Win32 `ctest` suite. Headset-visible, controller, runtime, or
package compatibility claims require an actual headset test; compilation alone
is not proof.
