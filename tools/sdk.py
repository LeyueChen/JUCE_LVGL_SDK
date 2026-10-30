"""Reproducible Windows SDK dependency, configuration and build entry point."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'third_party/deps.lock.json'


def run(args, **kwargs):
    print('+', subprocess.list2cmdline([str(a) for a in args]), flush=True)
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def vs_root():
    finder = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not finder.exists():
        raise RuntimeError('Visual Studio C++ tools not found (vswhere missing).')
    result = subprocess.check_output([str(finder), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not result:
        raise RuntimeError('Install Visual Studio Desktop development with C++.')
    return Path(result)


def tool(name):
    explicit = os.environ.get('SDK_' + name.upper())
    if explicit:
        return explicit
    found = shutil.which(name)
    if found:
        return found
    base = vs_root() / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    candidate = base / ('CMake/bin/cmake.exe' if name == 'cmake' else 'Ninja/ninja.exe')
    if not candidate.exists():
        raise RuntimeError(f'{name} missing; install the VS CMake component or set SDK_{name.upper()}.')
    return str(candidate)


def build_env():
    env = {key.upper(): value for key, value in os.environ.items()} if os.name == 'nt' else os.environ.copy()
    if os.name == 'nt' and not env.get('VSCMD_VER'):
        script = vs_root() / 'VC/Auxiliary/Build/vcvars64.bat'
        # cmd.exe needs its own outer quote pair; list2cmdline would escape the inner quotes.
        output = subprocess.check_output(f'cmd.exe /d /s /c ""{script}" >nul && set"', text=True, errors='replace')
        for line in output.splitlines():
            if '=' in line and not line.startswith('='):
                key, value = line.split('=', 1)
                env[key.upper()] = value
    # JUCE bootstraps its own CMake project with an older policy baseline.
    # MSVC appends _CL_ after command-line flags, so helper builds also use /Z7.
    if os.name == 'nt':
        env['_CL_'] = (env.get('_CL_', '') + ' /Z7').strip()
        env['VSLANG'] = '1033'  # Stable MSVC dependency-output prefix for Ninja.
    scratch = ROOT / '.cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    env['TMP'] = env['TEMP'] = str(scratch)
    return env


def source_dir(dep):
    return ROOT / '.cache/sources' / f"{dep['name']}-{dep['commit']}"


def sync_dep(dep, record):
    if not re.fullmatch(r'[a-z0-9_]+', dep['name']) or not re.fullmatch(r'[0-9a-f]{40}', dep['commit']):
        raise RuntimeError('Invalid dependency name or commit in lock file')
    destination = source_dir(dep)
    archive = ROOT / '.cache/archives' / f"{dep['name']}-{dep['commit']}.zip"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        url = f"https://codeload.github.com/{dep['repository']}/zip/{dep['commit']}"
        print(f"Downloading {dep['name']} {dep['version']}", flush=True)
        partial = archive.with_suffix('.part')
        try:
            with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as target:
                shutil.copyfileobj(response, target)
            partial.replace(archive)
        finally:
            partial.unlink(missing_ok=True)
    with archive.open('rb') as source:
        checksum = hashlib.file_digest(source, 'sha256').hexdigest()
    if dep['archive_sha256']:
        if checksum != dep['archive_sha256']:
            raise RuntimeError(f"Archive checksum mismatch: {dep['name']}")
    elif not record:
        raise RuntimeError(f"Missing archive hash for {dep['name']}; maintainers must run sync --record-hashes once.")
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.extract-', dir=destination.parent) as temporary:
            temp = Path(temporary)
            with zipfile.ZipFile(archive) as bundle:
                for member in bundle.infolist():
                    resolved = (temp / member.filename).resolve()
                    if not resolved.is_relative_to(temp.resolve()):
                        raise RuntimeError('Unsafe archive member')
                bundle.extractall(temp)
            roots = list(temp.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise RuntimeError('Expected a single source root')
            for attempt in range(6):
                try:
                    roots[0].replace(destination)
                    break
                except PermissionError:
                    if attempt == 5:
                        raise
                    time.sleep(0.2 * (attempt + 1))
    print(f"Ready {dep['name']} {dep['commit'][:12]}", flush=True)
    return dep['name'], checksum


def sync(args):
    lock = json.loads(LOCK.read_text(encoding='utf-8'))
    selected = [d for d in lock['dependencies'] if d['profile'] in args.profile or d['name'] in args.only]
    if args.only:
        selected = [d for d in lock['dependencies'] if d['name'] in args.only]
    if not selected:
        raise RuntimeError('No dependencies selected')
    hashes = {}
    errors = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = {pool.submit(sync_dep, dep, args.record_hashes): dep for dep in selected}
        for future in concurrent.futures.as_completed(futures):
            try:
                name, digest = future.result()
                hashes[name] = digest
            except Exception as error:
                errors.append(f"{futures[future]['name']}: {error}")
    if args.record_hashes:
        for dep in lock['dependencies']:
            if dep['name'] in hashes:
                dep['archive_sha256'] = hashes[dep['name']]
        LOCK.write_text(json.dumps(lock, indent=2) + '\n', encoding='utf-8')
    if errors:
        raise RuntimeError('\n'.join(errors))


def configure(args, interactive=False):
    from config.generate import generate
    app = (ROOT / args.app).resolve()
    board = ROOT / 'boards' / args.board
    if not app.is_relative_to(ROOT) or not app.is_dir() or not board.is_dir():
        raise RuntimeError('Unknown app or board')
    output = ROOT / 'out' / args.board / app.name / args.config
    generated = output / 'generated'
    generate(ROOT, app, board, generated, interactive)
    if interactive:
        return output
    env = build_env()
    extra = []
    if os.name == 'nt':
        probe = ROOT / '.cache/toolchain/include-probe.c'
        probe.parent.mkdir(parents=True, exist_ok=True)
        probe.write_text('#include <stddef.h>\n', encoding='utf-8')
        compiler = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
        if not compiler:
            raise RuntimeError('MSVC environment did not provide cl.exe')
        result = subprocess.check_output([compiler, '/nologo', '/showIncludes', '/EP', '/utf-8', str(probe)], env=env, stderr=subprocess.STDOUT)
        for line in result.splitlines():
            match = re.match(rb'^(.*?)(?=[A-Za-z]:[\\/])', line)
            if match and match[1].strip():
                try:
                    prefix = match[1].decode('utf-8')
                except UnicodeDecodeError:
                    prefix = match[1].decode('mbcs')
                extra = ['-DSDK_MSVC_INCLUDE_PREFIX=' + prefix.rstrip()]
                break
    run([tool('cmake'), '-S', ROOT, '-B', output, '-G', 'Ninja',
         f'-DCMAKE_MAKE_PROGRAM={tool("ninja")}', f'-DPython_EXECUTABLE={sys.executable}',
         f'-DCMAKE_BUILD_TYPE={args.config}', f'-DSDK_APP_DIR={app}',
         f'-DSDK_GENERATED_DIR={generated}', f'-DSDK_BOARD={args.board}', *extra], env=env)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('doctor')
    project_parser = sub.add_parser('new-project', help='Create a product branch and linked worktree from an SDK tag')
    project_parser.add_argument('name')
    project_parser.add_argument('--directory', type=Path)
    project_parser.add_argument('--sdk-ref', default='sdk/v0.1.0')
    sync_parser = sub.add_parser('sync')
    sync_parser.add_argument('--profile', nargs='+', default=['windows', 'inference', 'algorithms'])
    sync_parser.add_argument('--only', nargs='+', default=[])
    sync_parser.add_argument('--record-hashes', action='store_true')
    for command in ('configure', 'menuconfig', 'build', 'run', 'test'):
        child = sub.add_parser(command)
        child.add_argument('--app', default='user')
        child.add_argument('--board', choices=['windows_sim'], default='windows_sim')
        child.add_argument('--config', choices=['Debug', 'Release'], default='Debug')
        if command == 'run':
            child.add_argument('--smoke-seconds', type=int, default=0)
    args = parser.parse_args()
    if args.command == 'doctor':
        print(json.dumps({'python': sys.executable, 'cmake': tool('cmake'), 'ninja': tool('ninja'), 'visual_studio': str(vs_root())}, indent=2))
    elif args.command == 'sync':
        sync(args)
    elif args.command == 'new-project':
        if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', args.name):
            raise RuntimeError('Project name must use lowercase letters, digits and hyphens')
        if not re.fullmatch(r'sdk/v[0-9]+\.[0-9]+\.[0-9]+(?:[-.][a-zA-Z0-9.-]+)?', args.sdk_ref):
            raise RuntimeError('Select an SDK release tag such as sdk/v0.1.0')
        destination = (args.directory or ROOT.parent / (ROOT.name + '-' + args.name)).resolve()
        if destination.exists() or ROOT.is_relative_to(destination):
            raise RuntimeError('Worktree destination must be a new directory outside repository ancestors')
        run(['git', '-C', ROOT, 'rev-parse', '--verify', args.sdk_ref + '^{commit}'])
        run(['git', '-C', ROOT, 'worktree', 'add', '-b', f'project/{args.name}/main', destination, args.sdk_ref])
        print(f'Product worktree: {destination}\nRun tools/bootstrap.ps1, tools/sdk.ps1 sync, then edit user/.')
    else:
        output = configure(args, interactive=args.command == 'menuconfig')
        if args.command in ('build', 'test', 'run'):
            run([tool('cmake'), '--build', output, '--parallel', '4'], env=build_env())
        if args.command == 'test':
            ctest = Path(tool('cmake')).with_name('ctest.exe' if os.name == 'nt' else 'ctest')
            run([ctest, '--test-dir', output, '--output-on-failure'], env=build_env())
        elif args.command == 'run':
            executable = output / 'bin/lvgl_juce.exe'
            command = [executable]
            if args.smoke_seconds:
                if args.smoke_seconds < 3:
                    raise RuntimeError('Smoke test duration must be at least 3 seconds')
                command += ['--smoke-seconds', str(args.smoke_seconds)]
            run(command, cwd=output)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError, ModuleNotFoundError) as error:
        print(f'SDK error: {error}', file=sys.stderr)
        sys.exit(1)
