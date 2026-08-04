# Windows code signing & Defender false positives

> **Status:** the primary Windows channel is now the **Microsoft Store**
> (see `STORE.md`) — Store packages are signed by Microsoft during
> ingestion, so nothing below applies to them. This document remains for
> the unsigned fallback Inno installer: the Defender false-positive
> submission routine still applies to it, and the certificate options are
> kept in case a signed direct-download channel is ever wanted again.

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

Certificates are issued on **identity verification of the publisher**, not on
review of what the software does. CAs do not audit application behaviour, and
a render manager that spawns processes across a LAN is an ordinary software
category — Deadline, Royal Render, Qube! and OpenCue all ship signed. The
friction is paperwork and key custody, not eligibility.

1. **SignPath Foundation free OSS tier** — the best fit here. MinRender is
   MIT licensed with no commercial dual-licensing and a public repo, which
   are the substantive eligibility criteria. Signing runs inside their
   pipeline, so expect to move the Windows release build into CI rather than
   signing from a workstation. Applications take days to weeks.
   <https://signpath.io/solutions/open-source-community>
2. **Classic OV certificate** (Certum's Open Source Code Signing is the
   cheapest route; Sectigo/DigiCert run higher). Since the 2023 CA/Browser
   Forum change, OV keys must live on a hardware token or cloud HSM, which
   adds cost and logistics. OV builds SmartScreen reputation gradually:
   signing clears the *trojan* verdicts quickly, but "unrecognized app"
   warnings fade only after enough installs.
3. **Azure Artifact Signing** (formerly Trusted Signing) — *check current
   status before counting on it*. Individual-developer onboarding opened in
   preview in late 2024 but was suspended in April 2025, with access limited
   to US/Canada organisations having 3+ years of verifiable history. It is
   attractive when available (Microsoft-issued, no token to manage, signs
   fine from a workstation), but it is not currently a path for a solo
   maintainer without an established company.
   <https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options>

## You can ship before a certificate exists

Signing is the real fix, but it is not a prerequisite for releasing. The
VERSIONINFO metadata above improves the classifier score on its own, and the
false-positive submission below works on unsigned binaries — `!ml` verdicts
are typically cleared within days. `MR_SIGN_CMD` is opt-in, so an unsigned
0.5.3 builds and ships exactly as previous releases did.

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
