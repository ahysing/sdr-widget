# 1. Prefer upstream audio-widget-experimental on sync; minimal Mk3 build fixes

Date: 2026-10-12

## Status

Accepted

## Context

This fork carries Henry Audio Mk3 work (loudness, `henryctl`, UAC2
changes) on branch `main`, while **borgestrand/sdr-widget**
`audio-widget-experimental` remains the integration line for DA 256 /
SPRX firmware. Rebasing or merging must not silently fork USB
descriptors, board support, or Makefile defaults from upstream.

Mk3 release images are **not** the root `Makefile` default: they are
built with `scripts/build-ha128.sh` under **MSYS2 UCRT64**
(`MSYSTEM=UCRT64`, `IS_MSYS` via `makefile.msys2`) to produce
`Release/widget.elf` for **AT32UC3A3128** with **`FEATURE_PRODUCT_AB1x`**
and related AB1x flags. See `docs/plan-mk3-ucrt64-build.md`.

## Decision

**When integrating `audio-widget-experimental` into `main`, conflicts
must be resolved in favor of the upstream branch, and only the smallest
follow-up changes needed to pass `scripts/build-ha128.sh` on UCRT64 may
be kept on the fork side.**

## Consequences

- **Rebase onto upstream:** treat upstream as `ours` (`git checkout
  --ours` on conflicted paths). Do not prefer replayed `main` commits
  (`theirs`) for shared firmware or descriptor files unless upstream
  explicitly lacks a required symbol and a minimal patch is unavoidable.
- **Merge from upstream:** take upstream versions of conflicted files;
  re-apply fork-only deltas only where required for Mk3 build or host
  tools, in the smallest diff possible.
- **Build gate:** after each conflict resolution or replayed commit during
  a rebase, run `./scripts/build-ha128.sh` in the UCRT64 shell before
  `git rebase --continue`. To automate the loop: resolve conflicts with
  upstream (`git checkout --ours` on conflicted paths when rebasing `main`
  onto `audio-widget-experimental`), stage, run the Mk3 build gate, then
  `git rebase --continue`; repeat until the rebase finishes. If a merge
  auto-resolves `makefile.defs` to a stub, restore the full file from the
  integration branch (`git checkout HEAD -- makefile.defs`) before
  continuing.
- **Makefile policy:** root `Makefile` stays aligned with upstream
  (DA256 / SPRX defaults); Mk3 product selection stays in
  `build-ha128.sh` overrides, not permanent fork drift in upstream-owned
  files.
- **Risk:** upstream-first resolution may drop fork-only descriptor or
  integration hunks; restore behavior in a later commit only if Mk3
  build or documented product requirements fail after the gate.
- **Out of scope:** choosing MSVC vs MSYS for PC unit tests, or
  replacing `build-ha128.sh` with a new build system, without a new ADR.

## References

- [Mk3 UCRT64 build plan](../docs/plan-mk3-ucrt64-build.md)
