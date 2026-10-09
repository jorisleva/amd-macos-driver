"""Download immutable inputs with mandatory SHA-256; never trust the cache alone."""
import hashlib
from pathlib import Path, PurePosixPath
import urllib.request
import zipfile


def digest(path):
    # Apple's Command Line Tools still ship Python 3.9 (file_digest needs 3.11).
    sha256 = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def fetch(item, cache, offline=False):
    if Path(item['file']).name != item['file'] or '\\' in item['file']:
        raise ValueError('Cache input name must be a filename')
    path = Path(cache) / item['file']
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if digest(path) != item['sha256']:
            raise ValueError(f"Corrupt cached input: {path.name}")
        return path
    if offline:
        raise FileNotFoundError(f"Offline input missing: {path.name}")
    if not item['url'].startswith('https://'):
        raise ValueError('Only HTTPS input URLs are accepted')
    request = urllib.request.Request(item['url'], headers={'User-Agent': 'amd-macos-driver-preparation'})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(200 * 1024 * 1024 + 1)
    if len(data) > 200 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != item['sha256']:
        raise ValueError(f"Download checksum/size rejected: {path.name}")
    path.write_bytes(data)
    return path


def extract_zip(path, destination):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path) as archive:
        for member in archive.infolist():
            relative = PurePosixPath(member.filename)
            if relative.is_absolute() or '..' in relative.parts or '\\' in member.filename:
                raise ValueError('Unsafe archive path')
            if (member.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Archive symlink rejected')
            target = (destination / member.filename).resolve()
            if not target.is_relative_to(destination):
                raise ValueError('Archive escapes destination')
        archive.extractall(destination)


def local_output(root, requested):
    """These preparation tools write only beneath this repository's ignored out/."""
    root_path = Path(root).resolve()
    allowed = (root_path / 'out').resolve()
    if not allowed.is_relative_to(root_path):
        raise ValueError('Repository out/ cannot point to an external destination')
    result = Path(requested).resolve()
    if not result.is_relative_to(allowed) or result == allowed:
        raise ValueError('Output must be a child of repository out/; no USB/ESP writes')
    return result
