"""First-use Windows SDK setup using only the Python standard library."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import venv

# sdk.cmd uses isolated Python; import only this checkout's SDK tools.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependencies
import projects
import sdk
import toolchain
import config_check
from ide_config import read_jsonc

ROOT = Path(__file__).resolve().parents[1]
PROJECT_COMMANDS = {'build', 'run', 'configure', 'workspace', 'test', 'menuconfig'}
INSTALL_HINT = 'Install Visual Studio/Build Tools: Desktop development with C++, Windows SDK, and CMake tools.'


def run(command, **kwargs):
    print('+ ' + subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def system_tools():
    if os.name != 'nt':
        raise RuntimeError('This SDK implements Windows x64 only.')
    git = shutil.which('git')
    if not git:
        raise RuntimeError('Install Git for Windows and make git available on PATH.')
    try:
        visual_studio = toolchain.vs_root()
        cmake, ninja = toolchain.tool('cmake'), toolchain.tool('ninja')
        version = subprocess.check_output([cmake, '--version'], text=True)
        match = re.search(r'cmake version (\d+)\.(\d+)', version)
        if not match or tuple(map(int, match.groups())) < (3, 25):
            raise RuntimeError('CMake 3.25+ is required.')
        env = toolchain.build_env()
        for name, executable in (('cmake', cmake), ('ninja', ninja)):
            if not shutil.which(executable, path=env.get('PATH', '')):
                raise RuntimeError(f'{name} executable is missing: {executable}')
        if env.get('VSCMD_ARG_TGT_ARCH', '').lower() not in ('x64', 'amd64'):
            raise RuntimeError('Use an x64 MSVC environment, or run from a regular terminal so vcvars64 can prepare it.')
        compiler = shutil.which('cl', path=env.get('PATH', ''))
        sdk_directory = Path(env.get('WINDOWSSDKDIR', ''))
        sdk_version = env.get('WINDOWSSDKVERSION', '').strip('\\/')
        if not compiler or not sdk_version or not (sdk_directory / 'Include' / sdk_version / 'um/Windows.h').is_file():
            raise RuntimeError('MSVC x64 or Windows SDK headers are missing.')
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f'{INSTALL_HINT}\n{error}') from error
    return {'git': git, 'visual_studio': str(visual_studio), 'compiler': compiler,
            'windows_sdk': str(sdk_directory / 'Include' / sdk_version), 'cmake': cmake, 'ninja': ninja}


def locked_packages(root):
    text = (root / 'tools/requirements.txt').read_text(encoding='utf-8')
    packages = dict(re.findall(r'^([a-zA-Z0-9_.-]+)==([^\s;\\]+)', text, re.MULTILINE))
    if not packages:
        raise RuntimeError('tools/requirements.txt must contain pinned, hashed Python requirements.')
    return packages


def environment_status(root):
    directory = root / '.venv'
    if not directory.resolve().is_relative_to(root.resolve()):
        raise RuntimeError('The repository .venv must stay inside this checkout.')
    python = directory / 'Scripts/python.exe'
    if not python.exists():
        return {'ready': False, 'exists': directory.exists(), 'reason': 'Repository Python environment is missing.'}
    probe = '''import importlib.metadata as m, importlib.util, json, struct, sys
packages = {}
for name in json.loads(sys.argv[1]):
    try: packages[name] = m.version(name)
    except m.PackageNotFoundError: packages[name] = None
print(json.dumps({'prefix': sys.prefix, 'version': list(sys.version_info[:3]), 'bits': struct.calcsize('P')*8,
                  'pip': importlib.util.find_spec('pip') is not None, 'packages': packages}))
'''
    try:
        values = json.loads(subprocess.check_output([str(python), '-I', '-c', probe, json.dumps(list(locked_packages(root)))],
                                                   text=True, encoding='utf-8', stderr=subprocess.PIPE))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise RuntimeError('Existing .venv is unusable. Preserve/move it aside and rerun setup; it will not be deleted automatically.') from error
    if Path(values['prefix']).resolve() != directory.resolve() or values['version'] < [3, 11] or values['bits'] != 64:
        raise RuntimeError('Existing .venv has the wrong location/Python version. Preserve/move it aside and rerun setup.')
    missing = {name: version for name, version in locked_packages(root).items() if values['packages'].get(name) != version}
    return {'ready': not missing, 'exists': True, 'reason': 'Pinned Python packages differ.' if missing else '',
            'missing': missing, 'python': str(python), 'pip': values['pip']}


def ensure_environment(root, explicit_setup):
    status = environment_status(root)
    marker = root / '.local/bootstrap/environment.json'
    if not status['ready']:
        if status['exists'] and 'python' not in status:
            raise RuntimeError('Existing .venv is incomplete. Preserve/move it aside before setup.')
        if marker.exists() and not explicit_setup:
            raise RuntimeError('SDK Python requirements changed or are incomplete; run sdk.cmd setup explicitly.')
        if not status['exists']:
            venv.EnvBuilder(with_pip=True).create(root / '.venv')
            status = environment_status(root)
        python = root / '.venv/Scripts/python.exe'
        if not status.get('pip'):
            run([python, '-I', '-m', 'ensurepip'])
        run([python, '-I', '-m', 'pip', 'install', '--cache-dir', root / '.cache/pip',
             '--require-hashes', '-r', root / 'tools/requirements.txt'], cwd=root)
        status = environment_status(root)
        if not status['ready']:
            raise RuntimeError('Pinned host dependencies are still incomplete after installation.')
    marker.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps({'schema': 1, 'root': str(root), 'python': status['python'],
                      'requirements_sha256': hashlib.sha256((root / 'tools/requirements.txt').read_bytes()).hexdigest()}, indent=2) + '\n'
    if not marker.exists() or marker.read_text(encoding='utf-8') != text:
        marker.write_text(text, encoding='utf-8')
    return Path(status['python'])


def selected_dependencies(root, project, board):
    from config.generate import generate
    from check_dependencies import required
    target = project.directory / 'targets' / (board + '.conf')
    values = generate(root, project.user, root / 'boards' / board, root / '.local/bootstrap/config-preview',
                      project_id=project.id, extra_fragments=[target] if target.exists() else [], write_outputs=False)
    return required(values)


def missing_dependencies(root, names):
    selected = [dep for dep in dependencies.components(root) if dep['name'] in names]
    if {dep['name'] for dep in selected} != set(names):
        raise RuntimeError('An enabled SDK dependency is absent from components.json.')
    missing = set()
    initialized = []
    for dep in selected:
        if not (root / dep['path'] / '.git').exists():
            missing.add(dep['name'])
        else:
            # Existing wrong revisions or tracked edits are never reset by setup.
            dependencies.inspect(root, dep)
            initialized.append(dep)
    if initialized:
        output = subprocess.check_output(['git', '-C', str(root), 'submodule', 'status', '--recursive', '--',
                                          *[dep['path'] for dep in initialized]], text=True, encoding='utf-8')
        for line in output.splitlines():
            if line.startswith(('+', 'U')):
                raise RuntimeError('A nested submodule differs from its gitlink; review it before setup.')
            if line.startswith('-'):
                path = line[1:].split()[1]
                for dep in initialized:
                    if path == dep['path'] or path.startswith(dep['path'] + '/'):
                        missing.add(dep['name'])
    return missing


def ide_ready(root, project, board, configuration, *, tools=None):
    output = projects.output_dir(root, project, board, configuration)
    tools = tools or {}
    if not toolchain.validate_cache(output, root, project, compiler=tools.get('compiler'), ninja=tools.get('ninja'),
                                    python=root / '.venv/Scripts/python.exe', cmake=tools.get('cmake'), configuration=configuration):
        return False
    selected_config = toolchain.cache_values(output).get('SDK_LVGL_CONFIG_FILE')
    if not selected_config or not toolchain.same_path(selected_config, projects.lvgl_config(project)):
        return False
    properties = root / '.vscode/c_cpp_properties.json'
    workspace = root / 'workspaces' / f'{project.id}-{board}-{configuration.lower()}.code-workspace'
    if not all(path.is_file() for path in (properties, workspace, output / 'compile_commands.json')):
        return False
    try:
        config_check.read_database(root, project, output, compiler=tools.get('compiler'), check_compiler=True)
        for path in (properties, workspace):
            config_check.validate_document(path, read_jsonc(path))
        profiles = read_jsonc(properties)['configurations']
        selected = next((p for p in profiles if p['name'] == project.id), None)
        if not selected or selected.get('compileCommands') != [f'${{workspaceFolder}}/out/{project.id}/${{config:sdk.board}}/${{config:sdk.configuration}}/compile_commands.json']:
            return False
        source = read_jsonc(project.directory / '.vscode/c_cpp_properties.json')
        source_profile = next(p for p in source['configurations'] if p['name'] == project.id)
        if source_profile.get('compilerPath', '${default}') == '${default}' and 'compilerPath' in selected:
            return False
        document = read_jsonc(workspace)
        if 'C_Cpp.default.compilerPath' in document.get('settings', {}):
            return False
        return all(task.get('command', '').startswith('${workspaceFolder}/') and task['command'].endswith('/sdk.cmd') for task in document['tasks']['tasks']
                   if task['label'].startswith('SDK:'))
    except (ValueError, OSError) as error:
        print(f'IDE configuration needs regeneration: {error}', file=sys.stderr)
        return False


def default_project(root):
    registered = projects.discover(root)
    products = [project for project in registered if project.directory.is_relative_to(root / 'projects')]
    if not registered:
        raise RuntimeError('No SDK projects found.')
    return next((p.id for p in products if p.id == 'audio'), products[0].id if products else registered[0].id)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if sys.version_info < (3, 11) or sys.maxsize <= 2**32:
        raise RuntimeError('64-bit Python 3.11+ is required.')
    if not argv or argv[0] in ('--help', '-h'):
        print('Usage: sdk.cmd setup [--project audio] [--check]\n'
              '       sdk.cmd run|build --project <id> [SDK options]\n'
              '       sdk.cmd doctor\n'
              'First use prepares repository-local Python dependencies and pinned submodules.\n'
              'System tools must already be installed. Other commands are forwarded to tools/sdk.py.')
        return 0
    if '--help' in argv or '-h' in argv:
        if argv[0] != 'setup':
            sdk.main(argv)
            return 0
    parser = argparse.ArgumentParser(prog='sdk.cmd', add_help=argv[0] == 'setup')
    parser.add_argument('command')
    parser.add_argument('--project')
    parser.add_argument('--board', choices=['windows_sim'], default='windows_sim')
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Debug')
    parser.add_argument('--check', action='store_true', help='Check readiness without installing, syncing, configuring or building')
    args, remaining = parser.parse_known_args(argv)
    if args.command not in PROJECT_COMMANDS | {'setup', 'doctor'}:
        sdk.main(argv)
        return 0
    if args.command == 'setup' and remaining:
        parser.error('unrecognized arguments: ' + ' '.join(remaining))
    if args.check and args.command != 'setup':
        parser.error('--check is available only for setup')
    config_check.check(ROOT, generated=False, board=args.board, configuration=args.config)
    tools = system_tools()
    status = environment_status(ROOT)
    if args.command == 'doctor':
        print(json.dumps({'tools': tools, 'python': sys.executable, 'environment': status}, indent=2))
        return 0 if status['ready'] else 1
    args.project = args.project or default_project(ROOT)
    project = projects.resolve(ROOT, args.project)
    projects.lvgl_config(project)
    if args.check and not status['ready']:
        print(json.dumps({'tools': tools, 'project': project.id, 'environment': status, 'ready': False}, indent=2))
        return 1
    python = Path(status['python']) if args.check else ensure_environment(ROOT, args.command == 'setup')
    if Path(sys.prefix).resolve() != (ROOT / '.venv').resolve():
        return subprocess.run([str(python), '-I', str(Path(__file__).resolve()), *argv], cwd=ROOT).returncode
    names = selected_dependencies(ROOT, project, args.board)
    missing = missing_dependencies(ROOT, names)
    ready = ide_ready(ROOT, project, args.board, args.config, tools=tools)
    if args.check:
        print(json.dumps({'tools': tools, 'project': project.id, 'environment_ready': True,
                          'missing_submodules': sorted(missing), 'ide_ready': ready, 'ready': not missing and ready}, indent=2))
        return 0 if not missing and ready else 1
    if missing:
        dependencies.sync(ROOT, [], sorted(missing))
    if args.command == 'setup':
        sdk.main(['workspace', '--project', args.project, '--board', args.board, '--config', args.config])
        print(f'Setup complete for {project.id}. Select it in C/C++ configurations and use SDK: run.')
    else:
        has_project = any(arg == '--project' or arg.startswith('--project=') for arg in argv)
        forwarded = argv if has_project else [*argv, '--project', args.project]
        sdk.main(forwarded, initialize_ide=not ready and args.command in {'run', 'build', 'configure'})
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f'SDK setup error: {error}', file=sys.stderr)
        raise SystemExit(1)
