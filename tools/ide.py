"""Generate one VS Code workspace and one symbol/cache namespace per project."""
import ctypes
import json
import os
from pathlib import Path
import shlex
from projects import Project, discover, output_dir


def write_json(path: Path, value):
    text = json.dumps(value, indent=2, ensure_ascii=False) + '\n'
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')


def split_command(command):
    if os.name != 'nt':
        return shlex.split(command)
    shell = ctypes.windll.shell32
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    argv = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not argv:
        raise RuntimeError('Cannot parse compiler command')
    try:
        return [argv[index] for index in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))


def index_paths(root: Path, project: Project, board, configuration, compiler=None):
    output = output_dir(root, project, board, configuration)
    generated = output / 'generated'
    includes = {project.user, root / 'sdk/include', generated, generated / 'protocol'}
    browse = {project.user, root / 'sdk/include', generated}
    database = output / 'compile_commands.json'
    if database.exists():
        for entry in json.loads(database.read_text(encoding='utf-8')):
            source = Path(entry['file'])
            if not source.is_absolute():
                source = Path(entry['directory']) / source
            source = source.resolve()
            if any(source.is_relative_to(root / category) for category in ('projects', 'examples')) and not source.is_relative_to(project.directory):
                raise RuntimeError(f'Compile database for {project.id} contains another project; configure the selected project first')
            if source.is_relative_to(root):
                browse.add(source.parent)
            arguments = entry.get('arguments') or split_command(entry['command'])
            for index, argument in enumerate(arguments):
                include = None
                if argument in ('-I', '/I') and index + 1 < len(arguments):
                    include = arguments[index + 1]
                elif argument.startswith(('-I', '/I')) and len(argument) > 2:
                    include = argument[2:]
                if include:
                    path = Path(include)
                    includes.add((Path(entry['directory']) / path).resolve() if not path.is_absolute() else path.resolve())
            if compiler is None and arguments:
                compiler = arguments[0]
    else:
        browse.add(root / 'sdk')
    return includes, browse, compiler


def project_properties(name):
    return {
        'version': 4, 'configurations': [{
            'name': name, 'compilerPath': '${default}',
            'compileCommands': ['${default}'], 'includePath': ['${default}'],
            'defines': ['${default}'], 'configurationProvider': '',
            'intelliSenseMode': 'windows-msvc-x64', 'cStandard': 'c11', 'cppStandard': 'c++20',
            'browse': {'path': ['${default}'], 'limitSymbolsToIncludedHeaders': True, 'databaseFilename': '${default}'}
        }]
    }


def is_legacy_configuration(config):
    expected = project_properties('Windows SDK (selected project)')['configurations'][0]
    return dict(config, configurationProvider=config.get('configurationProvider', '')) == expected


def write_project_properties(project):
    path = project.directory / '.vscode/c_cpp_properties.json'
    expected = project_properties(project.id)
    if path.exists():
        existing = json.loads(path.read_text(encoding='utf-8'))
        configs = existing.get('configurations', [])
        if existing != expected and not (len(configs) == 1 and is_legacy_configuration(configs[0]) and
                                         set(existing) <= {'version', 'configurations'}):
            print(f'Preserved custom index configuration: {path}')
            return
    write_json(path, expected)


def compact_paths(paths):
    # Parent paths already cover their children for the recursive browse parser.
    result = []
    for path in sorted(paths, key=lambda p: (len(p.parts), p.as_posix())):
        if not any(path.is_relative_to(parent) for parent in result):
            result.append(path)
    return result


