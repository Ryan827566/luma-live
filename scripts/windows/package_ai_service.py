"""Stage the pinned x64 AI runtime beside the client; no machine-wide Python install."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

VERSION = "3.14.8"
ARCHIVE = f"python-{VERSION}-embed-amd64.zip"
URL = f"https://www.python.org/ftp/python/{VERSION}/{ARCHIVE}"
# Python.org release page: https://www.python.org/downloads/release/python-3148/
SHA256 = "a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", default="output/next/Release")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--download", action="store_true", help="Fetch the pinned runtime from python.org when absent")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = (root / args.binary_dir).resolve()
    if not (binary / "luma_studio.exe").is_file():
        parser.error("Build luma_studio before packaging its AI service.")
    archive = args.archive or root / "output" / ARCHIVE
    if not archive.is_file():
        if not args.download:
            parser.error("Runtime archive missing. Run with --download once, or supply --archive.")
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(URL, timeout=45) as response:
            data = response.read(32 * 1024 * 1024 + 1)
        if len(data) > 32 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != SHA256:
            raise RuntimeError("Downloaded runtime hash does not match the pinned release.")
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError("Runtime archive checksum mismatch; destination was not changed.")
    service = binary / "ai-service"
    runtime = service / "runtime"
    # Reject directory links before creating or writing anything. Never recursively delete an existing distribution.
    if service.is_symlink() or service.is_junction() or runtime.is_symlink() or runtime.is_junction():
        raise RuntimeError("AI service destination must not be a directory link.")
    manifest_path = service / "manifest.json"
    if manifest_path.is_symlink() or manifest_path.is_junction():
        raise RuntimeError("AI service manifest must not be a link.")
    runtime.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            name = item.filename
            if item.is_dir() or Path(name).name != name or "/" in name or "\\" in name or ":" in name:
                raise RuntimeError("Unexpected runtime archive layout.")
            target = runtime / name
            if target.is_symlink() or target.is_junction():
                raise RuntimeError("Runtime file must not be a link.")
            data = package.read(item)
            if name == "python314._pth":
                data = b"python314.zip\n.\n..\n"
            hashes["runtime/" + name] = hashlib.sha256(data).hexdigest()
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)
    sources = root / "LumaLive/server/ai-gateway/python"
    for name in ("gateway.py", "provider_settings.py"):
        target = service / name
        if target.is_symlink():
            raise RuntimeError("Gateway source destination must not be a link.")
        shutil.copyfile(sources / name, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    manifest = {"python_version": VERSION, "source": URL, "archive_sha256": SHA256, "files": hashes}
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    subprocess.run([str(runtime / "python.exe"), "-I", "-B", "-c",
                    "import ssl, ctypes, gateway, provider_settings; print('Bundled AI runtime imports passed')"],
                   cwd=service, check=True, timeout=20, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    print(f"Packaged AI service: {service}")


if __name__ == "__main__":
    main()
