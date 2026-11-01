"""Package an already built Windows client; this command does not build or test it."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
from uuid import uuid4

from release_launchers import write_launchers

BEIJING = timezone(timedelta(hours=8))
PYTHON_VERSION = "3.14.8"
PYTHON_SOURCE = "https://www.python.org/ftp/python/3.14.8/python-3.14.8-embed-amd64.zip"
PYTHON_ARCHIVE_SHA256 = "a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310"
# The pinned embeddable distribution used by package_ai_service.py. pythonw.exe
# is deliberately excluded: LocalAiService.hpp launches runtime/python.exe.
RUNTIME_FILES = (
    "python.exe", "python314.dll", "python3.dll", "vcruntime140.dll",
    "vcruntime140_1.dll", "LICENSE.txt", "pyexpat.pyd", "select.pyd",
    "unicodedata.pyd", "winsound.pyd", "_asyncio.pyd", "_bz2.pyd",
    "_ctypes.pyd", "_decimal.pyd", "_elementtree.pyd", "_hashlib.pyd",
    "_lzma.pyd", "_multiprocessing.pyd", "_overlapped.pyd", "_queue.pyd",
    "_remote_debugging.pyd", "_socket.pyd", "_sqlite3.pyd", "_ssl.pyd",
    "_uuid.pyd", "_wmi.pyd", "_zoneinfo.pyd", "_zstd.pyd", "libcrypto-3.dll",
    "libffi-8.dll", "libssl-3.dll", "libtommath.dll", "sqlite3.dll",
    "python314.zip", "python314._pth", "python.cat",
)
AI_FILES = ("gateway.py", "provider_settings.py") + tuple("runtime/" + name for name in RUNTIME_FILES)
SOURCE_MANIFEST_FILES = frozenset(AI_FILES) | {"runtime/pythonw.exe"}


class PackagingError(ValueError):
    pass


def _plain_path(path):
    """Reject links before resolve() can hide them, including linked ancestors."""
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if part.is_symlink() or getattr(part, "is_junction", lambda: False)():
            raise PackagingError(f"Path must not contain a symbolic link or junction: {part}")
    return path


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _source_file(path):
    path = _plain_path(path)
    if not path.is_file() or path.stat().st_size == 0:
        raise PackagingError(f"Required file missing or empty: {path}")
    return path


def _collect_sources(binary):
    executable = _source_file(binary / "luma_studio.exe")
    with executable.open("rb") as handle:
        if handle.read(2) != b"MZ":
            raise PackagingError("luma_studio.exe does not have a Windows executable header")
    manifest_path = _source_file(binary / "ai-service/manifest.json")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (ValueError, UnicodeError) as error:
        raise PackagingError("Invalid AI service manifest") from error
    if not isinstance(manifest, dict) or any(manifest.get(key) != value for key, value in (
        ("python_version", PYTHON_VERSION), ("source", PYTHON_SOURCE),
        ("archive_sha256", PYTHON_ARCHIVE_SHA256),
    )):
        raise PackagingError("AI service manifest does not match the pinned Python runtime")
    hashes = manifest.get("files")
    if not isinstance(hashes, dict) or set(hashes) != SOURCE_MANIFEST_FILES:
        raise PackagingError("AI service manifest must contain exactly the pinned distribution file list")
    sources = {"luma_studio.exe": (executable, _sha256(executable))}
    for name in AI_FILES:
        expected = hashes[name]
        if not isinstance(expected, str) or re.fullmatch(r"[0-9a-f]{64}", expected) is None:
            raise PackagingError(f"Invalid SHA256 for AI service file: {name}")
        source = _source_file(binary / "ai-service" / name)
        if _sha256(source) != expected:
            raise PackagingError(f"AI service file checksum mismatch: {name}")
        sources["ai-service/" + name] = (source, expected)
    # The release contains no unreviewed manifest extras (paths, account data, etc.).
    release_ai_manifest = {
        "python_version": PYTHON_VERSION, "source": PYTHON_SOURCE,
        "archive_sha256": PYTHON_ARCHIVE_SHA256,
        "source_manifest_sha256": _sha256(manifest_path),
        "files": {name: hashes[name] for name in AI_FILES},
        "omitted_unused_files": ["runtime/pythonw.exe"],
    }
    return sources, release_ai_manifest


def _json_file(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def package_release(binary_dir, output_root, *, now=None):
    """Return a new release directory. Existing releases are never overwritten."""
    binary, output = _plain_path(binary_dir), _plain_path(output_root)
    if not binary.is_dir():
        raise PackagingError(f"Binary directory does not exist: {binary}")
    if binary == output or binary in output.parents or output in binary.parents:
        raise PackagingError("Binary directory and release output root must not overlap")
    moment = now if now is not None else datetime.now(BEIJING)
    if moment.tzinfo is None or moment.utcoffset() is None:
        raise PackagingError("Packaging time must include a timezone")
    moment = moment.astimezone(BEIJING)
    destination = output / moment.strftime("%Y-%m-%d_%H-%M-%S")
    if destination.exists() or destination.is_symlink():
        raise PackagingError(f"Release already exists; choose another timestamp: {destination}")
    sources, ai_manifest = _collect_sources(binary)
    output.mkdir(parents=True, exist_ok=True)
    output = _plain_path(output)
    # Inherit the release parent's ACL. tempfile's explicit 0700 Windows ACL
    # can make a newly created directory inaccessible to a sandboxed token.
    staging = output / (".incomplete-release-" + uuid4().hex)
    staging.mkdir()
    try:
        for name, (source, expected) in sources.items():
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(_source_file(source), target)
            if _sha256(target) != expected or _sha256(source) != expected:
                raise PackagingError(f"Source changed during packaging: {name}")
        _json_file(staging / "ai-service/manifest.json", ai_manifest)
        generated = write_launchers(staging)
        if not isinstance(generated, list) or not generated or len(generated) != len(set(generated)):
            raise PackagingError("Launcher generator returned an invalid file list")
        names = list(sources) + ["ai-service/manifest.json"]
        for name in generated:
            if not isinstance(name, str) or Path(name).is_absolute() or ".." in Path(name).parts:
                raise PackagingError("Launcher generator returned an unsafe relative path")
            if name in names:
                raise PackagingError("Launcher generator returned a duplicate path")
            _source_file(staging / name)
            names.append(name)
        files = {name: {"sha256": _sha256(staging / name), "size_bytes": (staging / name).stat().st_size}
                 for name in sorted(names)}
        manifest = {
            "schema_version": 1, "packaged_at": moment.isoformat(timespec="seconds"),
            "timezone": "Asia/Shanghai (UTC+08:00)", "release_directory": destination.name,
            "source_program": {"name": "luma_studio.exe", "sha256": sources["luma_studio.exe"][1]},
            "git_commit": None,
            "git_commit_note": "No verified build-to-commit provenance supplied; current HEAD is not inferred.",
            "build": {"status": "unverified", "note": "Packaging only; no compilation performed."},
            "acceptance": {"status": "unverified", "note": "Awaiting separate acceptance of this exact executable."},
            "files": files,
            "files_note": "SHA256 covers every packaged payload file; manifest.json is excluded from its own hash list.",
        }
        _json_file(staging / "manifest.json", manifest)
        if destination.exists() or destination.is_symlink():
            raise PackagingError(f"Release already exists: {destination}")
        # On Windows rename fails if the destination exists, including an empty
        # directory created by a concurrent packager after the preceding check.
        staging.rename(destination)
        return destination
    finally:
        # Only remove our unique temporary directory, never an existing release.
        if staging.exists():
            _plain_path(staging)
            if staging.parent != output or staging.resolve().parent != output.resolve():
                raise PackagingError("Unsafe staging cleanup path")
            shutil.rmtree(staging)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", required=True, type=Path, help="Already built Release directory with staged AI runtime")
    parser.add_argument("--output-root", required=True, type=Path, help="Release parent, e.g. D:/workspace/luma-live/output/releases")
    args = parser.parse_args(argv)
    try:
        destination = package_release(args.binary_dir, args.output_root)
    except (OSError, PackagingError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
    print(f"Packaged (build and acceptance unverified): {destination}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
