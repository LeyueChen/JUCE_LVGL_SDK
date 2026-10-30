"""Portable SDK frontend: submodules, named projects, builds and isolated workspaces."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import dependencies
import ide
import projects
from toolchain import ROOT, build_env, compiler_arguments, run, tool, vs_root


def configure(args, interactive=False):
    from config.generate import generate
    project = projects.resolve(ROOT, args.project)
    board = ROOT / 'boards' / args.board
    output = projects.output_dir(ROOT, project, args.board, args.config)
    generated = output / 'generated'
    extra_config = project.directory / 'targets' / (args.board + '.conf')
    generate(ROOT, project.user, board, generated, interactive, project_id=project.id,
             extra_fragments=[extra_config] if extra_config.exists() else [])
    if interactive:
        return project, output
    values = json.loads((generated / 'config.json').read_text())
    from check_dependencies import required
    revisions = dependencies.report(ROOT, required(values))
    ide.write_json(generated / 'dependencies.json', revisions)
    env = build_env()
    extra, compiler = compiler_arguments(env)
    run([tool('cmake'), '-S', ROOT, '-B', output, '-G', 'Ninja',
         f'-DCMAKE_MAKE_PROGRAM={tool("ninja")}', f'-DPython_EXECUTABLE={sys.executable}',
         f'-DCMAKE_BUILD_TYPE={args.config}', f'-DSDK_PROJECT_DIR={project.directory}',
         f'-DSDK_GENERATED_DIR={generated}', f'-DSDK_BOARD={args.board}', *extra], env=env)
    workspace = ide.generate(ROOT, project, args.board, args.config, compiler)
    print(f'Workspace: {workspace}')
    return project, output


def clean(args):
    project = projects.resolve(ROOT, args.project)
    output = projects.output_dir(ROOT, project, args.board, args.config).resolve()
    if not output.is_relative_to((ROOT / 'out').resolve()):
        raise RuntimeError('Unsafe build output path')
    if not (output / 'CMakeCache.txt').exists():
        print('No configured build to clean')
        return
    cache = (output / 'CMakeCache.txt').read_text(encoding='utf-8')
    expected = 'SDK_PROJECT_DIR:PATH=' + project.directory.as_posix()
    if expected not in cache.replace('\\', '/').splitlines():
        raise RuntimeError('Build cache belongs to a different project; refusing clean')
    run([tool('cmake'), '--build', output, '--target', 'clean'], env=build_env())
    print(f'Cleaned compiled outputs for {project.id}; runtime data stays in {projects.data_dir(ROOT, project, args.board)}')


def new_project(args):
    if not args.sdk_ref:
        raise RuntimeError('Select a reviewed new-layout SDK tag using --sdk-ref; sdk/v0.1.0 uses the old layout')
    if not re.fullmatch(r'sdk/v[0-9]+\.[0-9]+\.[0-9]+(?:[-.][a-zA-Z0-9.-]+)?', args.sdk_ref):
        raise RuntimeError('Expected an SDK release tag')
    if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', args.name):
        raise RuntimeError('Invalid project name')
    status = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True)
    if status.strip():
        raise RuntimeError('Commit or preserve your current changes before creating a product branch')
    metadata = subprocess.run(['git', '-C', str(ROOT), 'show', args.sdk_ref + ':examples/basic/project.json'], capture_output=True, text=True)
    if metadata.returncode:
        raise RuntimeError('Selected tag does not contain the new project layout')
    run(['git', '-C', ROOT, 'switch', '-c', f'project/{args.name}/main', args.sdk_ref])
    project = projects.scaffold(ROOT, args.name)
    workspace = ide.generate(ROOT, project)
    print(f'Created {project.directory}\nOpen {workspace}; product changes are ready for review and commit.')


def format_sources(check=False):
    files = []
    for directory in ('sdk', 'platforms', 'examples', 'projects', 'templates', 'tests'):
        files.extend(path for path in (ROOT / directory).rglob('*') if path.suffix in ('.c', '.h', '.cpp', '.hpp') or path.name.endswith('.h.in'))
    formatter = tool('clang-format')
    for start in range(0, len(files), 30):
        run([formatter, '--style=file:' + str(ROOT / '.clang-format'), *(['--dry-run', '--Werror'] if check else ['-i']), *files[start:start + 30]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('doctor')
    sub.add_parser('projects')
    fmt = sub.add_parser('format')
    fmt.add_argument('--check', action='store_true')
    new = sub.add_parser('new-project', help='Create a product branch in this checkout from a reviewed SDK tag')
    new.add_argument('name')
    new.add_argument('--sdk-ref')
    sync = sub.add_parser('sync')
    sync.add_argument('--profile', nargs='+', default=['windows', 'inference', 'algorithms'])
    sync.add_argument('--only', nargs='+', default=[])
    for command in ('configure', 'menuconfig', 'build', 'clean', 'run', 'test', 'workspace'):
        child = sub.add_parser(command)
        child.add_argument('--project', default='basic')
        child.add_argument('--board', choices=['windows_sim'], default='windows_sim')
        child.add_argument('--config', choices=['Debug', 'Release'], default='Debug')
        if command == 'run':
            child.add_argument('--smoke-seconds', type=int, default=0)
            child.add_argument('--no-audio', action='store_true')
            child.add_argument('--show-devices', action='store_true')
    args = parser.parse_args()
    if args.command == 'doctor':
        print(json.dumps({'python': sys.executable, 'cmake': tool('cmake'), 'ninja': tool('ninja'), 'clang-format': tool('clang-format'), 'visual_studio': str(vs_root())}, indent=2))
    elif args.command == 'projects':
        print(json.dumps([{'id': project.id, 'path': project.directory.relative_to(ROOT).as_posix()} for project in projects.discover(ROOT)], indent=2))
    elif args.command == 'sync':
        dependencies.sync(ROOT, args.profile, args.only)
    elif args.command == 'new-project':
        new_project(args)
    elif args.command == 'format':
        format_sources(args.check)
    elif args.command == 'clean':
        clean(args)
    else:
        if args.command == 'run' and args.smoke_seconds and args.smoke_seconds < 3:
            raise RuntimeError('Smoke duration must be at least 3 seconds')
        project, output = configure(args, interactive=args.command == 'menuconfig')
        if args.command in ('build', 'test', 'run'):
            run([tool('cmake'), '--build', output, '--parallel', '4'], env=build_env())
        if args.command == 'test':
            ctest = Path(tool('cmake')).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
            run([ctest, '--test-dir', output, '--output-on-failure'], env=build_env())
        elif args.command == 'run':
            command = [output / 'bin/lvgl_juce.exe', '--data-dir', projects.data_dir(ROOT, project, args.board)]
            if args.smoke_seconds:
                command += ['--smoke-seconds', str(args.smoke_seconds)]
            if args.no_audio:
                command += ['--no-audio']
            if args.show_devices:
                command += ['--show-devices']
            run(command, cwd=output)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError, ModuleNotFoundError, ValueError) as error:
        print(f'SDK error: {error}', file=sys.stderr)
        sys.exit(1)
