#!/usr/bin/env python3
"""Stream a Debian package (including data.tar.xz) into a NovaFS v2 image."""

from __future__ import annotations

import argparse
import io
import os
import struct
import tarfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import BinaryIO

SECTOR = 512
SUPER_LBA = 4096
DIRECTORY_LBA = 4097
DIRECTORY_SECTORS = 256
DATA_LBA = 4608
MAX_ENTRIES = 1024
NAME_MAX = 95
FS_VERSION = 2
IMAGE_SIZE = 512 * 1024 * 1024

TYPE_FILE = 1
TYPE_DIRECTORY = 2
TYPE_SYMLINK = 3

SUPER = struct.Struct("<8s6I480s")
ENTRY = struct.Struct("<BBHI96sIQIII")
LEGACY_ENTRY = struct.Struct("<BBH40sIIIII")


@dataclass
class Entry:
    used: int = 0
    kind: int = 0
    mode: int = 0
    owner: int = 0
    name: str = ""
    start: int = 0
    size: int = 0
    capacity: int = 0
    modified: int = 0
    reserved: int = 0

    def encode(self) -> bytes:
        encoded = self.name.encode("utf-8")
        if len(encoded) > NAME_MAX:
            raise ValueError(f"NovaFS path too long: {self.name}")
        return ENTRY.pack(
            self.used,
            self.kind,
            self.mode & 0o777,
            self.owner,
            encoded.ljust(NAME_MAX + 1, b"\0"),
            self.start,
            self.size,
            self.capacity,
            self.modified,
            self.reserved,
        )


class FileSlice(io.RawIOBase):
    def __init__(self, source: BinaryIO, start: int, size: int):
        self.source = source
        self.start = start
        self.size = size
        self.position = 0

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def tell(self) -> int:
        return self.position

    def seek(self, offset: int, whence: int = os.SEEK_SET) -> int:
        if whence == os.SEEK_SET:
            position = offset
        elif whence == os.SEEK_CUR:
            position = self.position + offset
        elif whence == os.SEEK_END:
            position = self.size + offset
        else:
            raise ValueError("invalid seek mode")
        if position < 0:
            raise ValueError("negative seek")
        self.position = min(position, self.size)
        return self.position

    def read(self, size: int = -1) -> bytes:
        if size < 0 or size > self.size - self.position:
            size = self.size - self.position
        self.source.seek(self.start + self.position)
        data = self.source.read(size)
        self.position += len(data)
        return data


