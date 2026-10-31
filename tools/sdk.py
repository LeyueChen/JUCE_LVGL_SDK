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
import repository
import config_check
from toolchain import ROOT, build_env, compiler_arguments, run, tool, vs_root, validate_cache, cache_values


def configure(args, interactive=False):
    from config.generate import generate
    config_check.check(ROOT, generated=False, board=args.board, configuration=args.config)
    project = projects.resolve(ROOT, args.project)
    board = ROOT / 'boards' / args.board
    output = projects.output_dir(ROOT, project, args.board, args.config)
    testing = args.command == 'test'
    if testing:
        output = output.with_name(output.name + '-tests')
    generated = output / 'generated'
    env = build_env() if not interactive else None
    extra, compiler = compiler_arguments(env) if not interactive else ([], None)
    if not interactive:
        configured = validate_cache(output, ROOT, project, compiler=compiler, ninja=tool('ninja'),
                                    python=sys.executable, cmake=tool('cmake'), configuration=args.config)
    extra_config = project.directory / 'targets' / (args.board + '.conf')
    generate(ROOT, project.user, board, generated, interactive, project_id=project.id,
             extra_fragments=[extra_config] if extra_config.exists() else [])
    if interactive:
        return project, output
    values = json.loads((generated / 'config.json').read_text())
    from check_dependencies import required
    revisions = dependencies.report(ROOT, required(values))
    ide.write_json(generated / 'dependencies.json', revisions)
    # CMake compares compiler strings, not file identity. Replacing a cached
    # short path/case variant can clear unrelated cache values on reconfigure.
    compiler_options = [] if configured else [f'-DCMAKE_C_COMPILER={compiler}', f'-DCMAKE_CXX_COMPILER={compiler}']
    run([tool('cmake'), '-S', ROOT, '-B', output, '-G', 'Ninja', *compiler_options,
         f'-DCMAKE_MAKE_PROGRAM={tool("ninja")}', f'-DPython_EXECUTABLE={sys.executable}',
         f'-DCMAKE_BUILD_TYPE={args.config}', f'-DSDK_PROJECT_DIR={project.directory}',
         f'-DSDK_GENERATED_DIR={generated}', f'-DSDK_BOARD={args.board}',
         f'-DBUILD_TESTING={"ON" if testing else "OFF"}',
         f'-DSDK_ENABLE_WINDOW_TESTS={"ON" if testing and args.with_window_tests else "OFF"}', *extra], env=env)
    validate_configuration(project, output, args, values, compiler)
    if args.command == 'workspace':
        workspace = ide.generate(ROOT, project, args.board, args.config, compiler)
        ide.refresh_projects(ROOT, args.board, args.config, repair=True)
        print(f'Workspace: {workspace}')
    return project, output


def validate_configuration(project, output, args, expected, compiler):
    validate_cache(output, ROOT, project, compiler=compiler, ninja=tool('ninja'),
                   python=sys.executable, cmake=tool('cmake'), configuration=args.config)
    cached = cache_values(output)
    if cached.get('SDK_BOARD') != args.board or \
            Path(cached.get('SDK_GENERATED_DIR', '')).resolve() != (output / 'generated').resolve():
        raise RuntimeError(f'{output}: CMake changed the selected board/generated directory; refusing build')
    resolved = json.loads((output / 'generated/config.json').read_text(encoding='utf-8'))
    if resolved != expected:
        raise RuntimeError(f'{output}: CMake changed the selected project configuration; refusing build')
    header = (output / 'generated/sdk_project.h').read_text(encoding='utf-8')
    if f'"{project.id}"' not in header:
        raise RuntimeError(f'{output}: generated project identity differs; refusing build')
    config_check.read_database(ROOT, project, output, compiler=compiler, check_compiler=True)


def clean(args):
    project = projects.resolve(ROOT, args.project)
    output = projects.output_dir(ROOT, project, args.board, args.config).resolve()
    if not output.is_relative_to((ROOT / 'out').resolve()):
        raise RuntimeError('Unsafe build output path')
    if not (output / 'CMakeCache.txt').exists():
        print('No configured build to clean')
        return
    validate_cache(output, ROOT, project, python=ROOT / '.venv/Scripts/python.exe', cmake=tool('cmake'), configuration=args.config)
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
    if not projects.valid_id(args.name):
        raise RuntimeError('Invalid project name')
    status = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True)
    if status.strip():
        raise RuntimeError('Commit or preserve your current changes before creating a product branch')
    metadata = subprocess.run(['git', '-C', str(ROOT), 'show', args.sdk_ref + ':examples/basic/project.json'], capture_output=True, text=True)
    if metadata.returncode:
        raise RuntimeError('Selected tag does not contain the new project layout')
    modules = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', args.sdk_ref + ':cmake/UserModules.cmake'], capture_output=True)
    if modules.returncode:
        raise RuntimeError('Selected SDK tag predates modular project support; publish a reviewed modular SDK release first')
    repository.switch(ROOT, args.sdk_ref, create=f'project/{args.name}/main')
    project = projects.scaffold(ROOT, args.name)
    workspace = ide.generate(ROOT, project)
    ide.refresh_projects(ROOT)
    print(f'Created {project.directory}\nOpen {workspace}; product changes are ready for review and commit.')


