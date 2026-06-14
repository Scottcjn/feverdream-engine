# Why Windows / antivirus may flag `fd-game.exe` (and how it's addressed)

**Short version: it's a false positive.** `fd-game.exe` is an open-source game
client — SDL2 + Lua 5.4 talking to a local POV-Ray render daemon over a
**loopback** socket. The full source is in this repository and is reviewed; you
can build the exe yourself with `build-windows.sh`.

## Why it gets flagged

Generic/heuristic/ML antivirus detections (names like `Wacatac`, `Heur`, `ml`,
`Trojan:Win32/...!ml`) fire on a *profile*, not on behavior. A binary that is:

- **unsigned** (no Authenticode certificate),
- compiled with **mingw-w64** (less common than MSVC, trips some heuristics),
- statically linked (`-static-libgcc -static-libstdc++`),
- **brand new** with no download reputation, and
- **opens a network socket** (winsock),

matches the same shape as throwaway malware, so scanners flag it on suspicion of
being *unknown*, not because it does anything harmful. Indie and hobbyist games
hit this constantly.

## What this build already does to reduce it

- **Embedded version info** — CompanyName (Elyan Labs), ProductName, version,
  copyright, and the source URL (`fd-game.rc`). A metadata-less exe is the #1
  false-positive trigger; this fixes that half for free.
- **Application manifest** — declares the app properly (`asInvoker`, DPI-aware).
- **An icon** — real software has one.
- **No packing** — the exe is not UPX/packed (packing strongly trips AV).

## The real fix: code signing (provenance)

The only thing that makes the flags reliably stop is an **Authenticode
signature**. Options, cheapest first:

1. **SignPath.io — free for open-source projects.** Provides a certificate and a
   signing pipeline for OSS. Best fit for this repo. (https://signpath.io/open-source)
2. **OV code-signing certificate** (~$100-250/yr) — signs the exe; reputation
   then builds with downloads.
3. **EV code-signing certificate** (~$300/yr) — grants **instant** Microsoft
   SmartScreen reputation, no warm-up.
4. **Self-signed cert** — only useful if the user installs your cert; no public
   trust. Fine for a known group, not for wide distribution.

## If you hit a false positive

- **Quantify it:** upload to https://www.virustotal.com — a generic name on a
  small number of the ~70 engines confirms a false positive.
- **Report it** so the vendor whitelists the file:
  - Microsoft Defender: https://www.microsoft.com/wdsi/filesubmission (choose
    "I believe this file is incorrectly detected").
  - Most other vendors have a "report a false positive" form.
- **Build it yourself:** `cd game && ./build-windows.sh` reproduces the exact
  binary from source.
- **Reputation grows** with a GitHub Release and downloads; SmartScreen warnings
  fade as more users run the signed binary.
