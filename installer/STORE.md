# Microsoft Store distribution (primary Windows channel)

The Store signs MSIX packages with Microsoft's certificate during ingestion,
which is what makes this the primary Windows channel: no Authenticode
certificate to buy, and the Defender `Bearfoos.B!ml` false-positive problem
(see `CODESIGNING.md`) goes away for Store installs. The Store also delivers
updates, replacing WinSparkle on this channel. The Inno installer remains as
an unsigned manual fallback.

## One-time setup (Partner Center)

1. In [Partner Center](https://partner.microsoft.com/dashboard) (existing
   developer account), **Apps and games → New product → MSIX or PWA app** and
   reserve the name `minRender`.
2. Open the app's **Product management → Product identity** page and copy the
   three values into environment variables on the build machine:

   | Partner Center value        | Env var                     |
   |-----------------------------|-----------------------------|
   | `Package/Identity/Name`     | `MR_MSIX_IDENTITY_NAME`     |
   | `Package/Identity/Publisher`| `MR_MSIX_PUBLISHER`         |
   | `Package/Properties/PublisherDisplayName` | `MR_MSIX_PUBLISHER_DISPLAY` |

   They must match exactly or ingestion rejects the package. Everything else
   in the manifest comes from `installer/msix/AppxManifest.xml.in`.

## Building a Store package

```bat
cmake -S . -B build -DMINRENDER_STORE_BUILD=ON  <usual generator args>
scripts\build_msvc.bat --build build --target minrender mr-restart minrender-headless --config Release
cd mr-agent && cargo build --release && cd ..
scripts\package.bat --msix
```

Output: `build\minRender-<version>-x64.msix`, version read from
`project(MinRender VERSION …)` in CMakeLists.txt (MSIX versions are 4-part;
the script appends `.0`).

Notes:

- `MINRENDER_STORE_BUILD=ON` excludes WinSparkle. If you previously built
  without it, delete `build\WinSparkle.dll` (or wipe `build\`) — the stale
  DLL would be staged into `build\deploy` and `make_msix.ps1` refuses to
  package a deploy folder containing it.
- `MR_SIGN_CMD` is irrelevant on this channel; leave it unset.
- Do not run `--iss` and `--msix` from the same configure: the fallback
  installer should keep WinSparkle, the Store package must not have it.

## Local testing before submission

With Developer Mode enabled, register the staged loose files — no signing
needed:

```powershell
powershell -File scripts\make_msix.ps1 -Sideload -SkipPack
Add-AppxPackage -Register build\msix\AppxManifest.xml
```

This installs the packaged app for real (start menu entry, startup task,
firewall rules, package identity), running from `build\msix`. Verify:

- App launches, tray works, farm start binds 8420/4243 with **no firewall
  prompt** (the manifest rules applied).
- `%APPDATA%\MinRender` is used directly (unvirtualizedResources): existing
  node identity and config are picked up, and files written there are
  visible to a normal shell.
- Task Manager → Startup shows "minRender"; after sign-out/in the app is in
  the tray, window hidden (mr-restart's no-arg startup mode).
- "Check for Updates…" in the About menu is greyed out.

Uninstall from Start like any app.

## Submission

Upload the `.msix` in a new Partner Center submission. On the submission's
**Restricted capabilities** section, supply justifications:

- **runFullTrust** — "Win32 (Qt) render-farm monitor. Launches locally
  installed render applications (Blender, After Effects) as child processes
  on the user's own machines and supervises a bundled render agent."
- **unvirtualizedResources** — "Stores farm node identity and configuration
  in %APPDATA%\MinRender, shared with existing non-Store installs of the
  same app; virtualization would orphan render-farm node identities on
  migration. Writes only to per-user AppData, never system locations."

Certification typically completes in 1–3 business days. If a tester asks
about the LAN/process model, `SECURITY.md` is the reference: authenticated
HTTP API, trusted-LAN farm tool, same category as Deadline/RenderPal etc.
Choose "Publish this submission manually" if you want to control release
timing against the farm.

## Farm rollout

Per node, one-time migration from the Inno install:

1. Exit minRender, uninstall the old version (Settings → Apps). Node
   identity/config in `%APPDATA%\MinRender` and the farm share are untouched
   by uninstall, and the Store build reads the same paths.
2. Install from the Store, or headless via winget (no Microsoft account
   needed for free apps):
   `winget install --id <StoreId> --source msstore --accept-package-agreements`
   (`<StoreId>` is the 12-char product id, e.g. `9NBLGGH4R315`, shown in
   Partner Center → Product identity.)
3. **Disable Store auto-updates on farm nodes** (Store app → Settings → App
   updates off) so nodes don't self-update mid-job.

Upgrades then happen on your schedule, farm idle, all nodes together:

```powershell
winget upgrade --id <StoreId> --source msstore --accept-package-agreements
```

The workstation can leave Store auto-update on. The epoch-fence/capabilities
machinery tolerates brief version skew either way.

## Per-release routine

1. Bump `project(MinRender VERSION x.y.z)` (CMakeLists) — flows into the
   MSIX version automatically. Keep the `.iss` `MyAppVersion` in sync for
   the fallback installer.
2. Build + `scripts\package.bat --msix`, sideload-smoke-test if the release
   touched packaging.
3. New submission in Partner Center, upload, publish when the farm is ready.
4. Fallback channel (unsigned Inno + appcast) only when there's a reason;
   that path keeps the Defender FP routine in `CODESIGNING.md`.
