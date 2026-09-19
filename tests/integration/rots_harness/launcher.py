"""Server process handle shared by the harness. The launchers that create one
(local subprocess and container) arrive in the next task.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ServerHandle:
    host: str
    port: int
    log_path: Path
    process: subprocess.Popen
    container_name: str | None = None

    def is_alive(self) -> bool:
        return self.process.poll() is None

    def exit_status(self) -> int | None:
        return self.process.poll()
