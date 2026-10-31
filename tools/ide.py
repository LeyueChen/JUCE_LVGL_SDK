"""Generate one VS Code workspace and one symbol/cache namespace per project."""
import ctypes
import json
import os
from pathlib import Path
import shlex
import tempfile
from datetime import datetime, timezone
from projects import Project, discover, output_dir
from ide_config import compose, read_jsonc


def write_json(path: Path, value):
    text = json.dumps(value, indent=2, ensure_ascii=False) + '\n'
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', dir=path.parent,
                                             prefix=path.name + '.', suffix='.tmp', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(text)
            os.replace(temporary, path)
        finally:
            if temporary and temporary.exists():
                temporary.unlink()


def backup_generated(root, path):
    directory = root / '.local/config-recovery'
    directory.mkdir(parents=True, exist_ok=True)
    name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + path.name
    backup = directory / name
    backup.write_bytes(path.read_bytes())
    print(f'Preserved generated configuration: {backup}')
    return backup


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
    includes = {project.user, project.directory / 'config', root / 'sdk/include', generated, generated / 'protocol'}
    browse = {project.user, project.directory / 'config', root / 'sdk/include', generated}
    database = output / 'compile_commands.json'
    if database.exists():
        from config_check import read_database
        for entry in read_database(root, project, output):
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
    # Project-owned source: initialize only when absent, never rewrite edits.
    if not path.exists():
        write_json(path, project_properties(project.id))


def compact_paths(paths):
    # Parent paths already cover their children for the recursive browse parser.
    result = []
    for path in sorted(paths, key=lambda p: (len(p.parts), p.as_posix())):
        if not any(path.is_relative_to(parent) for parent in result):
            result.append(path)
    return result


def refresh_projects(root: Path, board=None, configuration=None, *, repair=False):
    root = root.resolve()
    settings_path = root / '.vscode/settings.json'
    settings = read_jsonc(settings_path) if settings_path.exists() else {}
    original_settings = dict(settings)
    board = board or settings.get('sdk.board', 'windows_sim')
    configuration = configuration or settings.get('sdk.configuration', 'Debug')
    if board != 'windows_sim' or configuration not in ('Debug', 'Release'):
        raise RuntimeError('Select windows_sim and Debug or Release before refreshing projects')
    registered = discover(root)
    profiles = {}
    for project in sorted(registered, key=lambda p: p.id):
        write_project_properties(project)
        includes, browse, compiler = index_paths(root, project, board, configuration)
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
        profile['compilerPath'] = '${default}'
        profile['compilerArgs'] = []
        profile['forcedInclude'] = []
        profile['defines'] = []
        profile['compileCommands'] = [dynamic_output + '/compile_commands.json']
        profile['includePath'] = [variable_path(path) for path in sorted(includes)]
        profile['browse'] = {
            'path': [variable_path(path) for path in compact_paths(includes | browse)],
            'limitSymbolsToIncludedHeaders': True,
            'databaseFilename': '${workspaceFolder}/.local/intellisense/' + project.id +
                                '/${config:sdk.board}/${config:sdk.configuration}/root/browse.vc.db'
        }
        profile = compose(project, root, profile, board, configuration, root_view=True)
        if profile.get('compilerPath') == '${default}':
            profile.pop('compilerPath')
        # The C/C++ extension removes an empty provider on save. Root settings
        # already disable a default provider, so omit this equivalent value.
        if profile.get('configurationProvider') == '':
            profile.pop('configurationProvider')
        profile['customConfigurationVariables'] = {
            **profile.get('customConfigurationVariables', {}), 'sdkManaged': 'root-project', 'sdkProject': project.id}
        profiles[project.id] = profile
        if not (output / 'compile_commands.json').exists():
            print(f'{project.id}: compile_commands.json not generated; run SDK: configure for this project, then refresh')

    properties_path = root / '.vscode/c_cpp_properties.json'
    from config_check import validate_document
    try:
        properties = read_jsonc(properties_path) if properties_path.exists() else {'version': 4, 'configurations': []}
        if properties_path.exists():
            validate_document(properties_path, properties)
    except ValueError:
        if not repair:
            raise
        backup_generated(root, properties_path)
        properties = {'version': 4, 'configurations': []}
    merged = []
    project_names = set(profiles)
    for existing in properties.get('configurations', []):
        name = existing['name']
        if is_legacy_configuration(existing):
            continue
        if name in project_names:
            # Project names are reserved for the project-owned source, even if
            # a stale/local root entry predates the sdkManaged marker.
            if name in profiles:
                merged.append(profiles.pop(name))
        elif existing.get('customConfigurationVariables', {}).get('sdkManaged') != 'root-project':
            merged.append(existing)
    merged.extend(profiles.values())
    if not merged:
        raise RuntimeError('No project configurations found; create a project.json first')
    properties['configurations'] = merged
    # Preserve the editor's JSONC/formatting when the generated values match.
    validate_document(properties_path, properties)
    try:
        unchanged = properties_path.exists() and read_jsonc(properties_path) == properties
    except ValueError:
        unchanged = False
    if not unchanged:
        write_json(properties_path, properties)

    # The active C/C++ configuration is the root workspace's only project selector.
    for key in ('sdk.project', 'C_Cpp.default.compileCommands', 'C_Cpp.default.includePath',
                'C_Cpp.default.browse.path', 'C_Cpp.default.browse.databaseFilename', 'C_Cpp.intelliSenseCachePath'):
        settings.pop(key, None)
    settings.update({'sdk.board': board, 'sdk.configuration': configuration, 'C_Cpp.intelliSenseCacheSize': 0})
    if settings != original_settings:
        write_json(settings_path, settings)
    return [project.id for project in sorted(registered, key=lambda p: p.id)]