class NovaFsImage:
    def __init__(self, image: Path):
        self.path = image
        self.file = image.open("r+b")
        self.file.seek(0, os.SEEK_END)
        if self.file.tell() < IMAGE_SIZE:
            self.file.truncate(IMAGE_SIZE)
        self.total_sectors = self.file.seek(0, os.SEEK_END) // SECTOR
        self.entries = [Entry() for _ in range(MAX_ENTRIES)]
        self._mount_or_migrate()

    def close(self) -> None:
        self.file.close()

    def _read_sector_range(self, lba: int, sectors: int) -> bytes:
        self.file.seek(lba * SECTOR)
        data = self.file.read(sectors * SECTOR)
        if len(data) != sectors * SECTOR:
            raise ValueError("short NovaFS disk read")
        return data

    def _write_super(self) -> None:
        block = SUPER.pack(
            b"NOVA64FS",
            FS_VERSION,
            DIRECTORY_LBA,
            DIRECTORY_SECTORS,
            DATA_LBA,
            MAX_ENTRIES,
            self.total_sectors,
            bytes(480),
        )
        self.file.seek(SUPER_LBA * SECTOR)
        self.file.write(block)

    def _format(self) -> None:
        self.entries = [Entry() for _ in range(MAX_ENTRIES)]
        self._write_super()
        self.file.seek(DIRECTORY_LBA * SECTOR)
        self.file.write(bytes(DIRECTORY_SECTORS * SECTOR))

    def _mount_or_migrate(self) -> None:
        self.file.seek(SUPER_LBA * SECTOR)
        raw = self.file.read(SECTOR)
        if len(raw) != SECTOR or raw[:8] != b"NOVA64FS":
            self._format()
            return
        magic, version, directory_lba, directory_sectors, data_lba, maximum, _, _ = (
            SUPER.unpack(raw)
        )
        del magic
        if version == FS_VERSION:
            if (directory_lba, directory_sectors, data_lba, maximum) != (
                DIRECTORY_LBA,
                DIRECTORY_SECTORS,
                DATA_LBA,
                MAX_ENTRIES,
            ):
                raise ValueError("unsupported NovaFS v2 geometry")
            raw_entries = self._read_sector_range(DIRECTORY_LBA, DIRECTORY_SECTORS)
            for index in range(MAX_ENTRIES):
                values = ENTRY.unpack_from(raw_entries, index * ENTRY.size)
                name = values[4].split(b"\0", 1)[0].decode("utf-8")
                self.entries[index] = Entry(
                    values[0], values[1], values[2], values[3], name,
                    values[5], values[6], values[7], values[8], values[9]
                )
            self._write_super()
            return
        if version != 1 or (directory_lba, directory_sectors, data_lba, maximum) != (
            4097,
            8,
            4128,
            64,
        ):
            raise ValueError(f"unsupported NovaFS version {version}")

        legacy_raw = self._read_sector_range(4097, 8)
        legacy: list[tuple[Entry, bytes]] = []
        for index in range(64):
            values = LEGACY_ENTRY.unpack_from(legacy_raw, index * LEGACY_ENTRY.size)
            if not values[0]:
                continue
            name = values[3].split(b"\0", 1)[0].decode("utf-8")
            entry = Entry(values[0], values[1] or TYPE_FILE, values[2], values[8], name,
                          values[4], values[5], values[6], values[7], 0)
            self.file.seek(entry.start * SECTOR)
            payload = self.file.read(entry.size) if entry.size else b""
            if len(payload) != entry.size:
                raise ValueError(f"failed to back up legacy file {name}")
            legacy.append((entry, payload))

        self._format()
        for old, _ in sorted(
            (item for item in legacy if item[0].kind == TYPE_DIRECTORY),
            key=lambda item: item[0].name.count("/"),
        ):
            self.make_directory(old.name, old.mode, old.owner)
        for old, payload in legacy:
            if old.kind == TYPE_DIRECTORY:
                continue
            if old.kind == TYPE_SYMLINK:
                self.write_symlink(old.name, "/" + payload.decode("utf-8"), old.owner)
            else:
                self.write_bytes(old.name, payload, old.mode, old.owner)
        self.save_directory()

    def _find(self, name: str) -> int | None:
        for index, entry in enumerate(self.entries):
            if entry.used and entry.name == name:
                return index
        return None

    def _reserve(self, name: str, kind: int, mode: int, owner: int) -> int:
        existing = self._find(name)
        if existing is not None:
            if self.entries[existing].kind != kind:
                raise ValueError(f"type conflict for {name}")
            return existing
        for index, entry in enumerate(self.entries):
            if not entry.used:
                self.entries[index] = Entry(1, kind, mode, owner, name)
                return index
        raise ValueError("NovaFS directory table is full")

    def _next_lba(self) -> int:
        return max(
            [DATA_LBA]
            + [entry.start + entry.capacity for entry in self.entries if entry.used]
        )

    @staticmethod
    def normalize_path(raw: str) -> str:
        raw = raw.replace("\\", "/")
        while raw.startswith("./"):
            raw = raw[2:]
        raw = raw.lstrip("/").rstrip("/")
        path = PurePosixPath(raw)
        if not raw or any(part in ("", ".", "..") for part in path.parts):
            raise ValueError(f"unsafe package path: {raw!r}")
        normalized = str(path)
        if len(normalized.encode("utf-8")) > NAME_MAX:
            raise ValueError(f"NovaFS path exceeds {NAME_MAX} bytes: {normalized}")
        return normalized

    def ensure_parents(self, name: str, owner: int = 0) -> None:
        parts = name.split("/")
        for length in range(1, len(parts)):
            self.make_directory("/".join(parts[:length]), 0o755, owner)

    def make_directory(self, name: str, mode: int = 0o755, owner: int = 0) -> None:
        name = self.normalize_path(name)
        self.ensure_parents(name, owner)
        index = self._reserve(name, TYPE_DIRECTORY, mode & 0o777, owner)
        self.entries[index].mode = mode & 0o777
        self.entries[index].owner = owner

    def _write_stream(self, name: str, source: BinaryIO, size: int,
                      kind: int, mode: int, owner: int) -> None:
        name = self.normalize_path(name)
        self.ensure_parents(name, owner)
        index = self._reserve(name, kind, mode & 0o777, owner)
        entry = self.entries[index]
        required = max(1, (size + SECTOR - 1) // SECTOR)
        if entry.capacity < required:
            entry.start = self._next_lba()
            entry.capacity = required
        if entry.start + required > self.total_sectors:
            raise ValueError(f"NovaFS image is full while installing {name}")
        self.file.seek(entry.start * SECTOR)
        remaining = size
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise ValueError(f"short package payload for {name}")
            self.file.write(chunk)
            remaining -= len(chunk)
        padding = (-size) % SECTOR
        if padding:
            self.file.write(bytes(padding))
        entry.kind = kind
        entry.mode = mode & 0o777
        entry.owner = owner
        entry.size = size
        entry.modified += 1

    def write_bytes(self, name: str, payload: bytes,
                    mode: int = 0o644, owner: int = 0) -> None:
        self._write_stream(name, io.BytesIO(payload), len(payload), TYPE_FILE, mode, owner)

    def write_file(self, name: str, source: BinaryIO, size: int,
                   mode: int = 0o644, owner: int = 0) -> None:
        self._write_stream(name, source, size, TYPE_FILE, mode, owner)

    def write_symlink(self, name: str, target: str, owner: int = 0) -> None:
        encoded = target.encode("utf-8")
        if not encoded or len(encoded) > NAME_MAX + 1:
            raise ValueError(f"unsupported symbolic-link target: {target}")
        self._write_stream(name, io.BytesIO(encoded), len(encoded),
                           TYPE_SYMLINK, 0o777, owner)

    def save_directory(self) -> None:
        self.file.seek(DIRECTORY_LBA * SECTOR)
        for entry in self.entries:
            self.file.write(entry.encode())
        self._write_super()
        self.file.flush()
        os.fsync(self.file.fileno())


def find_data_member(package: BinaryIO) -> tuple[str, int, int]:
    package.seek(0)
    if package.read(8) != b"!<arch>\n":
        raise ValueError("not a Debian ar archive")
    while True:
        header = package.read(60)
        if not header:
            break
        if len(header) != 60 or header[58:60] != b"`\n":
            raise ValueError("invalid ar member header")
        name = header[:16].decode("ascii").rstrip(" /")
        size = int(header[48:58].decode("ascii").strip())
        start = package.tell()
        if name.startswith("data.tar"):
            return name, start, size
        package.seek(size + (size & 1), os.SEEK_CUR)
    raise ValueError("Debian package has no data.tar member")


def install(package_path: Path, image_path: Path) -> tuple[int, int, int, int]:
    image = NovaFsImage(image_path)
    files = directories = symlinks = payload_bytes = 0
    try:
        with package_path.open("rb") as package:
            member_name, start, size = find_data_member(package)
            sliced = FileSlice(package, start, size)
            mode = "r:xz" if member_name.endswith(".xz") else "r:*"
            with tarfile.open(fileobj=sliced, mode=mode) as archive:
                for member in archive:
                    name = image.normalize_path(member.name)
                    if member.isdir():
                        image.make_directory(name, member.mode, member.uid)
                        directories += 1
                    elif member.issym():
                        image.write_symlink(name, member.linkname, member.uid)
                        symlinks += 1
                    elif member.isfile():
                        source = archive.extractfile(member)
                        if source is None:
                            raise ValueError(f"cannot read package member {name}")
                        image.write_file(name, source, member.size, member.mode, member.uid)
                        files += 1
                        payload_bytes += member.size
                    elif member.islnk():
                        raise ValueError(f"hard-link package member is not supported: {name}")
            image.save_directory()
    finally:
        image.close()
    return files, directories, symlinks, payload_bytes


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    files, directories, symlinks, size = install(args.package, args.image)
    print(
        f"Installed {files} files, {directories} directories, {symlinks} symlinks "
        f"({size} payload bytes) into {args.image}"
    )


if __name__ == "__main__":
    main()
