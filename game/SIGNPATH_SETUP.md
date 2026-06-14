# Code-signing `fd-game.exe` with SignPath (free for open source)

The release workflow (`.github/workflows/release-windows.yml`) builds the
Windows client and, **once configured**, gets it Authenticode-signed by
[SignPath](https://signpath.io) so antivirus/SmartScreen stop flagging it. Until
you do the steps below, the workflow still runs and produces an *unsigned but
version-stamped* package (which already clears most heuristic flags).

## One-time setup

### 1. Register the project on SignPath
- Go to https://signpath.io/open-source and apply with this repo
  (`Scottcjn/feverdream-engine`). Approval is free for OSS.
- In the SignPath console, create:
  - a **Project** (note its *slug*, e.g. `feverdream-engine`),
  - an **Artifact configuration** for a single PE file (note its *slug*, e.g. `fd-game-exe`),
  - a **Signing policy** (use `test-signing` first to dry-run, then `release-signing`).
- Connect the **GitHub Actions** trusted build system to the project (SignPath
  verifies the build came from this repo's workflow).
- Create an **API token** for CI.

### 2. Add the GitHub repo variables + secret
Repo → Settings → Secrets and variables → Actions:

| Type     | Name                        | Value (from SignPath console)        |
|----------|-----------------------------|--------------------------------------|
| Variable | `SIGNPATH_ORGANIZATION_ID`  | your SignPath organization GUID      |
| Variable | `SIGNPATH_PROJECT_SLUG`     | e.g. `feverdream-engine`             |
| Variable | `SIGNPATH_POLICY_SLUG`      | `test-signing` then `release-signing`|
| Variable | `SIGNPATH_ARTIFACT_SLUG`    | e.g. `fd-game-exe`                   |
| Secret   | `SIGNPATH_API_TOKEN`        | the CI API token                     |

The signing step is guarded on `SIGNPATH_ORGANIZATION_ID`, so it stays skipped
until that variable exists — no broken runs in the meantime.

### 3. Ship a signed build
- Publish a GitHub **Release** (tag like `v0.3.2`), **or** run the *Windows
  Release (signed)* workflow manually with the release tag.
- The workflow builds `fd-game.exe`, submits it to SignPath, downloads the
  **signed** exe, zips the package, and attaches it to the release.

## Verify a build is signed
On Windows: right-click `fd-game.exe` → Properties → **Digital Signatures** tab
(should list Elyan Labs / your SignPath identity). Or `signtool verify /pa fd-game.exe`.

## Notes
- SignPath's free tier gives a standard (OV-class) certificate. SmartScreen
  reputation then builds with downloads. For *instant* SmartScreen trust you'd
  need an EV certificate (paid).
- The **daemon** (`fd-daemon.exe`, MSVC) is built separately on the Windows box
  and isn't part of this CI flow; sign it the same way if you distribute it.
- Pin the SignPath action to the current major version per its README; this
  workflow uses `@v1`.
