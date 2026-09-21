# Provenance

This repository is a product-oriented derivative of [Synopse mORMot 2](https://github.com/synopse/mORMot2), created for the Moonbot codebase and the [MoonCompiler](https://github.com/Moonbot-Tech/MoonCompiler) Unicode product profile.

## Upstream base

- Project: Synopse mORMot 2
- Original author and principal maintainer: Arnaud Bouchez
- Upstream repository: https://github.com/synopse/mORMot2
- Pinned release: mORMot 2.3-stable / `2.3.8832`
- Pinned upstream commit: `38874e16c03373a5275b959fdb1cc38d5597f67f`
- Upstream release date: 2024-10-16

The MoonORMot product branch is anchored at that exact upstream commit. Commit `9bfb7f3b51aae60807048bb6c8bb34528323ede3` then extracts and flattens the upstream `src/` subtree into the product layout. All Moonbot changes remain visible as later commits.

The public `Moonbot-Tech/mORMot2` repository remains a normal GitHub fork used for proposing focused changes upstream. This product tree is intentionally maintained as a separate non-fork repository so its pinned base, flattened layout, product configuration and qualification history are not confused with upstream pull-request branches.

## Product scope

The derivative keeps the broad mORMot source surface required by Moonbot while adding:

- Delphi-compatible Unicode boundaries for the MoonCompiler Win64 and Linux x86-64 product profiles;
- qualified x86-64 number parsing for Delphi Win64 and FPC System V/Win64 ABIs;
- the Moonbot memory-manager profile, diagnostics and stress-tested allocator repairs;
- selected correctness and lifecycle fixes discovered while compiling and qualifying production Moonbot workloads.

These changes do not transfer authorship of mORMot itself. mORMot is Arnaud Bouchez's project, and the original copyrights and per-file notices are retained with gratitude.

## Static-link bundle

The 40 files under `static/` were imported from the long-lived Moonbot product checkout by commit `187253761594135c7c937b40443c03e220ea74d1`. They were not rebuilt during the repository reconstruction. Their exact SHA-256 identities are recorded in [`static/MANIFEST.sha256`](static/MANIFEST.sha256).

The bundle corresponds to the static dependencies expected by the pinned mORMot source, including SQLite 3.46.1. The original source trees, build recipes and notices are available in the pinned upstream tree under [`res/static`](https://github.com/synopse/mORMot2/tree/38874e16c03373a5275b959fdb1cc38d5597f67f/res/static). Component attribution and redistribution notices are collected in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

When any static binary is replaced, the same commit must identify its source revision and build procedure, update the matching source-side version contract, and regenerate `static/MANIFEST.sha256`. Silent binary replacement is not accepted.

## Qualification boundary

- `tests/numbers` provides black-box equivalence and boundary qualification for the numeric parser implementations.
- `tests/zip` checks ZIP reads, copies, append and filtered updates against independent Python fixtures on Delphi Win64 and MoonCompiler Win64/Linux.
- The allocator is qualified by the memory and Pulse suites in MoonCompiler, including size sweeps, realloc transitions, independent fuzzing, cross-thread frees and saturation profiles.
- Product integration is additionally exercised by Moonbot and its Linux service builds.

A green focused suite proves only its stated boundary. It does not turn this pinned product derivative into a drop-in replacement for every later upstream mORMot release.

## Allocation failure and unwind

The bundled allocator and this copy share the same cold failure paths. Failed
allocation raises the runtime out-of-memory error; failed realloc preserves the
old pointer, contents and allocator ownership. A failed Windows commit releases
the unsuccessful reservation. Linux manual frames carry matching unwind metadata.
Ordinary leaf instructions are unchanged; cold refill/large checks and unwind
metadata have an explicit cost.

The transferred source passed MoonCompiler's addressed failure, ownership,
unwind, 21-profile and linked-layout checks on its supported targets. Fault hooks
were confined to test copies. These checks do not claim a new whole-product
qualification or qualify unrelated mORMot/Delphi code.

## Reason-preserving small-pool handoff

The Windows leaf transfers an already locked, empty pool to the cold retirement
path. Pending frees retain their original slow-path reason. This removes a
repeated unlock/decode/lock sequence without adding pool retention or changing
the medium-allocation policy. Linux is unchanged.

Temporary pairs benefit; under contention, earlier retirement can move another
worker to a different pool and increase latency. Matched-owner and forced-pending
tests cover that boundary. The existing deferred-free backlog is not repaired by
this change, and idle time alone is not a completion guarantee. The single-block
retention bound applies per arena/class, not to the entire process's memory.

## ZIP descriptors and update offsets

ZIP reads use the parsed central directory for final CRC, sizes and ZIP64 offsets.
Append preserves the existing local-header offsets and starts at the actual central
directory boundary, including archives with data descriptors. Copied or moved entries
receive complete local headers without a descriptor flag. Rebuilt directories contain
only the name and normalized ZIP64 fields that the writer actually emits.

The repair also includes the archive base offset when copying disk-backed entries.
A failed update constructor closes its output without writing a partial directory;
this does not provide rollback after an in-place payload move.

This is a local repair, compared against Synopse upstream
`743a53fd4cb3af700463b0d143581ce17c6c8b31` on 2026-09-22. That snapshot still contains
the descriptor search and append offset defects. It was inspected, not runtime-qualified.

All 231 direct ZIP checks pass with MoonCompiler O-/O2/O3 on Win64/Linux and Delphi
12 Win64 O-/O+. The preceding product pin `49b9cd4c2355ac382d4c5539b0257628a6409dfc`
passes 81/231 on each compiler/platform. Fixtures cover signed/unsigned descriptors,
empty entries, ZIP64 layouts, small working memory, append, copy, deletion, and failure
before the first payload move. ZIP64 uses small forced fixtures; files over 4 GiB,
metadata preservation, and filtered updates with reordered central directories are
outside this qualification. Static binaries and the allocator are unchanged.
