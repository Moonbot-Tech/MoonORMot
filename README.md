<p align="center">
  <a href="https://moonbot.pro">
    <img src="assets/moonbot-logo-full.svg" alt="Moonbot" width="199">
  </a>
</p>

<h1 align="center">MoonORMot</h1>

<p align="center">
  <b>Product-qualified mORMot 2 source tree for Moonbot and MoonCompiler</b>
</p>

MoonORMot is the pinned mORMot 2 derivative used by Moonbot developers and by
MoonCompiler product builds. It carries Moonbot-specific product adaptations
and hot-path optimizations. Exact changes and their provenance are recorded in
the Git history and in [PROVENANCE.md](PROVENANCE.md).

## Upstream and acknowledgements

MoonORMot is built on [mORMot 2](https://github.com/synopse/mORMot2), created
and maintained by Arnaud Bouchez and Synopse. We are deeply grateful for
Arnaud's architecture, performance work, documentation, and many years of
contribution to the Object Pascal community.

mORMot 2 remains the upstream project and the source of the framework
architecture. Generally useful fixes are submitted upstream as focused pull
requests through the standard
[`Moonbot-Tech/mORMot2`](https://github.com/Moonbot-Tech/mORMot2) fork.
Moonbot-specific product changes remain in MoonORMot. This repository does not
replace upstream and does not imply endorsement by Arnaud Bouchez or Synopse.

## Product scope

This repository is maintained for:

- Moonbot production builds with Delphi 12.2 on Win64;
- MoonCompiler product builds on x86-64 Win64 and Linux.

Other mORMot targets remain part of the inherited source tree, but they are not
part of the MoonORMot product qualification contract.

## For Moonbot developers

Product projects should consume an explicitly pinned MoonORMot commit. Do not
merge a new upstream snapshot or replace files under `static/` independently of
the matching source and qualification changes.

For an update:

1. keep each product repair or upstream synchronization in a semantic commit;
2. qualify all affected Delphi and MoonCompiler targets;
3. update provenance and static-binary hashes when their inputs change;
4. submit generally useful fixes upstream whenever they can stand on their own.

Useful repository entry points:

- [PROVENANCE.md](PROVENANCE.md) — pinned upstream base and product boundary;
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) — bundled component notices;
- [`static/MANIFEST.sha256`](static/MANIFEST.sha256) — exact identities of the
  prebuilt static-link inputs;
- [`tests/numbers`](tests/numbers) — black-box numeric parser qualification;
- [`tests/zip`](tests/zip) — ZIP read/copy/update regression with independent fixtures.

## Licensing

The inherited mORMot source retains its original disjunctive MPL 1.1, GPL 2.0,
or LGPL 2.1 license with the FPC modified-LGPL linking exception. See
[LICENSE.md](LICENSE.md), the original per-file notices, and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

Moonbot · MoonORMot — product mORMot derivative · [moonbot.pro](https://moonbot.pro)
