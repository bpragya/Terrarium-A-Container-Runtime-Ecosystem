# Terrarium: A Container Runtime Ecosystem

**Timebox:** 36 hours
**Category:** OS/Systems (namespaces, cgroups) + Backend + Infra
**One-liner:** A lightweight Linux container runtime, visualized as a digital terrarium — containers are "creatures," resource limits are their "food supply," and killing a process is watching it go dormant.

---

## Project Goals

1. Build a working container runtime from scratch using Linux namespaces and cgroups v2 — real isolation, not a wrapper around Docker.
2. Expose the runtime through a REST API with persistent state (SQLite).
3. Automate build/test/deploy with CI/CD (GitHub Actions + Docker).
4. Ship a live dashboard that visually demonstrates isolation and resource limiting — something demoable in an interview, not just bullet points.
5. Produce resume bullets and a portfolio artifact that are 100% truthful and defensible under technical questioning.

---

## Scope (What's In / What's Cut)

**In scope:**
- PID namespace, mount namespace, UTS namespace (process isolation, chroot, hostname isolation)
- cgroups v2, memory limit only (`memory.max`), with OOM-kill as the core demo moment
- chroot into a prebuilt minimal rootfs (Alpine) — no custom image layering
- SQLite for creature/container state persistence
- REST API (FastAPI or cpp-httplib) wrapping the runtime binary
- Static-refresh (polling) web dashboard, plain HTML/JS
- GitHub Actions CI: build + GTest unit tests on every push
- Dockerized API layer

**Explicitly cut (to protect the 36-hour budget):**
- Network namespace / networking between containers
- CPU and I/O cgroup controls (memory only)
- Custom image format or overlayfs layering
- Real-time websockets (polling is fine)
- Auth/multi-user support
- Dashboard visual polish beyond "looks intentional"

---

## Build Plan

### Phase 1 — Core Isolation Engine (Hours 0–6)
**Goal:** A working binary that can hatch an isolated, memory-limited process.
- [x] Write C++ program using `clone()` with `CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS` — `src/isolate.cpp`
- [x] Chroot into a prebuilt minimal Alpine rootfs — per-creature copy of the template via `spawn_rootfs()`
- [x] Exec a shell/process inside the new namespace — `child_fn()`: chroot → chdir → mount `/proc` → `execlp("/bin/sh")`
- [x] Create a cgroup v2 directory, set `memory.max`, attach the child PID — `setup_cgroup()` + `write_file()`
- [x] Manually verify: process can't see host PIDs; process dies when it exceeds memory limit — OOM-kill confirmed
- **Checkpoint:** `./terrarium hatch <mem_limit_mb>` works from the CLI — ✅ done as `./isolate hatch <name> [mem_limit_mb]`

**Phase 1 complete.** Verified end to end:
- `x=x; while true; do x="$x$x"; done` inside a 20 MB creature → kernel OOM-kills it
- `dmesg`: `Memory cgroup out of memory: Killed process ... (sh)`, `constraint=CONSTRAINT_MEMCG`, `oom_memcg=/terrarium-<pid>`
- parent prints `creature went dormant (killed by signal 9)` via `WIFSIGNALED`
- cgroup dir + rootfs copy cleaned up on exit

**Progress notes:**
- CLI: `./isolate hatch <name> [mem_limit_mb]` (default 64 MB if omitted)
- Alpine template at `/root/terrarium/template`; creatures copied to `/root/terrarium/creatures/<name>`, `rm -rf`'d after exit
- cgroups at `/sys/fs/cgroup/terrarium-<hostpid>/`, `rmdir`'d after exit
- Named exit codes in `src/errors.h` (through `EXIT_CGROUP = 7`)
- Env: WSL2 (Ubuntu, root user), kernel 6.18, cgroups v2, `memory` already in `subtree_control`. Build: `g++ -std=c++17 -Wall -Wextra -o isolate src/isolate.cpp`
- Deferred polish (non-blocking): `sethostname()` for UTS demo; pipe handshake to close the cgroup-vs-exec race; `/dev` nodes (Alpine minirootfs `/dev` is empty — no `/dev/null`, `/dev/zero`)

---

## ⏸️ SESSION HANDOFF — pick up here

### What works right now (Phase 1 complete, verified by hand)
- `./isolate hatch <name> [mem_limit_mb]` drops into an Alpine `/ #` shell in new PID + mount + UTS namespaces, inside a memory-capped cgroup.
- **PID isolation:** `ps aux` inside shows only `sh` (PID 1) + the command run; host processes invisible.
- **FS isolation:** `ls /` is Alpine; a file written inside does not appear on host; creature dir `rm -rf`'d on exit; template stays pristine.
- **Memory limit:** 20 MB creature + `x=x; while true; do x="$x$x"; done` → kernel OOM-kills it; `dmesg` shows `constraint=CONSTRAINT_MEMCG`, `oom_memcg=/terrarium-<pid>`; parent prints `creature went dormant (killed by signal 9)`.
- Builds clean with `-Wall -Wextra`. Runs as root (WSL default user is root).

### Repo state
- Branch `main`, **2 commits ahead of origin** (not pushed): `a4c185e` clone skeleton, `960977f` per-creature rootfs + chroot + errors.h.
- Uncommitted: `src/isolate.cpp` (cgroup code), `src/errors.h` (`EXIT_CGROUP`), `notes.txt`, this MD. **Commit these next.**
- Files: `src/isolate.cpp` (~150 lines, all logic), `src/errors.h`, `notes.txt`. No Makefile yet (`g++` by hand).

