---
title: Install
nav_order: 2
---

# Install

Install minRender on every node that will participate in the farm. Nodes can be Windows or macOS and work together in one farm.

## Windows

Install minRender from the [Microsoft Store](https://apps.microsoft.com/detail/9PN4VXDZ3THW) for automatic updates:

<a href="https://apps.microsoft.com/detail/9PN4VXDZ3THW?referrer=appbadge&mode=full" target="_blank" rel="noopener noreferrer">
  <img src="https://get.microsoft.com/images/en-us%20dark.svg" width="200" style="border: 1px solid #707070; border-radius: 4.5px;"/>
</a>

Requires **Windows 11** (see Requirements below). For farm nodes you can
install from a terminal without opening the Store:

```
winget install --id 9PN4VXDZ3THW --source msstore --accept-package-agreements
```

### Requirements

- **Windows 11**, x64 (any edition). Windows 10 is not supported: the Store
  package declares Windows 11 as its minimum and will not install on
  Windows 10 machines, so upgrade any remaining Windows 10 node first.
- The render application(s) you submit to — Blender, After Effects or Cinema 4D — installed on each node that should render.

The Store package declares firewall rules for HTTP port **8420** and UDP port **4243** and registers a startup task so minRender launches with the machine (toggle it in Task Manager → Startup).

### Where minRender keeps its data

Everything minRender stores on the machine lives in one per-user folder,
`%LOCALAPPDATA%\MinRender` (type that into Explorer's address bar):

| Item | What it is |
|---|---|
| `config.json` | farm settings: sync root, ports, node name |
| `node_id.txt` | this node's identity on the farm |
| `minrender.db` | local job/chunk history |
| `submissions\` | hand-off folder: the Blender, After Effects and Cinema 4D plug-ins drop job files here and minRender picks them up |
| `staging\` | frames in flight while a chunk renders; emptied when the chunk finishes |
| `mr-agent.log` | render agent log |

The folder is deliberately kept outside the Store package's private storage
so the plug-ins (which run inside Blender, After Effects or Cinema 4D) can
read the farm settings and write submissions, and so a node reinstalled or
upgraded keeps its identity.

### Uninstall

Uninstall from **Settings → Apps** (or `winget uninstall --id 9PN4VXDZ3THW`)
like any Store app. That removes the program, its startup task and firewall
rules, and the package's private data. The data folder above is left in
place so your farm settings and node identity survive a reinstall. To remove
it too, delete `%LOCALAPPDATA%\MinRender`, or from a terminal:

```
rmdir /s /q "%LOCALAPPDATA%\MinRender"
```

If you installed the optional [RNDR companion](https://github.com/cbkow/MinRender/tree/main/tools/rndr-companion),
run its `uninstall.ps1` first; it also lives in that folder.

---

## macOS

Download the latest release [here](https://github.com/cbkow/MinRender/releases/latest/).

1. Open the `.dmg` file.
2. Drag **minRender** to your **Applications** folder.
3. Launch from Applications or Spotlight.

### Updates

Once installed in **Applications**, minRender keeps itself up to date — it checks once a day and offers to download and install new versions in place. You can also check any time from **About → Check for Updates…**. (In-place updates require running minRender from the Applications folder, not from the mounted disk image.)

---

> minRender, like all render farm coordinators, lets you launch executables remotely. That behavior is dangerous outside a trusted LAN — keep it inside one, test your flow thoroughly, and practice safe computing.

## Point each node at the shared folder

Open the Monitor, go to **File → Settings…**, and browse to or paste a shared directory that **every** node can reach. This can be:

- an SMB share on a NAS,
- a synced folder via LucidLink, Dropbox, Synology Drive, Resilio, Syncthing, or similar.

That folder holds the node phonebook, leader state, job records, and aggregated logs (MinRender + DCC stdout). Press **Save**.

![MinRender Settings](/images/mr006.jpg)

Repeat on every node. As soon as two or more nodes share the same sync root, they'll discover each other and elect a leader.

## What gets created in the sync root

| Subfolder | Purpose |
|---|---|
| `nodes/` | Per-node phonebook entries — each node writes its own `meta.json` and heartbeat here. |
| `jobs/` | Active and finished job records, plus per-chunk DCC stdout logs. |
| `templates/examples/` | Read-only example templates that ship with MinRender. Override by dropping your own template with the same `template_id` into `templates/`. |
| `plugins/` | Installable DCC submitters (After Effects, Blender, Cinema 4D) — see [Submit a job](submit.md). |

## Next

Configure the node's [tags and local staging](configure.md), then [submit your first job](submit.md).
