"""Submodule gitlinks are the only dependency revision authority."""
import json
from pathlib import Path
import re
import subprocess
from toolchain import run


def components(root):
    return json.loads((root / 'third_party/components.json').read_text(encoding='utf-8'))['dependencies']


def recorded_revision(root, dependency):
    result = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--stage', '--', dependency['path']], text=True).strip()
    match = re.fullmatch(r'160000 ([0-9a-f]{40}) 0\t.*', result)
    if not match:
        raise RuntimeError(f"No pinned submodule gitlink for {dependency['name']}")
    return match[1]


def inspect(root, dependency):
    directory = (root / dependency['path']).resolve()
    if not directory.is_relative_to(root) or not (directory / '.git').exists():
        raise RuntimeError(f"Submodule {dependency['name']} is not initialized; run sdk.ps1 sync")
    expected = recorded_revision(root, dependency)
    actual = subprocess.check_output(['git', '-C', str(directory), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != expected:
        raise RuntimeError(f"Submodule {dependency['name']} differs from the recorded gitlink; review or sync it")
    dirty = subprocess.check_output(['git', '-C', str(directory), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
    if dirty:
        raise RuntimeError(f"Submodule {dependency['name']} has tracked modifications; review them before building")
    return {'name': dependency['name'], 'path': dependency['path'], 'commit': actual}


def sync(root, profiles, only):
    selected = [dep for dep in components(root) if dep['name'] in only] if only else [dep for dep in components(root) if dep['profile'] in profiles]
    if not selected or (only and set(only) != {dep['name'] for dep in selected}):
        raise RuntimeError('Unknown or empty dependency selection')
    paths = [dep['path'] for dep in selected]
    for path in paths:
        if (root / path / '.git').exists():
            dirty = subprocess.check_output(['git', '-C', str(root / path), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
            if dirty:
                raise RuntimeError(f'Review tracked submodule changes before sync: {path}')
    run(['git', '-C', root, 'submodule', 'sync', '--recursive', '--', *paths])
    run(['git', '-C', root, 'submodule', 'update', '--init', '--recursive', '--checkout', '--', *paths])
    for dep in selected:
        report = inspect(root, dep)
        print(f"Ready {report['name']} {report['commit'][:12]}")


def report(root, required):
    return [inspect(root, dep) for dep in components(root) if dep['name'] in required]
