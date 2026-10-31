"""Validate first-party configuration and reject foreign build databases."""
import ast
import os
from pathlib import Path
import shutil

from ide_config import read_jsonc
import projects


def require(condition, path, message):
    if not condition:
        raise ValueError(f'{path}: {message}')


def strings(value):
    return isinstance(value, list) and all(isinstance(item, str) for item in value)


def validate_document(path, document):
    require(isinstance(document, dict), path, 'expected an object')
    name = path.name
    if name == 'c_cpp_properties.json':
        require(document.get('version') == 4, path, 'expected C/C++ version 4')
        profiles = document.get('configurations')
        require(isinstance(profiles, list) and bool(profiles), path, 'configurations must be a nonempty array')
        names = []
        for profile in profiles:
            require(isinstance(profile, dict) and isinstance(profile.get('name'), str), path, 'each configuration needs a name')
            names.append(profile['name'])
            for key in ('compilerPath', 'configurationProvider', 'intelliSenseMode', 'cStandard', 'cppStandard'):
                require(key not in profile or isinstance(profile[key], str), path, f'{key} must be a string')
            for key in ('compileCommands', 'includePath', 'defines', 'compilerArgs', 'forcedInclude'):
                require(key not in profile or strings(profile[key]), path, f'{key} must be a string array')
            if 'browse' in profile:
                browse = profile['browse']
                require(isinstance(browse, dict), path, 'browse must be an object')
                require('path' not in browse or strings(browse['path']), path, 'browse.path must be a string array')
                require('databaseFilename' not in browse or isinstance(browse['databaseFilename'], str), path, 'browse.databaseFilename must be a string')
                require('limitSymbolsToIncludedHeaders' not in browse or isinstance(browse['limitSymbolsToIncludedHeaders'], bool), path, 'browse.limitSymbolsToIncludedHeaders must be boolean')
        require(len(names) == len(set(names)), path, 'duplicate configuration names')
    elif name == 'settings.json':
        require(document.get('sdk.board', 'windows_sim') == 'windows_sim', path, 'unsupported sdk.board')
        require(document.get('sdk.configuration', 'Debug') in ('Debug', 'Release'), path, 'unsupported sdk.configuration')
    elif name == 'project.json':
        require(document.get('schema') == 1, path, 'expected project schema 1')
        require(projects.valid_id(document.get('id')) or document.get('id') == '@PROJECT_ID@', path, 'invalid project ID')
        user = document.get('user_dir')
        require(isinstance(user, str) and not Path(user).is_absolute() and '..' not in Path(user).parts, path, 'user_dir must stay inside the project')
    elif name == 'topology.json':
        cores = document.get('cores')
        require(isinstance(cores, list) and all(isinstance(core, dict) and isinstance(core.get('id'), int)
                and isinstance(core.get('enabled'), bool) for core in cores), path, 'invalid core topology')
        require(len({core['id'] for core in cores}) == len(cores), path, 'duplicate core IDs')
        require([core['id'] for core in cores if core['enabled']] == [0], path, 'only CPU0 may be enabled')
    elif name == 'CMakePresets.json':
        require(isinstance(document.get('version'), int), path, 'preset version must be an integer')
        configured = document.get('configurePresets', [])
        require(isinstance(configured, list) and all(isinstance(item, dict) and isinstance(item.get('name'), str) for item in configured), path, 'invalid configurePresets')
        configured = {item['name'] for item in configured}
        for kind in ('configurePresets', 'buildPresets', 'testPresets'):
            items = document.get(kind, [])
            require(isinstance(items, list) and all(isinstance(item, dict) and isinstance(item.get('name'), str) for item in items), path, f'invalid {kind}')
            require(len({item['name'] for item in items}) == len(items), path, f'duplicate {kind} names')
            for item in items:
                require('configurePreset' not in item or item['configurePreset'] in configured, path, 'unknown configurePreset reference')
    if name == 'tasks.json' or name.endswith('.code-workspace'):
        container = document if name == 'tasks.json' else document.get('tasks', {})
        tasks = container.get('tasks', [])
        require(isinstance(tasks, list), path, 'tasks must be an array')
        labels = []
        for task in tasks:
            require(isinstance(task, dict) and isinstance(task.get('label'), str), path, 'each task needs a label')
            labels.append(task['label'])
            if task['label'].startswith('SDK:'):
                require(isinstance(task.get('command'), str) and strings(task.get('args', [])), path, 'SDK task command/args have invalid types')
        require(len(labels) == len(set(labels)), path, 'duplicate task labels')
        if name.endswith('.code-workspace'):
            require(isinstance(document.get('folders'), list) and all(isinstance(folder, dict) and isinstance(folder.get('path'), str)
                    for folder in document['folders']), path, 'invalid workspace folders')
            validate_launch(path, document.get('launch', {}), set(labels))
    if name == 'launch.json':
        tasks_path = path.with_name('tasks.json')
        tasks = read_jsonc(tasks_path).get('tasks', []) if tasks_path.exists() else []
        validate_launch(path, document, {item.get('label') for item in tasks})


def validate_launch(path, document, labels):
    require(isinstance(document, dict) and isinstance(document.get('configurations', []), list), path, 'invalid launch configurations')
    for launch in document.get('configurations', []):
        require(isinstance(launch, dict), path, 'launch entry must be an object')
        if str(launch.get('name', '')).startswith('SDK:'):
            require(launch.get('preLaunchTask') in labels, path, 'unknown SDK preLaunchTask')
            require(isinstance(launch.get('program'), str) and isinstance(launch.get('cwd'), str), path, 'SDK launch requires program/cwd')