def refresh_projects(root: Path, board=None, configuration=None):
    root = root.resolve()
    settings_path = root / '.vscode/settings.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8')) if settings_path.exists() else {}
    board = board or settings.get('sdk.board', 'windows_sim')
    configuration = configuration or settings.get('sdk.configuration', 'Debug')
    if board != 'windows_sim' or configuration not in ('Debug', 'Release'):
        raise RuntimeError('Select windows_sim and Debug or Release before refreshing projects')
    registered = discover(root)
    profiles = {}
    for project in sorted(registered, key=lambda p: p.id):
        includes, browse, _ = index_paths(root, project, board, configuration)
        output = output_dir(root, project, board, configuration)
        # A root window must not open the database used by a dedicated workspace.
        (root / '.local/intellisense' / project.id / board / configuration / 'root').mkdir(parents=True, exist_ok=True)
        dynamic_output = '${workspaceFolder}/out/' + project.id + '/${config:sdk.board}/${config:sdk.configuration}'

        def variable_path(path):
            if path.is_relative_to(output):
                suffix = path.relative_to(output).as_posix()
                return dynamic_output if suffix == '.' else dynamic_output + '/' + suffix
            if path.is_relative_to(root):
                return '${workspaceFolder}/' + path.relative_to(root).as_posix()
            return path.as_posix()

        profile = project_properties(project.id)['configurations'][0]
        profile['compileCommands'] = [dynamic_output + '/compile_commands.json']
        profile['includePath'] = [variable_path(path) for path in sorted(includes)]
        profile['browse'] = {
            'path': [variable_path(path) for path in compact_paths(includes | browse)],
            'limitSymbolsToIncludedHeaders': True,
            'databaseFilename': '${workspaceFolder}/.local/intellisense/' + project.id +
                                '/${config:sdk.board}/${config:sdk.configuration}/root/browse.vc.db'
        }
        profile['customConfigurationVariables'] = {'sdkManaged': 'root-project', 'sdkProject': project.id}
        profiles[project.id] = profile

    properties_path = root / '.vscode/c_cpp_properties.json'
    properties = json.loads(properties_path.read_text(encoding='utf-8')) if properties_path.exists() else {'version': 4}
    merged = []
    for existing in properties.get('configurations', []):
        name = existing['name']
        if is_legacy_configuration(existing):
            continue
        if existing.get('customConfigurationVariables', {}).get('sdkManaged') == 'root-project':
            if name in profiles:
                generated = profiles.pop(name)
                generated['customConfigurationVariables'] = {
                    **existing.get('customConfigurationVariables', {}), **generated['customConfigurationVariables']}
                # Keep user-supplied compiler options and other non-index fields.
                owned = ('compileCommands', 'includePath', 'browse', 'customConfigurationVariables')
                merged.append(dict(existing, **{key: generated[key] for key in owned}))
        else:
            merged.append(existing)
            if name in profiles:
                profiles.pop(name)
                print(f'Preserved custom root index configuration: {name}')
    merged.extend(profiles.values())
    if not merged:
        raise RuntimeError('No project configurations found; create a project.json first')
    properties['configurations'] = merged
    write_json(properties_path, properties)

    # The active C/C++ configuration is the root workspace's only project selector.
    for key in ('sdk.project', 'C_Cpp.default.compileCommands', 'C_Cpp.default.includePath',
                'C_Cpp.default.browse.path', 'C_Cpp.default.browse.databaseFilename', 'C_Cpp.intelliSenseCachePath'):
        settings.pop(key, None)
    settings.update({'sdk.board': board, 'sdk.configuration': configuration, 'C_Cpp.intelliSenseCacheSize': 0})
    write_json(settings_path, settings)
    for project in registered:
        write_project_properties(project)
    return [project.id for project in sorted(registered, key=lambda p: p.id)]


