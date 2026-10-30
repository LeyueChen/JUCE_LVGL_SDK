"""Generate one VS Code workspace and one symbol/cache namespace per project."""
import ctypes
import json
import os
from pathlib import Path
import shlex
from projects import Project, output_dir


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


def generate(root: Path, project: Project, board='windows_sim', configuration='Debug', compiler=None):
    root = root.resolve()
    output = output_dir(root, project, board, configuration)
    generated = output / 'generated'
    cache = root / '.local/intellisense' / project.id / board / configuration
    cache.mkdir(parents=True, exist_ok=True)
    includes = {project.user, root / 'sdk/include', generated, generated / 'protocol'}
    browse = {project.user}
    database = output / 'compile_commands.json'
    if database.exists():
        for entry in json.loads(database.read_text(encoding='utf-8')):
            source = Path(entry['file']).resolve()
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
    # The workspace opens the selected project, not the repository root. Root
    # .vscode folder settings would otherwise override workspace settings and
    # redirect product indexing back to the basic example.
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
    properties = {
        'version': 4, 'configurations': [{
            'name': 'Windows SDK (selected project)', 'compilerPath': '${default}',
            'compileCommands': ['${default}'], 'includePath': ['${default}'],
            'defines': ['${default}'], 'configurationProvider': '',
            'intelliSenseMode': 'windows-msvc-x64', 'cStandard': 'c11', 'cppStandard': 'c++20',
            'browse': {'path': ['${default}'], 'limitSymbolsToIncludedHeaders': True, 'databaseFilename': '${default}'}
        }]
    }
    property_path = project.directory / '.vscode/c_cpp_properties.json'
    if property_path.exists() and json.loads(property_path.read_text(encoding='utf-8')) != properties:
        raise RuntimeError(f'Review custom index configuration before regenerating {property_path}')
    write_json(property_path, properties)
    tasks = []
    for command in ('build', 'clean', 'run', 'test', 'configure', 'menuconfig'):
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
