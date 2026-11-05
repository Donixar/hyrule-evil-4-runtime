"""Fetch and unpack the pinned MinHook source into a build directory, never install it."""
import argparse
import hashlib
import io
import os
import urllib.request
import zipfile
from pathlib import Path

URL = 'https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4'
SHA256 = '172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4'
PREFIX = 'minhook-1.3.4/'
# The download fails with an SSL certificate error on this PC; a seeded copy is still SHA-checked.
SEEDS = [Path(p) for p in (os.environ.get('LINK_MINHOOK_ZIP'), 'D:/link-in-re4-work/cache/minhook-v1.3.4.zip') if p]


def seeded_bytes():
    for seed in SEEDS:
        if seed.is_file():
            return seed.read_bytes()
    return None


def verify_existing(build, data):
    """Read the pinned archive in memory and compare existing files; never extract or repair."""
    destination = Path(build).resolve() / 'minhook-src'
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        for member in package.infolist():
            if member.is_dir():
                continue
            if not member.filename.startswith(PREFIX):
                raise SystemExit('unexpected MinHook archive prefix')
            target = destination / member.filename[len(PREFIX):]
            if not target.resolve().is_relative_to(destination.resolve()):
                raise SystemExit('unsafe MinHook archive member')
            if not target.is_file() or target.read_bytes() != package.read(member):
                raise SystemExit(f'existing MinHook source missing or changed: {member.filename}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--verify-existing', action='store_true', help='verify the cached sources without extracting')
    args = parser.parse_args()
    build = args.build.resolve()
    archive = build / 'minhook-v1.3.4.zip'
    if args.verify_existing and not archive.is_file():
        raise SystemExit('existing MinHook archive missing; no download or extraction attempted')
    if not args.verify_existing:
        build.mkdir(parents=True, exist_ok=True)
    data = archive.read_bytes() if archive.exists() else seeded_bytes() or urllib.request.urlopen(URL, timeout=60).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit('MinHook source SHA256 mismatch; no source extracted')
    if args.verify_existing:
        verify_existing(build, data)
        print(f'MinHook v1.3.4 existing sources verified without extraction: {SHA256}')
        return
    if not archive.exists():
        archive.write_bytes(data)
    destination = build / 'minhook-src'
    # Re-extract verified bytes each build; edits to a prior cache cannot enter the DLL.
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        for member in package.infolist():
            if member.is_dir():
                continue
            if not member.filename.startswith(PREFIX):
                raise SystemExit('unexpected MinHook archive prefix')
            relative = Path(member.filename[len(PREFIX):])
            target = destination / relative
            if not target.resolve().is_relative_to(destination.resolve()):
                raise SystemExit('unsafe MinHook archive member')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(package.read(member))
    (build / 'link_runtime_licenses.txt').write_bytes((destination / 'LICENSE.txt').read_bytes())
    print(f'MinHook v1.3.4 verified: {SHA256}')


if __name__ == '__main__':
    main()
