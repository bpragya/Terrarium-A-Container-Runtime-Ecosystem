import os
import signal
import sqlite3
import subprocess
from contextlib import closing
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

REPO_ROOT = Path(__file__).resolve().parent.parent
ISOLATE = REPO_ROOT / "isolate"
DASHBOARD = REPO_ROOT / "dashboard"
DB_PATH = "/root/terrarium/terrarium.db"

app = FastAPI(title="Terrarium API")

# Detached `isolate hatch` launchers, kept only so we can reap the zombies.
_launchers: list[subprocess.Popen] = []


def _reap() -> None:
    for p in list(_launchers):
        if p.poll() is not None:
            _launchers.remove(p)


def _query(sql: str, args: tuple = ()) -> list[dict]:
    if not os.path.exists(DB_PATH):
        raise HTTPException(503, "no creatures yet (database not created)")
    with closing(sqlite3.connect(DB_PATH)) as conn:
        conn.row_factory = sqlite3.Row
        return [dict(r) for r in conn.execute(sql, args).fetchall()]


class HatchRequest(BaseModel):
    name: str = Field(min_length=1)
    mem_limit_mb: int = Field(default=64, gt=0)
    cmd: str = ""


@app.get("/terrarium")
def list_creatures() -> list[dict]:
    _reap()
    return _query("SELECT id, name, pid, mem_limit, status FROM creatures ORDER BY id")


@app.get("/terrarium/{name}")
def get_creature(name: str) -> dict:
    rows = _query(
        "SELECT id, name, pid, mem_limit, status FROM creatures WHERE name = ?", (name,)
    )
    if not rows:
        raise HTTPException(404, f"no creature named {name!r}")
    return rows[0]


@app.post("/terrarium/hatch", status_code=201)
def hatch(req: HatchRequest) -> dict:
    _reap()
    if os.path.exists(DB_PATH):
        # `isolate` would fail the UNIQUE(name) insert and the creature would
        # just never appear; reject it here so the caller gets a clear answer.
        if _query("SELECT 1 FROM creatures WHERE name = ?", (req.name,)):
            raise HTTPException(409, f"a creature named {req.name!r} already exists")
    argv = [str(ISOLATE), "hatch", req.name, str(req.mem_limit_mb)]
    if req.cmd:
        argv.append(req.cmd)
    # Detached and not waited on: the launcher runs the creature to completion
    # and records its final status in the DB. Return as soon as it has started.
    _launchers.append(subprocess.Popen(argv, start_new_session=True))
    return {"name": req.name, "mem_limit_mb": req.mem_limit_mb, "cmd": req.cmd}


@app.delete("/terrarium/{name}/release")
def release(name: str) -> dict:
    rows = _query("SELECT pid, status FROM creatures WHERE name = ?", (name,))
    if not rows:
        raise HTTPException(404, f"no creature named {name!r}")
    pid, status = rows[0]["pid"], rows[0]["status"]
    if status != "ALIVE":
        raise HTTPException(409, f"{name} is {status}, not ALIVE")
    if not pid:
        raise HTTPException(409, f"{name} has no pid yet")
    try:
        # SIGKILL, not SIGTERM: the creature's top process is PID 1 of its own
        # PID namespace, and the kernel drops un-handled signals sent to a
        # namespace init from outside — only SIGKILL/SIGSTOP force through.
        # The launcher sees no cgroup OOM and records RELEASED.
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    return {"name": name, "status": "RELEASED"}


# Mounted last so it doesn't shadow the /terrarium API routes above.
# Serving the dashboard from this same origin means the browser makes
# same-origin fetch() calls — no CORS to configure.
app.mount("/", StaticFiles(directory=str(DASHBOARD), html=True), name="dashboard")
