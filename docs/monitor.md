---
title: Monitor a job
nav_order: 5
---

# Monitor a job

Click any job in the **Jobs** panel to load it into the detail view. Progress bars and the frame grid live-update from the leader as chunks come back from worker nodes.

![Job monitor](/images/mr009.jpg)

## Frame grid colors

The grid shows one cell per frame in the job's range, coloured by state:

| Color | Meaning |
|---|---|
| **grey** | Unassigned — no node has claimed this frame yet. |
| **blue** | Assigned — a node has the frame and is rendering. |
| **dark green** | Local-staging only: the frame is rendered to the node's staging dir but hasn't been copied to the sync root yet. MinRender copies a chunk's files only once the whole chunk completes. |
| **bright green** | Done. With local staging on, the file has also been copied to your render folder. |
| **red** | The chunk containing this frame failed. Use **Retry failed** in the bulk action strip to re-queue. |

## Logs

The Logs panel has three sources, switchable from the dropdown:

- **Monitor Log** — local structured events from the Monitor itself. Filterable by level (Info / Warn / Error).
- A peer — plain-text log streamed from the agent on another node. Refreshes every 3 s.
- **Task Output** — per-chunk stdout from the DCC for the currently-selected job. Pick a chunk on the left, see its raw output on the right.

These logs are also written to disk in the sync root under `jobs/<job_id>/`, so you can `tail -f` them or open them in a text editor if you'd rather inspect outside the Monitor. They stay around until you delete the job from the Monitor.

## Bulk actions

Selecting one or more jobs in the **Jobs** panel reveals the action strip — Pause, Resume, Retry failed, Requeue, Archive, Cancel, Delete — applied to every selected job. Destructive ops (Cancel, Delete) route through a confirmation dialog so a stray click can't wipe a batch.

Right-click a single job for the same actions on just that row.
