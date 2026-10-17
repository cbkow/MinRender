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

The Store configure lives in its own tree (`build-store`) so it never shares
a `deploy\` folder with the WinSparkle build in `build\`. `MR_BUILD_DIR`
points both `package.bat` and `make_msix.ps1` at it.

```bat
scripts\build_msvc.bat -S . -B build-store -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINRENDER_STORE_BUILD=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
scripts\build_msvc.bat --build build-store --target minrender mr-restart minrender-headless --config Release
cd mr-agent && cargo build --release && cd ..
set MR_BUILD_DIR=%CD%\build-store
scripts\package.bat --msix
```

Output: `build-store\minRender-<version>-x64.msix`, version read from
`project(MinRender VERSION …)` in CMakeLists.txt (MSIX versions are 4-part;
the script appends `.0`).

Notes:

- `MINRENDER_STORE_BUILD=ON` excludes WinSparkle and defines
  `MINRENDER_STORE_BUILD` for the app, which drops the "Check for Updates…"
  menu item. `make_msix.ps1` refuses a deploy folder that contains
  `WinSparkle.dll`, so packaging the wrong tree fails loudly.
- `MR_SIGN_CMD` is irrelevant on this channel; leave it unset.
- The fallback installer keeps using `build\` and `package.bat --iss`
  without `MR_BUILD_DIR`.

## Local testing before submission

With Developer Mode enabled, register the staged loose files — no signing
needed:

```powershell
$env:MR_BUILD_DIR = "$PWD\build-store"
powershell -File scripts\make_msix.ps1 -Sideload -SkipPack
Add-AppxPackage -Register build-store\msix\AppxManifest.xml
```

This installs the packaged app for real (start menu entry, startup task,
firewall rules, package identity), running from `build-store\msix`.

`Add-AppxPackage -Register` (and `Remove-AppxPackage`) must run in the
**interactive desktop session**. From an SSH or other non-interactive logon
it fails with `0x80070005` and the deployment log says "Failed to
initialize PLM" — the process lifetime manager does not exist there. The
pack step itself (`make_msix.ps1`) is fine over SSH.

Verified end to end on 2026-10-17 (first run of this flow): makeappx
accepted the manifest unchanged; the registered package launched from the
loose layout, read the real `%LOCALAPPDATA%\MinRender\config.json` with no
virtualized copy under `Packages\<pfn>\LocalCache`, published the node
state key, answered `/api/status`, got both manifest firewall rules
(profile Any), the `minrender-headless.exe` alias resolved under
`WindowsApps`, and the startup task was registered. Verify after any
manifest change:

- App launches, tray works, farm start binds 8420/4243 with **no firewall
  prompt** (the manifest rules applied).
- `%APPDATA%\MinRender` is used directly (unvirtualizedResources): existing
  node identity and config are picked up, and files written there are
  visible to a normal shell.
- Task Manager → Startup shows "minRender"; after sign-out/in the app is in
  the tray, window hidden (mr-restart's no-arg startup mode).
- The About menu has no "Check for Updates…" item (absent on Windows —
  the Store owns updates).

Uninstall from Start like any app.

## Listing text

Policy 10.2.4 requires a dependency on non-integrated software to be
disclosed at the *beginning* of the description, and 10.2.2 is answered by
stating plainly what the app launches. Draft:

> **Requires Blender, After Effects or Cinema 4D installed on each render
> node.** minRender is a lightweight render-farm manager for small studios.
> It coordinates render jobs across the Windows and macOS machines on your
> own network, launching the render application already installed on each
> node to render frame ranges in parallel, and shows progress, logs and
> frame previews in one place. Nodes find each other over the LAN and a
> shared folder; nothing leaves your network. Submit from the included
> Blender, After Effects and Cinema 4D plugins or from the app.

Restricted-capability justifications (Submission → Restricted capabilities):

- **runFullTrust** — "Win32 (Qt) render-farm manager. Launches locally
  installed render applications (Blender, After Effects, Cinema 4D) as
  child processes on the user's own machines to render frame ranges, and
  supervises a bundled render agent. No code is downloaded or executed
  beyond those installed applications."
- **unvirtualizedResources** — "Stores farm node identity and configuration
  in %LOCALAPPDATA%\MinRender, shared with existing non-Store installs of
  the same app; virtualization would orphan render-farm node identities on
  migration. Writes only to per-user AppData, never system locations."

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
