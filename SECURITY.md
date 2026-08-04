# Security model

MinRender coordinates a render farm: nodes accept work over HTTP and launch
DCC binaries to execute it. Anything that can reach a node's API can make it
run a renderer, restart the app, or stop rendering. That makes the trust
boundary worth stating explicitly.

## Trust model

MinRender assumes a **trusted LAN plus a trusted shared filesystem**. It is
not designed to be exposed to the public internet; do not port-forward 8420.

Two things are treated as equally privileged:

- **The shared farm root** (`farm.json`, `nodes/`, `jobs/`, templates). It
  holds `api_secret` in cleartext, so anyone who can read the share can drive
  the farm. **Farm security is share-permission security** — restrict the
  sync root to the accounts that should be able to submit renders.
- **The farm API secret**, a 256-bit random hex string generated on first
  farm init (`BCryptGenRandom` / `/dev/urandom`) and stored in `farm.json`.

## What is authenticated

Every `/api/` route requires `Authorization: Bearer <api_secret>`, with one
exception: `GET /api/status`, which peer discovery must reach before it can
know whether a secret matches. It exposes hostname, hardware, and render
state — no control, no secrets.

Auth **fails closed**. If `farm.json` is unreadable or its `api_secret` is
blank, the API answers `503 auth_unavailable` rather than serving unprotected
requests. Farm init refuses to write a blank secret.

Token comparison is length-independent (constant-time) so a wrong token can't
be recovered byte-by-byte from response timing.

## What is *not* authenticated: UDP multicast

Heartbeats on UDP 4243 are plaintext and unsigned — anything on the LAN can
emit or forge one. They are therefore treated as an **untrusted hint**:

- A heartbeat can only refresh liveness/state for a peer that is *already*
  known from the shared filesystem.
- A heartbeat can **never** create a peer or change a peer's endpoint.

This matters because the leader POSTs `/api/dispatch/assign` to a peer's
endpoint *with the secret in the Authorization header*. When UDP could set
endpoints, a single spoofed packet redirected that POST to an attacker-chosen
host and handed over the farm secret. The authority for endpoints is
`nodes/<id>/endpoint.json` on the shared root, which already requires share
access to write.

Forged heartbeats can still misreport a known peer's state (e.g. mark it busy)
— a nuisance-level denial of service on a LAN that is already trusted.
Signing heartbeats with an HMAC of the farm secret would close it and is the
natural next step if the threat model tightens.

## Network exposure

The HTTP server binds `0.0.0.0`, so reachability is bounded by the firewall
rather than the bind address. The installer opens TCP 8420 and UDP 4243 for
**all profiles and any remote address**, and that is a deliberate choice
rather than an oversight.

Both obvious tightenings were evaluated and rejected:

- `remoteip=localsubnet` breaks peers that reach the farm over VPN from
  another subnet, and does not help on public Wi-Fi, where the hostile
  network is itself the local subnet.
- `profile=domain,private` does cover the public Wi-Fi case, but makes a node
  silently unreachable whenever Windows classifies its VPN adapter as Public.
  That presents as a peer that is simply dead, which is expensive to diagnose.

The deciding factor is that every `/api/` route now requires the farm secret.
An open port therefore exposes only unauthenticated `GET /api/status` —
hostname, hardware, tags, and the active job name. On a controlled LAN that
residual is smaller than the support cost of nodes disappearing from the farm.
If that information disclosure ever matters, trimming or authenticating
`/api/status` is the lever, not the firewall.

## Secrets and logs

The farm secret must never reach a log or a file outside `farm.json`. As of
this writing it flows only into `authHeaders()` and `secretFingerprint()`;
nothing logs it, and no httplib logger is installed. Job manifests are also
never logged in full — the monitor logs job ids and chunk ranges, and the
agent logs only the resolved executable, never `flags` or `environment`.
Keep it that way: manifest environment blocks routinely carry licence keys,
and both the node log (`GET /api/nodes/:id/log`) and chunk stdout
(`GET /api/jobs/:id/task-output/...`) are readable over the API.

## Renders run arbitrary code by design

A job manifest names its own executable (`cmd`), arguments (`flags`), and
environment. Nothing constrains it to the template it claims — `template_id`
is provenance only. Spawning uses an argv array with no shell, so there is no
metacharacter injection, but anyone who can submit a job can run code on
every node. An executable allowlist was considered and rejected: DCC apps
ship interpreters inside their own install trees (Blender bundles
`python.exe`), and since the manifest also controls argv, `blender -b
--python-expr` is arbitrary code regardless. Node tags do not help either —
the manifest declares its own `tags_required`, so it cannot authorise itself.

What actually bounds the damage is deployment, not code: **run renders under
a dedicated non-administrator account**, and keep the sync root's ACLs tight.
Note there are two execution channels — `POST /api/jobs`, which needs the
secret, and an edit command written to the farm share, which needs only share
write access.

## Rotating the secret

Stop MinRender on every node, edit `api_secret` in `farm.json`, restart. Nodes
read it at farm start; mixed secrets show up as `401` in the logs.

## Binary distribution

The primary Windows channel is the Microsoft Store: MSIX packages are signed
by Microsoft during ingestion and updated by the Store (Store builds exclude
the self-updater — `MINRENDER_STORE_BUILD`; see `installer/STORE.md`). A
direct-download Inno installer remains as an unsigned fallback, which is why
Defender may flag it — see `installer/CODESIGNING.md`. macOS builds are
signed and notarized, with updates Ed25519-verified by Sparkle.

## Reporting

Open a GitHub issue at <https://github.com/cbkow/minrender>. Please avoid
posting a working exploit against a farm you do not own.
