"""Host VM control helpers for NovaAI.

Provides start_vm, stop_vm, restart_vm and status functions that invoke the
project's Makefile "run" target by default. The implementation is intentionally
small and conservative: it records the child PID in a pidfile and uses that to
stop or report status. It is designed for local development on macOS/Linux.

Usage (from repo root):
  python3 tools/vm_control.py start
  python3 tools/vm_control.py stop
  python3 tools/vm_control.py restart
  python3 tools/vm_control.py status

Notes / safety:
- The default start command is ["make", "run"]; if your workflow uses a
  different command, pass the exact command to start_vm(...).
- The pidfile is written to .cache/vm.pid inside the repository. If the VM is
  launched by another mechanism (IDE, container), this tool will not manage it.
- Stopping sends SIGINT then SIGTERM if needed, and finally SIGKILL as a last
  resort. This avoids leaving QEMU processes running.
"""

from __future__ import annotations

import os
import shlex
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent
PIDFILE = REPO_ROOT / ".cache" / "vm.pid"
LOGFILE = REPO_ROOT / ".cache" / "vm.log"
DEFAULT_START_CMD = ["make", "run"]


def _ensure_cache_dir() -> None:
    (REPO_ROOT / ".cache").mkdir(parents=True, exist_ok=True)


def start_vm(command: Optional[List[str]] = None, detach: bool = True) -> int:
    """Start the VM using `command` (list). Returns the child PID.

    If detach is True the process is started in the background and its PID is
    recorded in the pidfile. If detach is False the current process will wait
    for the child to exit and pidfile will not be written.
    """
    _ensure_cache_dir()
    cmd = command or DEFAULT_START_CMD

    if PIDFILE.exists():
        try:
            existing = int(PIDFILE.read_text())
        except Exception:
            existing = None
        else:
            if existing and _is_process_running(existing):
                raise RuntimeError(f"VM already running (pid={existing})")
            else:
                # stale pidfile
                PIDFILE.unlink()

    # Open logfile for combined stdout/stderr
    logfile = open(LOGFILE, "ab")

    # Start the process
    # If detach, use Popen and return immediately
    proc = subprocess.Popen(cmd, stdout=logfile, stderr=subprocess.STDOUT, cwd=REPO_ROOT)

    pid = proc.pid
    if detach:
        PIDFILE.write_text(str(pid))
    else:
        # Wait for the process to complete in foreground
        try:
            proc.wait()
        finally:
            logfile.close()
    return pid


def _is_process_running(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True


def stop_vm(timeout: int = 10) -> bool:
    """Stop the VM recorded in the pidfile. Returns True if stopped."""
    if not PIDFILE.exists():
        print("No pidfile found; nothing to stop.")
        return False
    try:
        pid = int(PIDFILE.read_text())
    except Exception:
        print("Failed to read pidfile; removing it.")
        PIDFILE.unlink(missing_ok=True)
        return False

    if not _is_process_running(pid):
        print(f"Process {pid} not running; removing pidfile.")
        PIDFILE.unlink(missing_ok=True)
        return False

    # Try graceful termination: SIGINT, then SIGTERM, then SIGKILL
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            break
        except PermissionError:
            print(f"Permission denied sending signal to pid {pid}")
            return False
        # wait for process to exit
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not _is_process_running(pid):
                PIDFILE.unlink(missing_ok=True)
                return True
            time.sleep(0.2)

    # Last resort: SIGKILL
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        PIDFILE.unlink(missing_ok=True)
        return True
    except PermissionError:
        print(f"Permission denied killing pid {pid}")
        return False

    # Wait a short moment
    time.sleep(0.5)
    if not _is_process_running(pid):
        PIDFILE.unlink(missing_ok=True)
        return True

    print(f"Failed to stop process {pid}")
    return False


def restart_vm(command: Optional[List[str]] = None) -> int:
    stop_vm()
    time.sleep(0.5)
    return start_vm(command=command)


def status() -> str:
    if not PIDFILE.exists():
        return "stopped"
    try:
        pid = int(PIDFILE.read_text())
    except Exception:
        return "unknown"
    return "running" if _is_process_running(pid) else "stopped"


def _parse_command_line(argv: List[str]) -> None:
    if len(argv) < 2:
        print(__doc__)
        return
    cmd = argv[1]
    if cmd == "start":
        pid = start_vm()
        print(f"Started VM pid={pid}; logs -> {LOGFILE}")
    elif cmd == "stop":
        ok = stop_vm()
        print("Stopped" if ok else "Stop failed or nothing to do")
    elif cmd == "restart":
        pid = restart_vm()
        print(f"Restarted VM pid={pid}; logs -> {LOGFILE}")
    elif cmd == "status":
        print(status())
    elif cmd == "start-cmd":
        # Allow passing an arbitrary command string after --
        # e.g. python3 tools/vm_control.py start-cmd -- qemu-system-x86_64 -m 512
        if "--" not in argv:
            print("Usage: start-cmd -- <command...>")
            return
        sep = argv.index("--")
        cmd_list = argv[sep + 1 :]
        if not cmd_list:
            print("No command given")
            return
        pid = start_vm(command=cmd_list)
        print(f"Started VM pid={pid} with command: {' '.join(cmd_list)}")
    else:
        print(__doc__)


if __name__ == "__main__":
    _parse_command_line(sys.argv)
