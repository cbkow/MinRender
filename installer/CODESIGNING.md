# Windows code signing & Defender false positives

MinRender's Windows builds get flagged by Windows Defender as
`Trojan:Win32/Bearfoos.B!ml`. The `!ml` suffix means it is a machine-learning
heuristic verdict, not a real signature match — a well-known false-positive
bucket for unsigned Qt/Rust binaries. The app's behavior profile (spawns
render binaries, kill-and-relaunch updater helper, WinSparkle downloading
installers, netsh firewall rules, startup shortcut) is legitimate but scores
badly when combined with **no Authenticode signature** and (before 0.5.3)
**no version metadata**.

macOS releases are already signed + notarized (`scripts/macos-release.sh`);
Windows needs the equivalent.

## What the build now does automatically

- Every exe (`minRender.exe`, `minrender-headless.exe`, `mr-restart.exe`,
  `mr-agent.exe`) embeds an icon and a full `VERSIONINFO` resource
  (CompanyName/FileDescription/versions) — `resources/version.rc.in` for the
  CMake targets, `mr-agent/build.rs` for the Rust agent. Metadata-less
  binaries are heavily penalized by Defender's classifier.
- `scripts/package.bat` signs everything when `MR_SIGN_CMD` is set, and
  passes `/DSIGN` to Inno Setup so the installer + uninstaller stub get
  signed too:

  ```bat
  set MR_SIGN_CMD=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a
  scripts\package.bat --iss
  ```

  (`signtool` is on PATH in a VS developer prompt. `/a` picks the best cert
  in the store; replace with `/sha1 <thumbprint>` or Azure Trusted Signing's
  dlib arguments as appropriate.)

## Getting a certificate (pick one)

1. **Azure Trusted Signing** — ~$10/month, open to individual developers
   (identity verification required). Microsoft-issued short-lived certs;
   builds start with good SmartScreen reputation. Best price/effort for a
   solo maintainer. Integrates with signtool via the Trusted Signing dlib.
2. **SignPath.io free OSS tier** — free code signing for open-source
   projects; signing happens in their CI infrastructure. Worth applying
   since this repo is public on GitHub.
3. **Classic OV certificate** (Certum ~€, Sectigo/DigiCert ~$100–400/yr).
   Certum's "Open Source Code Signing" cert is the cheapest OV route.
   Note OV certs build SmartScreen reputation gradually — signing stops the
   *trojan* verdicts quickly, but "unrecognized app" warnings fade only
   after enough installs.

## Per-release routine (until reputation is established)

Signing fixes most of it, but each release is a new file hash. For each
Windows release:

1. Sign binaries + installer (above).
2. Submit the installer **and** `minRender.exe` to Microsoft as a software
   developer at <https://www.microsoft.com/en-us/wdsi/filesubmission>
   ("I believe this file is incorrectly detected"). `!ml` false positives
   are typically cleared in 1–3 days and feed the reputation system.
3. Optionally check the release on <https://www.virustotal.com> before
   publishing to catch other engines' heuristics early.

## If Defender already quarantined an install

On affected machines: Windows Security → Protection history → select the
`Bearfoos` detection → **Restore**, then add an allow entry. For your own
farm nodes an exclusion works immediately:
`Add-MpPreference -ExclusionPath 'C:\Program Files\MinRender'` (admin).
Don't ask end users to do this — fix it with signing + FP submission.
