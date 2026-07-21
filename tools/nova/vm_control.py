"""Host VM control for NovaAI.

`VM` runs the project's Makefile "run" target (by default) as a background
child, records its PID in a pidfile and uses that to stop or report status.
It is designed for local development on macOS/Linux.

    from nova.vm_control import VM

    vm = VM()
    vm.on()                 # start, or return the PID if already running
    vm.status               # "running" | "stopped" | "unknown"
    vm.off()                # stop, or succeed quietly if already stopped

`on`/`off`/`toggle` are idempotent: they move the VM to the state you asked
for. `start`/`stop` are the strict forms and report failure when the VM is
already in that state.

Usage (from repo root):
  python3 -m nova.vm_control on
  python3 -m nova.vm_control off
  python3 -m nova.vm_control toggle
  python3 -m nova.vm_control status
  python3 -m nova.vm_control restart
  python3 -m nova.vm_control run -- qemu-system-x86_64 -m 512

Notes / safety:
- The default start command is ["make", "run"]; pass another to VM(command=...).
- The pidfile lives at .cache/vm.pid inside the repository. A VM launched by
  another mechanism (IDE, container) is not managed by this tool.
- Stopping sends SIGINT, then SIGTERM, then SIGKILL as a last resort, so QEMU
  processes are not left running.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional, Sequence

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_START_CMD = ["make", "run"]


def _is_process_running(pid: int) -> bool:
    # A VM we launched ourselves stays visible to kill(pid, 0) as a zombie
    # until it is reaped, so reap it first when the process is our child.
    try:
        reaped, _ = os.waitpid(pid, os.WNOHANG)
    except (ChildProcessError, OSError):
        pass  # not our child; fall through to the signal probe
    else:
        if reaped == pid:
            return False

    try:
        os.kill(pid, 0)
    except OSError:
        return False
    else:
        return True


class VM:
    """A NovaOS guest managed through a pidfile."""

    def __init__(
        self,
        command: Optional[Sequence[str]] = None,
        repo_root: Optional[Path] = None,
        pidfile: Optional[Path] = None,
        logfile: Optional[Path] = None,
    ) -> None:
        self.repo_root = Path(repo_root) if repo_root else REPO_ROOT
        self.command = list(command) if command else list(DEFAULT_START_CMD)
        cache = self.repo_root / ".cache"
        self.pidfile = Path(pidfile) if pidfile else cache / "vm.pid"
        self.logfile = Path(logfile) if logfile else cache / "vm.log"

    def __repr__(self) -> str:
        return f"<VM {self.status} pidfile={self.pidfile}>"

    # -- state ------------------------------------------------------------

    @property
    def pid(self) -> Optional[int]:
        """PID of the running VM, or None if there is none."""
        try:
            pid = int(self.pidfile.read_text())
        except (OSError, ValueError):
            return None
        return pid if _is_process_running(pid) else None

    @property
    def status(self) -> str:
        """One of "running", "stopped" or "unknown" (unreadable pidfile)."""
        if not self.pidfile.exists():
            return "stopped"
        try:
            pid = int(self.pidfile.read_text())
        except (OSError, ValueError):
            return "unknown"
        return "running" if _is_process_running(pid) else "stopped"

    @property
    def is_on(self) -> bool:
        return self.status == "running"

    # -- idempotent controls ----------------------------------------------

    def on(self, command: Optional[Sequence[str]] = None) -> int:
        """Turn the VM on and return its PID, reusing one already running."""
        running = self.pid
        if running is not None:
            return running
        return self.start(command=command)

    def off(self, timeout: int = 10) -> bool:
        """Turn the VM off. An already-stopped VM counts as success."""
        if self.pid is None:
            self.pidfile.unlink(missing_ok=True)
            return True
        return self.stop(timeout=timeout)

    def toggle(self, command: Optional[Sequence[str]] = None) -> str:
        """Flip the VM's state and return the new one ("on" or "off")."""
        if self.is_on:
            self.off()
            return "off"
        self.on(command=command)
        return "on"

    # -- strict controls ---------------------------------------------------

    def start(self, command: Optional[Sequence[str]] = None, detach: bool = True) -> int:
        """Start the VM and return the child PID.

        Detached, the child runs in the background and its PID is written to
        the pidfile. Otherwise this waits for the child and writes no pidfile.
        Raises RuntimeError if a VM is already running.
        """
        running = self.pid
        if running is not None:
            raise RuntimeError(f"VM already running (pid={running})")
        self.pidfile.unlink(missing_ok=True)  # stale

        self.logfile.parent.mkdir(parents=True, exist_ok=True)
        cmd = list(command) if command else self.command
        with open(self.logfile, "ab") as log:
            proc = subprocess.Popen(
                cmd, stdout=log, stderr=subprocess.STDOUT, cwd=self.repo_root
            )

        if detach:
            self.pidfile.write_text(str(proc.pid))
        else:
            proc.wait()
        return proc.pid

    def stop(self, timeout: int = 10) -> bool:
        """Stop the VM in the pidfile. False if there was nothing to stop."""
        pid = self.pid
        if pid is None:
            self.pidfile.unlink(missing_ok=True)
            return False

        # Escalate politely: SIGINT, then SIGTERM, then SIGKILL.
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                break
            except PermissionError:
                return False
            if self._wait_for_exit(pid, timeout):
                self.pidfile.unlink(missing_ok=True)
                return True

        self.pidfile.unlink(missing_ok=True)
        return not _is_process_running(pid)

    def restart(self, command: Optional[Sequence[str]] = None) -> int:
        self.off()
        return self.start(command=command)

    @staticmethod
    def _wait_for_exit(pid: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not _is_process_running(pid):
                return True
            time.sleep(0.2)
        return False


def _main(argv: List[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 1

    action, vm = argv[1], VM()
    if action == "on":
        print(f"VM on pid={vm.on()}; logs -> {vm.logfile}")
    elif action == "off":
        print("VM off" if vm.off() else "Failed to turn VM off")
    elif action == "toggle":
        print(f"VM {vm.toggle()}")
    elif action == "status":
        print(vm.status)
    elif action == "start":
        print(f"Started VM pid={vm.start()}; logs -> {vm.logfile}")
    elif action == "stop":
        print("Stopped" if vm.stop() else "Stop failed or nothing to do")
    elif action == "restart":
        print(f"Restarted VM pid={vm.restart()}; logs -> {vm.logfile}")
    elif action == "run":
        # Start an arbitrary command, e.g. run -- qemu-system-x86_64 -m 512
        if "--" not in argv:
            print("Usage: run -- <command...>")
            return 1
        command = argv[argv.index("--") + 1:]
        if not command:
            print("No command given")
            return 1
        print(f"Started VM pid={vm.start(command=command)}: {' '.join(command)}")
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
