#!/usr/bin/env python3
"""Create the deterministic, uncompressed .deb used by NovaOS's boot test."""

import io
import pathlib
import sys
import tarfile


def ar_member(name: str, payload: bytes) -> bytes:
    identifier = (name + "/").encode("ascii").ljust(16, b" ")
    header = (
        identifier
        + b"0".ljust(12, b" ")
        + b"0".ljust(6, b" ")
        + b"0".ljust(6, b" ")
        + b"100644".ljust(8, b" ")
        + str(len(payload)).encode("ascii").ljust(10, b" ")
        + b"`\n"
    )
    return header + payload + (b"\n" if len(payload) & 1 else b"")


def make_tar() -> bytes:
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for directory in ("opt", "opt/nova"):
            entry = tarfile.TarInfo(directory)
            entry.type = tarfile.DIRTYPE
            entry.mode = 0o755
            entry.mtime = 0
            archive.addfile(entry)

        payload = b"NovaOS .deb payload\n"
        entry = tarfile.TarInfo("opt/nova/package.txt")
        entry.size = len(payload)
        entry.mode = 0o644
        entry.mtime = 0
        archive.addfile(entry, io.BytesIO(payload))

        entry = tarfile.TarInfo("opt/nova/current")
        entry.type = tarfile.SYMTYPE
        entry.linkname = "/opt/nova/package.txt"
        entry.mode = 0o777
        entry.mtime = 0
        archive.addfile(entry)
    return output.getvalue()


def main() -> None:
    destination = pathlib.Path(sys.argv[1])
    destination.parent.mkdir(parents=True, exist_ok=True)
    empty_tar = bytes(1024)
    package = (
        b"!<arch>\n"
        + ar_member("debian-binary", b"2.0\n")
        + ar_member("control.tar", empty_tar)
        + ar_member("data.tar", make_tar())
    )
    destination.write_bytes(package)


if __name__ == "__main__":
    main()