def format_sources(check=False):
    files = []
    for directory in ('sdk', 'platforms', 'examples', 'projects', 'templates', 'tests'):
        files.extend(path for path in (ROOT / directory).rglob('*') if path.suffix in ('.c', '.h', '.cpp', '.hpp') or path.name.endswith('.h.in'))
    formatter = tool('clang-format')
    for start in range(0, len(files), 30):
        run([formatter, '--style=file:' + str(ROOT / '.clang-format'), *(['--dry-run', '--Werror'] if check else ['-i']), *files[start:start + 30]])


def test_command(args, output):
    ctest = Path(tool('cmake')).with_name('ctest.exe' if sys.platform == 'win32' else 'ctest')
    command = [ctest, '--test-dir', output]
    # Listing discovers registered names without running any test executable.
    report = json.loads(subprocess.check_output(
        [*command, '--show-only=json-v1'], text=True, encoding='utf-8'))
    available = {test['name'] for test in report['tests']}
    if args.only:
        missing = set(args.only) - available
        if missing:
            raise RuntimeError('Unknown or disabled tests: ' + ', '.join(sorted(missing)) +
                               '. Use test --list; window tests require --with-window-tests.')
        # Exact names, not arbitrary regular expressions or partial matches.
        command += ['--tests-regex', '^(' + '|'.join(re.escape(name) for name in args.only) + ')$']
    if args.list_tests:
        command += ['--show-only']
    else:
        command += ['--output-on-failure', '--no-tests=error']
    return command


def main(argv=None, initialize_ide=False):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('doctor')
    sub.add_parser('check-config', help='Validate first-party configuration without changing files')
    sub.add_parser('projects')
    refresh = sub.add_parser('refresh-projects', help='Refresh root C/C++ project configurations without configuring or building')
    refresh.add_argument('--board', choices=['windows_sim'])
    refresh.add_argument('--config', choices=['Debug', 'Release'])
    sub.add_parser('release-check', help='Read-only audit before a manual SDK release')
    switch = sub.add_parser('switch', help='Switch a branch while protecting ignored local docs')
    switch.add_argument('ref')
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
        if command == 'test':
            child.add_argument('--only', nargs='+', metavar='TEST', help='Run only these exact registered test names')
            child.add_argument('--list', dest='list_tests', action='store_true', help='Configure and list tests without building or running them')
            child.add_argument('--with-window-tests', action='store_true', help='Explicitly include JUCE window, device and viewport tests')
    args = parser.parse_args(argv)
    if args.command == 'check-config':
        print(json.dumps(config_check.check(ROOT), indent=2))
    elif args.command == 'doctor':
        print(json.dumps({'python': sys.executable, 'cmake': tool('cmake'), 'ninja': tool('ninja'), 'clang-format': tool('clang-format'), 'visual_studio': str(vs_root())}, indent=2))
    elif args.command == 'projects':
        print(json.dumps([{'id': project.id, 'path': project.directory.relative_to(ROOT).as_posix()} for project in projects.discover(ROOT)], indent=2))
    elif args.command == 'refresh-projects':
        config_check.check(ROOT, generated=False, board=args.board, configuration=args.config)
        names = ide.refresh_projects(ROOT, args.board, args.config, repair=True)
        print('C/C++ project configurations: ' + ', '.join(names))
    elif args.command == 'release-check':
        print(json.dumps(repository.release_check(ROOT), indent=2))
    elif args.command == 'switch':
        repository.switch(ROOT, args.ref)
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
        if initialize_ide and args.command in ('build', 'run', 'configure'):
            ide.generate(ROOT, project, args.board, args.config)
            ide.refresh_projects(ROOT, args.board, args.config, repair=True)
        selected_test_command = test_command(args, output) if args.command == 'test' else None
        if args.command in ('build', 'run') or (args.command == 'test' and not args.list_tests):
            run([tool('cmake'), '--build', output, '--parallel', '4'], env=build_env())
        if args.command == 'test':
            run(selected_test_command, env=build_env())
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