def generate(root: Path, project: Project, board='windows_sim', configuration='Debug', compiler=None):
    root = root.resolve()
    output = output_dir(root, project, board, configuration)
    generated = output / 'generated'
    cache = root / '.local/intellisense' / project.id / board / configuration
    cache.mkdir(parents=True, exist_ok=True)
    includes, browse, compiler = index_paths(root, project, board, configuration, compiler)
    database = output / 'compile_commands.json'
    write_project_properties(project)
    defaults = project_properties(project.id)['configurations'][0]
    defaults.update({'compilerPath': '${default}', 'compilerArgs': [], 'forcedInclude': [], 'defines': [],
                     'compileCommands': [database.as_posix()], 'includePath': sorted(p.as_posix() for p in includes),
                     'browse': {'path': sorted(p.as_posix() for p in browse), 'limitSymbolsToIncludedHeaders': True,
                                'databaseFilename': (cache / 'browse.vc.db').as_posix()}})
    compose(project, root, defaults, board, configuration, root_view=False)
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
    settings['python.defaultInterpreterPath'] = (root / '.venv/Scripts/python.exe').as_posix()
    root_settings = root / '.vscode/settings.json'
    if root_settings.exists():
        common = read_jsonc(root_settings)
        for key in ('files.exclude', 'search.exclude'):
            if key in common:
                settings[key] = common[key]
    tasks = []
    for command in ('build', 'clean', 'run', 'test', 'configure', 'menuconfig', 'workspace'):
        task = {
            'label': 'SDK: ' + command, 'type': 'shell', 'command': str(root / 'sdk.cmd'),
            'args': [command,
                     '--project', str(project.directory), '--board', board, '--config', configuration],
            'options': {'cwd': str(root), 'shell': {'executable': '${env:ComSpec}', 'args': ['/d', '/c']}}, 'problemMatcher': '$msCompile' if command in ('build', 'run', 'test') else []
        }
        if command == 'build':
            task['group'] = {'kind': 'build', 'isDefault': True}
        tasks.append(task)
    tasks.append({
        'label': 'SDK: device settings', 'type': 'shell',
        'command': str(root / 'sdk.cmd'),
        'args': ['run', '--project', str(project.directory),
                 '--board', board, '--config', configuration, '--show-devices'],
        'options': {'cwd': str(root), 'shell': {'executable': '${env:ComSpec}', 'args': ['/d', '/c']}}, 'problemMatcher': '$msCompile'
    })
    tasks.append({
        'label': 'SDK: refresh projects', 'type': 'shell',
        'command': str(root / 'sdk.cmd'),
        'args': ['refresh-projects', '--board', board, '--config', configuration],
        'options': {'cwd': str(root), 'shell': {'executable': '${env:ComSpec}', 'args': ['/d', '/c']}}, 'problemMatcher': []
    })
    tasks.append({
        'label': 'SDK: check config', 'type': 'shell', 'command': str(root / 'sdk.cmd'),
        'args': ['check-config'],
        'options': {'cwd': str(root), 'shell': {'executable': '${env:ComSpec}', 'args': ['/d', '/c']}},
        'problemMatcher': []
    })
    tasks.append({
        'label': 'SDK: setup', 'type': 'shell', 'command': str(root / 'sdk.cmd'),
        'args': ['setup', '--project', str(project.directory), '--board', board, '--config', configuration],
        'options': {'cwd': str(root), 'shell': {'executable': '${env:ComSpec}', 'args': ['/d', '/c']}},
        'problemMatcher': '$msCompile'
    })
    launch = {'version': '0.2.0', 'configurations': [{
        'name': f'SDK: {project.id} / CPU0 FreeRTOS', 'type': 'cppvsdbg', 'request': 'launch',
        'program': str(output / 'bin/lvgl_juce.exe'), 'cwd': str(output),
        'args': ['--data-dir', str(root / '.local/data' / project.id / board)],
        'preLaunchTask': 'SDK: build', 'stopAtEntry': False
    }]}
    workspace = root / 'workspaces' / f'{project.id}-{board}-{configuration.lower()}.code-workspace'
    root_variable = '${workspaceFolder}/' + os.path.relpath(root, project.directory).replace('\\', '/')

    def portable(value):
        if isinstance(value, dict):
            return {key: portable(item) for key, item in value.items()}
        if isinstance(value, list):
            return [portable(item) for item in value]
        if isinstance(value, str):
            for prefix in (str(root), root.as_posix()):
                if value == prefix or value.startswith(prefix + ('\\' if '\\' in prefix else '/')):
                    return root_variable + value[len(prefix):].replace('\\', '/')
        return value

    document = portable({
        'folders': [{'name': project.id, 'path': os.path.relpath(project.directory, workspace.parent).replace('\\', '/')}],
        'settings': settings, 'tasks': {'version': '2.0.0', 'tasks': tasks}, 'launch': launch
    })
    from config_check import validate_document
    validate_document(workspace, document)
    if workspace.exists():
        try:
            validate_document(workspace, read_jsonc(workspace))
        except ValueError:
            backup_generated(root, workspace)
    write_json(workspace, document)
    write_json(cache / 'index-config.json', settings)
    return workspace
