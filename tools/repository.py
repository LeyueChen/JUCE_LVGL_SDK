"""Read-only SDK release audit and local-docs-safe branch switching."""
from pathlib import Path
import subprocess
import re


def git(root: Path, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True, encoding='utf-8').strip()


def check_target(root: Path, ref: str):
    if not ref or ref.startswith('-'):
        raise RuntimeError('Expected a branch or SDK tag')
    commit = git(root, 'rev-parse', '--verify', ref + '^{commit}')
    paths = git(root, 'ls-tree', '-r', '--name-only', commit).splitlines()
    if any(path == 'docs' or path.startswith('docs/') for path in paths):
        raise RuntimeError('Target tracks docs/. Refusing to overwrite local documents; first migrate that branch to the local-only docs policy.')
    if 'examples/basic/project.json' not in paths:
        raise RuntimeError('Target does not contain the current SDK project layout')
    return commit


def switch(root: Path, ref: str, create=None):
    commit = check_target(root, ref)
    if git(root, 'status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('Preserve or commit working changes before switching branches')
    command = ['git', '-C', str(root), 'switch', '--no-overwrite-ignore']
    if create:
        command += ['-c', create, commit]
    else:
        command += [ref]
    subprocess.run(command, check=True)
    print('Branch changed. Run sync to restore pinned submodules, then workspace --project <id>.')


def release_check(root: Path):
    from dependencies import report, components
    check_target(root, 'HEAD')
    if git(root, 'status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('Release candidate has uncommitted changes')
    if subprocess.run(['git', '-C', str(root), 'check-ignore', '-q', 'docs/local-policy-check.md']).returncode:
        raise RuntimeError('docs/ must remain ignored')
    revisions = report(root, {dep['name'] for dep in components(root)})
    version = re.search(r'project\(LVGL_JUCE_SDK VERSION ([0-9]+\.[0-9]+\.[0-9]+)',
                        (root / 'CMakeLists.txt').read_text(encoding='utf-8'))
    if not version:
        raise RuntimeError('Missing SDK version in CMakeLists.txt')
    return {'branch': git(root, 'branch', '--show-current'),
            'commit': git(root, 'rev-parse', 'HEAD'),
            'suggested_tag': 'sdk/v' + version[1],
            'dependencies': len(revisions), 'docs_local_only': True,
            'next': 'Run SDK tests; manually review, merge and create a new SDK tag. No refs were changed.'}