### One-time environment setup already done
- Alpine 3.24.1 minirootfs → `/root/terrarium/template`
- `build-essential` installed in WSL; `memory` controller already in `cgroup.subtree_control`

### NEXT TASK: Phase 2 — Creature model + SQLite persistence
Start with a **refactor** (the Phase 3 API links against this, so it must become a library):
- split `isolate.cpp` → `src/cgroup.{h,cpp}`, `src/container.{h,cpp}` (spawn_rootfs + child_fn + clone/waitpid), `src/main.cpp` (arg parsing only)
- add a `Makefile` for the multi-file build
Then:
1. `Creature` struct/class: `name, pid, mem_limit_mb, status (alive|dormant|released), created_at`
2. SQLite schema `creatures(id, name, pid, mem_limit, status, created_at)`; link `-lsqlite3` (`apt install libsqlite3-dev`)
3. `hatch` writes a row (status `alive`); on `waitpid` return, update status (`dormant` if `WIFSIGNALED`, `released` if clean exit)
4. add `terrarium list` — read all rows, print name/status/pid
- **Checkpoint:** hatch a creature, kill it, restart the binary, `list` still shows it with the right status

### Deferred Phase 1 polish (non-blocking, do when convenient)
- `sethostname("terrarium")` in `child_fn` — cheap UTS demo
- pipe handshake: child `read()`s a pipe, blocks until parent closes write end after `setup_cgroup` — closes the race where the child could allocate before the limit is applied
- `/dev` nodes: `mount tmpfs /dev` + `mknod` for `null`/`zero`/`random`/`urandom` (Alpine minirootfs `/dev` is empty; many commands need `/dev/null`)

### Phase 2 — Creature Model + Persistence (Hours 6–12)
**Goal:** Runtime state survives restarts and is queryable.
- [ ] Design `Creature` class: name, PID, memory limit, status (alive/dormant/released), created_at
- [ ] Create SQLite schema: `creatures(id, name, pid, mem_limit, status, created_at)`
- [ ] Wire CLI to write/read creature state from SQLite
- **Checkpoint:** Can list all creatures and their status from the DB after a restart

### Phase 3 — REST API (Hours 12–20)
**Goal:** Runtime is controllable over HTTP.
- [ ] Stand up FastAPI (or cpp-httplib) server that shells out to / links against the runtime
- [ ] `POST /terrarium/hatch` — create + start a creature
- [ ] `GET /terrarium/:id/vitals` — live memory usage from `/sys/fs/cgroup/.../memory.current`
- [ ] `DELETE /terrarium/:id/release` — kill and clean up
- [ ] `GET /terrarium` — list all creatures
- **Checkpoint:** Full lifecycle controllable via curl/Postman

### Phase 4 — Dashboard (Hours 20–28)
**Goal:** A visual, demoable artifact.
- [ ] Single HTML page, plain JS, polls `GET /terrarium` every 2s
- [ ] Render each creature as a sprite (SVG or emoji) with a live memory bar
- [ ] Creature visibly grays out / goes "dormant" when OOM-killed
- [ ] Basic hatch/release buttons wired to the API
- **Checkpoint:** Can hatch a creature, watch its memory bar climb, and see it go dormant on limit breach — live, on screen

### Phase 5 — CI/CD + Testing (Hours 28–33)
**Goal:** Automated, reproducible build/test/deploy.
- [ ] GTest unit tests: namespace creation succeeds, cgroup limit is enforced, API returns correct status codes
- [ ] GitHub Actions workflow: build C++ binary + run GTest suite on every push
- [ ] Dockerfile for the API layer; confirm `docker build && docker run` works standalone
- **Checkpoint:** Green CI badge on the repo; API runs from a container

### Phase 6 — README + Polish (Hours 33–36)
**Goal:** The project sells itself in under a minute.
- [ ] Record a short GIF/screen capture of the dashboard in action (hatch → grow → OOM-kill)
- [ ] README: what it is, architecture diagram (even a simple one), how to run it, what each phase demonstrates
- [ ] List honest technical details for interview defensibility (see below)
- **Buffer:** Reserve this window loosely — namespace/cgroup debugging tends to run long in Phases 1–3; let it borrow from here if needed

---

## Truthful Resume Bullets (draft — only use once actually built)

- Built a container runtime in C++ using Linux namespaces (PID, mount, UTS) and cgroups v2 for process isolation and memory limiting
- Exposed container lifecycle management through a REST API backed by SQLite for state persistence
- Automated build and test with GitHub Actions CI/CD, containerizing the API layer with Docker
- Built a live monitoring dashboard visualizing per-container memory usage and OOM-kill events in real time

---

## Interview-Defensibility Checklist

Before listing this on a resume, be able to explain without notes:
- [ ] What `clone()` flags you used and what each namespace actually isolates
- [ ] How cgroups v2 differs from v1, and how `memory.max` triggers an OOM-kill
- [ ] Why you chose chroot over a full custom filesystem layer (and what real Docker does differently)
- [ ] How your API layer communicates with the runtime (shelling out vs. linking)
- [ ] One specific bug you hit in namespace/cgroup setup and how you debugged it (this is the question that actually gets asked)

---

## Stretch Goals (only if time remains)
- Add a second cgroup control (CPU) if Phase 1–3 finish early
- Add simple auth to the API
- Track "lineage" — spawning a creature from another as a parent/child relationship in SQLite