def generate(root: Path, project: Project, board='windows_sim', configuration='Debug', compiler=None):
    root = root.resolve()
    output = output_dir(root, project, board, configuration)
    generated = output / 'generated'
    cache = root / '.local/intellisense' / project.id / board / configuration
    cache.mkdir(parents=True, exist_ok=True)
    includes, browse, compiler = index_paths(root, project, board, configuration, compiler)
    database = output / 'compile_commands.json'
    # Dedicated workspaces open only the selected project and retain its own
    # persistent cache; root workspaces instead select among named profiles.
    includes = sorted(Path(path).as_posix() for path in includes)
    browse = sorted(Path(path).as_posix() for path in browse)
    settings = {
        'sdk.project': project.id, 'sdk.board': board, 'sdk.configuration': configuration,
        'cmake.configureOnOpen': False,
        'C_Cpp.default.configurationProvider': '',
        'C_Cpp.default.compileCommands': [database.as_posix()],
        'C_Cpp.default.includePath': includes,
        'C_Cpp.default.browse.path': browse,
        'C_Cpp.default.browse.limitSymbolsToIncludedHeaders': True,
        'C_Cpp.default.browse.databaseFilename': (cache / 'browse.vc.db').as_posix(),
        'C_Cpp.intelliSenseCachePath': (cache / 'ipch').as_posix(),
        'C_Cpp.clang_format_style': 'file',
        'C_Cpp.formatting': 'clangFormat',
        '[c]': {'editor.defaultFormatter': 'ms-vscode.cpptools', 'editor.formatOnSave': True},
        '[cpp]': {'editor.defaultFormatter': 'ms-vscode.cpptools', 'editor.formatOnSave': True},
    }
    if compiler:
        settings['C_Cpp.default.compilerPath'] = compiler
    settings['python.defaultInterpreterPath'] = (root / '.venv/Scripts/python.exe').as_posix()
    root_settings = root / '.vscode/settings.json'
    if root_settings.exists():
        common = json.loads(root_settings.read_text(encoding='utf-8'))
        for key in ('files.exclude', 'search.exclude'):
            if key in common:
                settings[key] = common[key]
    write_project_properties(project)
    tasks = []
    for command in ('build', 'clean', 'run', 'test', 'configure', 'menuconfig', 'workspace'):
        task = {
            'label': 'SDK: ' + command, 'type': 'process', 'command': str(root / '.venv/Scripts/python.exe'),
            'args': [str(root / 'tools/sdk.py'), command,
                     '--project', str(project.directory), '--board', board, '--config', configuration],
            'options': {'cwd': str(root)}, 'problemMatcher': '$msCompile' if command in ('build', 'run', 'test') else []
        }
        if command == 'build':
            task['group'] = {'kind': 'build', 'isDefault': True}
        tasks.append(task)
    tasks.append({
        'label': 'SDK: device settings', 'type': 'process',
        'command': str(root / '.venv/Scripts/python.exe'),
        'args': [str(root / 'tools/sdk.py'), 'run', '--project', str(project.directory),
                 '--board', board, '--config', configuration, '--show-devices'],
        'options': {'cwd': str(root)}, 'problemMatcher': '$msCompile'
    })
    tasks.append({
        'label': 'SDK: refresh projects', 'type': 'process',
        'command': str(root / '.venv/Scripts/python.exe'),
        'args': [str(root / 'tools/sdk.py'), 'refresh-projects', '--board', board, '--config', configuration],
        'options': {'cwd': str(root)}, 'problemMatcher': []
    })
    launch = {'version': '0.2.0', 'configurations': [{
        'name': f'SDK: {project.id} / CPU0 FreeRTOS', 'type': 'cppvsdbg', 'request': 'launch',
        'program': str(output / 'bin/lvgl_juce.exe'), 'cwd': str(output),
        'args': ['--data-dir', str(root / '.local/data' / project.id / board)],
        'preLaunchTask': 'SDK: build', 'stopAtEntry': False
    }]}
    workspace = root / 'workspaces' / f'{project.id}-{board}-{configuration.lower()}.code-workspace'
    write_json(workspace, {
        'folders': [{'name': project.id, 'path': os.path.relpath(project.directory, workspace.parent).replace('\\', '/')}],
        'settings': settings, 'tasks': {'version': '2.0.0', 'tasks': tasks}, 'launch': launch
    })
    write_json(cache / 'index-config.json', settings)
    return workspace
