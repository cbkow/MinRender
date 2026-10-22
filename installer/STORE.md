# Microsoft Store distribution (the Windows channel)

Windows ships only through the Microsoft Store. The Store signs the MSIX
with Microsoft's certificate during ingestion (no Authenticode certificate
to buy, no Defender `!ml` false positives on unsigned binaries) and delivers
updates, so the Windows build contains no self-updater. The former
direct-download Inno installer and WinSparkle feed were retired in 0.5.6;
`docs/appcast-win.xml` stays frozen at 0.5.5 so old installs polling it see
no update rather than an error.

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
scripts\build_msvc.bat -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
scripts\build_msvc.bat --build build --target minrender mr-restart minrender-headless --config Release
cd mr-agent && cargo build --release && cd ..
scripts\package.bat --msix
```

Output: `build\minRender-<version>-x64.msix`, version read from
`project(MinRender VERSION …)` in CMakeLists.txt (MSIX versions are 4-part;
the script appends `.0`). `MR_BUILD_DIR` points `package.bat` and
`make_msix.ps1` at another build tree if you keep several.

The staged `deploy\` folder is what goes into the package minus
`vc_redist.x64.exe` (windeployqt stages the runtime DLLs the app actually
needs; the redistributable installer is dropped).

## Local testing before submission

With Developer Mode enabled, register the staged loose files — no signing
needed:

```powershell
powershell -File scripts\make_msix.ps1 -Sideload -SkipPack
Add-AppxPackage -Register build\msix\AppxManifest.xml
```

This installs the packaged app for real (start menu entry, startup task,
firewall rules, package identity), running from `build\msix`.

The Store build has no `minrender-headless.exe` execution alias: giving the
headless exe its own Application entry hidden from Start requires the
`HeadlessAppBypass` waiver, which Partner Center refused on first upload.
On Windows, headless is a dev-build feature only.

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
(profile Any), and the startup task was registered. (The headless alias
verified then has since been removed, see above.) Verify after any
manifest change:

- App launches, tray works, farm start binds 8420/4243 with **no firewall
  prompt** (the manifest rules applied).
- `%LOCALAPPDATA%\MinRender` is used directly (the one Flexible
  Virtualization exclusion): existing node identity and config are picked
  up, files written there are visible to a normal shell, and nothing
  appears under `%LOCALAPPDATA%\Packages\<pfn>\LocalCache\Local\MinRender`.
- `HKCU\Software\MinRender\Node` is **not** visible in regedit while the
  app runs (registry stays virtualized); the RNDR companion, if installed,
  logs that it is on the HTTP poll.
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
- **unvirtualizedResources** — the text below (the Partner Center field is
  short, so this is deliberately compact). It answers, in order, the four
  points certification asked for when it denied 0.5.6.0 (see "Certification
  history"); keep that structure in any future edit, because a
  reconsideration "without new or expanded information" is denied again.

  > Win32 render-farm manager. Scoped with Windows Flexible Virtualization to ONE per-user folder, %LOCALAPPDATA%\MinRender (virtualization:ExcludedDirectory). No registry keys; HKCU stays virtualized. Minimum OS is Windows 11 so the exclusion is honored on every device. WHY: our Blender, After Effects and Cinema 4D submission plug-ins run inside those applications (not launched by us, no package identity), read config.json in that folder to find the farm and drop job files into submissions\ for minRender to pick up. Submitting from those apps is the core function and cannot see virtualized AppData. node_id.txt there keeps a node's farm identity across upgrades. FLEXIBLE VIRTUALIZATION: evaluated and used; per Microsoft's docs it still requires this capability. UNINSTALL: removes the app, startup task, firewall rules and all virtualized data. Only %LOCALAPPDATA%\MinRender remains (config.json, node_id.txt, minrender.db, submissions\, staging\); removal is documented at minrender.com/install/#uninstall (delete the folder). Nothing is written to HKLM, Program Files or ProgramData.

  Partner Center flags this capability as needing approval on upload — that
  is the normal path; the justification is what gets it approved during
  certification.

## Submission

Upload the `.msix` in a new Partner Center submission and paste the two
justifications from "Listing text" above into the submission's
**Restricted capabilities** section.

### Certification history

- **2026-10-17, 0.5.6.0 (first submission): denied** for
  unvirtualizedResources. The package disabled file-system and registry
  virtualization wholesale (`desktop6:*WriteVirtualization=disabled`) and
  the justification argued interop in general terms. The tester asked for:
  the specific files/folders/registry locations and why core functionality
  needs them; confirmation that Windows Flexible Virtualization was
  evaluated; uninstall behavior and how users remove leftovers; and
  confirmation the capability is limited to the minimum. Response: the
  manifest now uses Flexible Virtualization with a single
  `ExcludedDirectory` (`%LOCALAPPDATA%\MinRender`), the registry is left
  virtualized (the companion's registry fast path becomes the HTTP poll on
  Store installs), MinVersion is Windows 11 (the per-location syntax is
  ignored on Windows 10, which would silently re-virtualize the folder),
  and docs/install.md documents the data folder and uninstall. Flexible
  Virtualization reference:
  <https://learn.microsoft.com/windows/msix/desktop/flexible-virtualization>.
  Resubmitted as **0.5.7.0** (version bumped so the Store sees a clean new
  package rather than a reconsideration of the denied one).

Certification typically completes in 1–3 business days. If a tester asks
about the LAN/process model, `SECURITY.md` is the reference: authenticated
HTTP API, trusted-LAN farm tool, same category as Deadline/RenderPal etc.
Choose "Publish this submission manually" if you want to control release
timing against the farm.

## Farm rollout

The Store build requires **Windows 11** (see Certification history).

Per node, one-time migration from a pre-0.5.6 direct install:

1. Exit minRender, uninstall the old version (Settings → Apps). Node
   identity/config in `%APPDATA%\MinRender` and the farm share are untouched
   by uninstall, and the Store build reads the same paths.
2. Install from the Store, or headless via winget (no Microsoft account
   needed for free apps):
   `winget install --id 9PN4VXDZ3THW --source msstore --accept-package-agreements`
   (Store ID from Partner Center → Product identity; listing:
   <https://apps.microsoft.com/detail/9PN4VXDZ3THW>.)
3. **Disable Store auto-updates on farm nodes** (Store app → Settings → App
   updates off) so nodes don't self-update mid-job.

Upgrades then happen on your schedule, farm idle, all nodes together:

```powershell
winget upgrade --id 9PN4VXDZ3THW --source msstore --accept-package-agreements
```

The workstation can leave Store auto-update on. The epoch-fence/capabilities
machinery tolerates brief version skew either way.

## Per-release routine

1. Bump `project(MinRender VERSION x.y.z)` (CMakeLists), `APP_VERSION` in
   `src/core/config.h`, and the mr-agent `Cargo.toml`/`Cargo.lock` — the
   MSIX version follows CMakeLists automatically.
2. Build + `scripts\package.bat --msix`, sideload-smoke-test if the release
   touched packaging. Defender-scan the result on the build box.
3. New submission in Partner Center, upload, publish when the farm is ready
   (manual publish, so certification passing never lands on nodes mid-job).
4. macOS ships separately through `scripts/macos-release.sh` + the Sparkle
   appcast as before.