def check(root, *, generated=True, board=None, configuration=None):
    settings_path = root / '.vscode/settings.json'
    settings = read_jsonc(settings_path) if settings_path.exists() else {}
    require(isinstance(settings, dict), settings_path, 'expected an object')
    board = board or settings.get('sdk.board', 'windows_sim')
    configuration = configuration or settings.get('sdk.configuration', 'Debug')
    paths = {root / name for name in ('CMakePresets.json', 'third_party/components.json') if (root / name).exists()}
    for category in ('.vscode', 'boards', 'projects', 'examples', 'templates', 'tools', 'cmake'):
        paths.update(path for path in (root / category).rglob('*') if path.is_file() and path.suffix in ('.json', '.jsonc'))
    if generated:
        paths.update((root / 'workspaces').glob('*.code-workspace'))
    else:
        paths.discard(root / '.vscode/c_cpp_properties.json')
    for path in sorted(paths):
        jsonc = '.vscode' in path.parts or path.suffix in ('.jsonc', '.code-workspace')
        validate_document(path, read_jsonc(path, strict=not jsonc))
    registered = projects.discover(root)
    active_workspaces = {f'{project.id}-windows_sim-{configuration}.code-workspace'
                         for project in registered for configuration in ('debug', 'release')}
    for path in paths:
        if path == root / '.vscode/tasks.json' or path.suffix == '.code-workspace' and path.name in active_workspaces:
            document = read_jsonc(path)
            tasks = document.get('tasks', []) if path.name == 'tasks.json' else document.get('tasks', {}).get('tasks', [])
            for task in tasks:
                if task['label'].startswith('SDK:'):
                    require(task['command'].startswith('${workspaceFolder}/') and task['command'].endswith('/sdk.cmd'), path, 'SDK task must use the workspace-relative sdk.cmd entry; regenerate this workspace')
    for project in registered:
        source = project.directory / '.vscode/c_cpp_properties.json'
        if source.exists():
            names = [item['name'] for item in read_jsonc(source)['configurations']]
            require(names.count(project.id) == 1, source, f'provide exactly one configuration named {project.id!r}')
            # Resolve JSONC env/defaults and enforce per-project database paths,
            # without writing files or depending on an existing build database.
            from ide import project_properties
            from ide_config import compose
            defaults = project_properties(project.id)['configurations'][0]
            output = projects.output_dir(root, project, board, configuration)
            defaults.update({'compileCommands': [(output / 'compile_commands.json').as_posix()],
                             'includePath': [], 'defines': [], 'compilerArgs': [], 'forcedInclude': [],
                             'browse': {'path': [], 'databaseFilename': (root / '.local/intellisense' / project.id / board / configuration / 'browse.vc.db').as_posix()}})
            compose(project, root, defaults, board, configuration, root_view=False)
            generated_path = root / '.vscode/c_cpp_properties.json'
            if generated and generated_path.exists():
                profile = next((item for item in read_jsonc(generated_path)['configurations'] if item['name'] == project.id), {})
                selected = next(item for item in read_jsonc(source)['configurations'] if item['name'] == project.id)
                if selected.get('compilerPath', '${default}') == '${default}':
                    require('compilerPath' not in profile, generated_path, f'{project.id}: stale SDK compilerPath; run SDK: refresh projects')
    python = [path for category in ('tools', 'tests') for path in (root / category).rglob('*.py')]
    for path in python:
        ast.parse(path.read_text(encoding='utf-8-sig'), filename=str(path))
    return {'configuration_files': len(paths), 'python_files': len(python)}


def read_database(root, project, output, *, compiler=None, env=None, check_compiler=False):
    from ide import split_command
    path = output / 'compile_commands.json'
    entries = read_jsonc(path, strict=True)
    require(isinstance(entries, list) and bool(entries), path, 'expected a nonempty compile database')
    result = []
    for entry in entries:
        require(isinstance(entry, dict) and all(isinstance(entry.get(key), str) for key in ('directory', 'file')), path, 'each entry needs file/directory strings')
        directory = Path(entry['directory'])
        require(directory.is_absolute() and directory.resolve().is_relative_to(output.resolve()), path, 'compile directory belongs to another output/checkout; configure this project again')
        source = (directory / entry['file']).resolve()
        require(source.is_relative_to(root.resolve()), path, 'source belongs to another checkout')
        require(not any(source.is_relative_to(root / category) for category in ('projects', 'examples'))
                or source.is_relative_to(project.directory), path, f'contains another project: {source}')
        arguments = entry.get('arguments')
        if arguments is None:
            require(isinstance(entry.get('command'), str), path, 'entry needs command or arguments')
            arguments = split_command(entry['command'])
        require(strings(arguments) and bool(arguments), path, 'arguments must be a nonempty string array')
        if check_compiler:
            from toolchain import same_path
            executable = shutil.which(arguments[0], path=(env or os.environ).get('PATH', ''))
            require(executable is not None, path, f'compiler is unavailable: {arguments[0]}')
            if source.suffix.lower() in ('.c', '.cc', '.cpp', '.cxx', '.c++', '.h', '.hpp', '.hxx'):
                require(compiler is None or same_path(executable, compiler), path, f'compile database uses a different host compiler: {executable}')
        result.append(dict(entry, arguments=arguments))
    return result
