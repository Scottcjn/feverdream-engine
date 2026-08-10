# daemon/tests — the check a machine without POV-Ray can run

```bash
bash daemon/tests/run.sh          # compile the daemon + drive the wire protocol
```

`run.sh` compiles **the real `daemon/fd-daemon.cpp`** with `-Wall -Werror`,
boots the binary on a Unix socket, and runs `test_protocol.py` against it —
using `daemon/fd_client.py` for the happy path and raw frames for the messages
a correct client would never send.

## Why a stub renderer

`fd-daemon` normally links `libvfe` + `libpovray` from the vendored, patched
POV-Ray 3.7 tree that `tools/build_engine.sh` produces. That tree cannot be
built on a bare runner, so nothing ever compiled this file — and between
2026-07-04 and the commit that added this directory, **it did not compile at
all** (an unterminated string literal in `serve_client`). `game/test.sh`, the
suite that defines "verified" for this repo, starts `../daemon/fd-daemon` and
fails every one of its checks without that binary; anyone with a stale copy
from before the break saw green anyway.

`stub/vfe.h` + `stub/vfeplatform.h` supply only the symbols `fd-daemon.cpp`
actually names. No POV-Ray source is copied and nothing links against
`libpovray`, so the stubs sit on the MIT side of the firewall
(`ARCHITECTURE.md` → Licensing) exactly like `game/fd_platform.h`.

The fake session is not a renderer. It parses the same `Width=` / `Height=` /
`Clock=` / `Declare=` option strings the real vfe consumes and paints a
deterministic gradient **through the daemon's own display creator**, so
`CaptureDisplay` → `fb_put` → the FRAME reply is exercised for real and the
tests assert on actual pixel values. Everything the suite covers — framing,
length caps, the declare whitelist, per-connection scene state, the
`SO_RCVTIMEO` read timeout — is independent of which renderer is behind it.

What this deliberately does **not** cover: whether POV-Ray parses a given
scene, render timing, and the resident-session behaviour that is the whole
point of `FINDINGS.md`. Those still need `game/test.sh` against a real daemon.

## Knobs

| env | default | meaning |
|---|---|---|
| `CXX` | `g++` | compiler for the stub build |
| `SOCK` | `/tmp/feverdream-proto-test.sock` | socket the test daemon listens on |
| `FD_READ_TIMEOUT` | `2` | seconds; the timeout the suite then measures |

## Suggested CI job

Not added here — committing under `.github/workflows/` needs a token scope this
PR was authored without. Drop this in as `.github/workflows/daemon-protocol.yml`
to close the gap that let a non-compiling daemon sit on `main` for four weeks:

```yaml
name: daemon protocol

on:
  push:
    paths: ['daemon/**', '.github/workflows/daemon-protocol.yml']
  pull_request:
    paths: ['daemon/**', '.github/workflows/daemon-protocol.yml']
  workflow_dispatch:

jobs:
  protocol:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build the daemon + run the protocol suite
        run: bash daemon/tests/run.sh

  # the game is the other half of the socket and has its own -Werror wall
  game-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y libsdl2-dev liblua5.4-dev
      - working-directory: game
        run: make
```
