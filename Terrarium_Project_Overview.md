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
- [ ] Create a cgroup v2 directory, set `memory.max`, attach the child PID
- [ ] Manually verify: process can't see host PIDs; process dies when it exceeds memory limit
- **Checkpoint:** `./terrarium hatch <mem_limit_mb>` works from the CLI

**Progress notes:**
- CLI so far: `sudo ./isolate hatch <name> [mem_limit_mb]` (`mem_limit_mb` parsed but unused until cgroups land)
- Alpine template extracted to `/root/terrarium/template`; creatures copied to `/root/terrarium/creatures/<name>`, `rm -rf`'d after exit
- Named exit codes in `src/errors.h`
- Env: WSL2 (Ubuntu), kernel 6.18, cgroups v2. Build: `g++ -std=c++17 -Wall -Wextra -o isolate src/isolate.cpp`
- Not yet: `sethostname()` for the UTS demo; pipe handshake so the cgroup limit applies before `exec`

---

## ⏸️ SESSION HANDOFF — pick up here

### What works right now (verified by hand)
- `./isolate hatch bugsy 100` drops into an Alpine `/ #` shell inside new PID + mount + UTS namespaces.
- **PID isolation confirmed:** `ps aux` inside shows only `sh` (PID 1) + the command run; host processes invisible.
- **FS isolation confirmed:** `ls /` is Alpine; a file written inside (`/marker.txt`) does not appear on host; creature dir is `rm -rf`'d on exit; template stays pristine.
- Builds clean with `-Wall -Wextra`. Runs as root (WSL default user is root).

### Repo state
- Branch `main`, **2 commits ahead of origin** (not pushed): `a4c185e` clone skeleton, `960977f` per-creature rootfs + chroot + errors.h.
- `Terrarium_Project_Overview.md` has uncommitted edits (this handoff + Phase 1 checkboxes).
- Files: `src/isolate.cpp` (~95 lines, all logic), `src/errors.h` (ExitCode enum). No Makefile yet.

### One-time environment setup already done
- Alpine 3.24.1 minirootfs → `/root/terrarium/template`
- `build-essential` installed in WSL

### NEXT TASK: memory cgroup (finishes Phase 1)
In `main`, after `clone()` returns `pid`, before `waitpid`:
1. **Check controller is delegated** (terminal, one-time):
   `cat /sys/fs/cgroup/cgroup.subtree_control` — if no `memory`, run `echo +memory > /sys/fs/cgroup/cgroup.subtree_control`
2. Add `write_file(path, text)` helper: `open(O_WRONLY)` → `write()` → `close()`, false on any `-1`. **Not** `fopen`/`fprintf` — cgroup files need a single `write()`.
3. Add `setup_cgroup(pid, mem_limit_mb)`:
   - `mkdir("/sys/fs/cgroup/terrarium-<pid>", 0755)`
   - write `"<N>M\n"` → `<dir>/memory.max`
   - write `"<pid>\n"` → `<dir>/cgroup.procs`
   - return dir path (or "" on failure → new `ExitCode` in errors.h)
4. After `waitpid`: `rmdir(dir)` (works only once the cgroup is empty, i.e. child exited).
5. **Known race, fix after first OOM works:** child `exec`s immediately, so the limit may not be applied before it allocates. Fix = pipe handshake: child `read()`s on a pipe and blocks until parent closes the write end after `setup_cgroup`.

### Then verify (Phase 1 checkpoint)
```sh
./isolate hatch piggy 20
# inside the shell:
yes | tr \\n x | head -c 100m | grep n    # or: cat /dev/zero | head -c 100m > /dev/null
```
Expect the process to be **OOM-killed**. Check from host: `dmesg | tail` shows an OOM kill; parent's `waitpid` status is `WIFSIGNALED` / signal 9. Print that in the parent ("creature went dormant").

### After Phase 1 closes
- Add `sethostname("terrarium")` in `child_fn` (cheap UTS demo).
- Commit, then push the 3 commits.
- Phase 2 starts with a **refactor**: split into `src/cgroup.{h,cpp}`, `src/container.{h,cpp}`, `src/main.cpp` + a Makefile, since the Phase 3 API links against this.

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
