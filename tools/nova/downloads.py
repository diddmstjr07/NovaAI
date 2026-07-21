"""Streaming downloads to the host cache.

Large files never pass through the guest's 8 KiB wire buffer. The bridge stores
them under `downloads/` on the host and streams only progress events, which the
guest renders as a progress bar.
"""

import hashlib
import os
import re
import urllib.parse
from pathlib import Path
from typing import Iterator

from . import net, wire

MAX_DOWNLOAD_BYTES = 512 * 1024 * 1024
CHROME_URL = "https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb"
DEFAULT_DIR = Path(__file__).resolve().parent.parent.parent / "downloads"
# Report at least every 5 percent or 5 MiB so slow links still show movement.
PERCENT_STEP = 5
BYTES_STEP = 5 * 1024 * 1024


def download_dir() -> Path:
    return Path(os.environ.get("NOVA_DOWNLOAD_DIR", str(DEFAULT_DIR)))


def event(state: str, percent: int, received: int, filename: str,
          detail: str = "") -> wire.Reply:
    """One DOWNLOAD progress line: state, percent, bytes, filename, detail."""
    return wire.Reply.fields("DOWNLOAD", state, str(percent), str(received),
                             filename, wire.compact_text(detail))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_to_cache(url: str) -> Iterator[wire.Reply]:
    """Download a URL, yielding progress events as they happen.

    Written to a `.part` file and moved into place only after the expected byte
    count arrives, so an interrupted download never leaves a valid-looking file.
    """
    part_path: Path | None = None
    try:
        requested_url = net.validate_public_url(url)
        raw_name = Path(urllib.parse.urlsplit(requested_url).path).name or "download.bin"
        filename = re.sub(r"[^A-Za-z0-9._-]", "_", raw_name)[:96] or "download.bin"
        target_dir = download_dir()
        target_dir.mkdir(parents=True, exist_ok=True)
        target_path = target_dir / filename
        part_path = target_dir / f".{filename}.part"

        with net.open_url(requested_url, timeout=30) as response:
            net.validate_public_url(response.geturl())
            total_header = response.headers.get("Content-Length")
            total = int(total_header) if total_header and total_header.isdigit() else 0
            if total > MAX_DOWNLOAD_BYTES:
                raise ValueError("Download exceeds the 512 MiB safety limit")
            if target_path.exists() and total and target_path.stat().st_size == total:
                yield event("DONE", 100, total, filename,
                            f"already cached sha256={file_sha256(target_path)}")
                return

            yield event("START", 0, 0, filename,
                        f"size={total}" if total else "size=unknown")
            received = 0
            last_percent = -1
            last_bytes = 0
            digest = hashlib.sha256()
            with part_path.open("wb") as destination:
                while True:
                    chunk = response.read(64 * 1024)
                    if not chunk:
                        break
                    received += len(chunk)
                    if received > MAX_DOWNLOAD_BYTES:
                        raise ValueError("Download exceeds the 512 MiB safety limit")
                    destination.write(chunk)
                    digest.update(chunk)
                    percent = min(99, received * 100 // total) if total else 0
                    if (percent >= last_percent + PERCENT_STEP
                            or received - last_bytes >= BYTES_STEP):
                        yield event("ACTIVE", percent, received, filename)
                        last_percent = percent
                        last_bytes = received
            if total and received != total:
                raise ValueError(f"Incomplete download: {received} of {total} bytes")
            os.replace(part_path, target_path)
            part_path = None
            yield event("DONE", 100, received, filename,
                        f"saved to downloads/{filename} sha256={digest.hexdigest()}")
    except (OSError, *net.NETWORK_ERRORS) as error:
        if part_path and part_path.exists():
            part_path.unlink()
        yield event("ERROR", 0, 0, "download", str(error))
