# OpenBIOS

`openbios.bin` is the unchanged standard BIOS image shipped in PCSX-Redux
Windows x64 development build **30495.20261107.24**, published on 2026-11-07.
The package and source revisions are recorded below.

- [PCSX-Redux downloads](https://distrib.app/pub/org/pcsx-redux/project/dev-win-x64)
- [Exact package](https://distrib.app/storage/assets/d72/365/553/34322f517473127d239bc174f5b3ebe45049773e10471a36c77b457/pcsx-redux-nightly-30495.20261107.24-x64.zip)
- PCSX-Redux source: `bb359c5149280bce32b2aa379bb0204a1d5e676a`
- OpenBIOS/Nugget source pinned by that build: `8a116567ad5202c53161266eaa2b9cc80460c689`
- `openbios.bin` SHA-256: `3b8bbf9bc67dfcb503116df0e56be977b8ae3d1ab2dc9dc31dcf59c175436a71`

Regenerate `openbios.bin.h` after replacing the binary:

```sh
cd deps/openbios
./convert.sh
```

OpenBIOS is distributed under the MIT license in `LICENSE.txt`.
