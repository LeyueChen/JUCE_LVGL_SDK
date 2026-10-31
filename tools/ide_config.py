"""Read project-owned JSONC and compose workspace-specific IntelliSense views."""
from copy import deepcopy
import json
import os
from pathlib import Path
import re


def read_jsonc(path: Path, *, strict=False):
    text = path.read_text(encoding='utf-8-sig')
    chars = list(text)
    index = 0
    quoted = False
    while index < len(chars):
        char = text[index]
        if quoted:
            if char == '\\':
                index += 2
                continue
            if char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif text.startswith('//', index) or text.startswith('/*', index):
            if text.startswith('//', index):
                end = text.find('\n', index)
                end = len(text) if end < 0 else end
            else:
                end = text.find('*/', index + 2)
                if end < 0:
                    raise ValueError(f'{path}: unterminated JSONC comment')
                end += 2
            for pos in range(index, end):
                if chars[pos] not in '\r\n':
                    chars[pos] = ' '
            index = end
            continue
        index += 1
    text = ''.join(chars)
    quoted = False
    index = 0
    while index < len(chars):
        if quoted:
            if text[index] == '\\':
                index += 2
                continue
            if text[index] == '"':
                quoted = False
        elif text[index] == '"':
            quoted = True
        elif text[index] == ',':
            following = index + 1
            while following < len(text) and text[following].isspace():
                following += 1
            if following < len(text) and text[following] in ']}':
                chars[index] = ' '
        index += 1
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f'duplicate key {key!r}')
            result[key] = value
        return result

    try:
        return json.loads(path.read_text(encoding='utf-8-sig') if strict else ''.join(chars), object_pairs_hook=unique)
    except ValueError as error:
        raise ValueError(f'{path}: {error}') from error


def compose(project, root: Path, defaults, board, configuration, root_view):
    source = project.directory / '.vscode/c_cpp_properties.json'
    document = read_jsonc(source)
    if not isinstance(document, dict) or document.get('version') != 4:
        raise ValueError(f'{source}: expected C/C++ configuration version 4')
    configs = document.get('configurations', [])
    if not isinstance(configs, list) or not all(isinstance(item, dict) for item in configs):
        raise ValueError(f'{source}: configurations must be an array of objects')
    selected = [item for item in configs if item.get('name') == project.id]
    if len(selected) != 1:
        raise ValueError(f'{source}: provide exactly one configuration named {project.id!r}')
    environment = document.get('env', {})
    if not isinstance(environment, dict) or not all(isinstance(value, str) or
            isinstance(value, list) and all(isinstance(item, str) for item in value) for value in environment.values()):
        raise ValueError(f'{source}: env values must be strings or string arrays')
    token = re.compile(r'\$\{([^}]+)\}')

    def local_variables(value, stack=()):
        if isinstance(value, list):
            result = []
            for item in value:
                resolved = local_variables(item, stack)
                result.extend(resolved if isinstance(resolved, list) else [resolved])
            return result
        matches = list(token.finditer(value))
        for match in reversed(matches):
            name = match[1]
            key = name[4:] if name.startswith('env:') else name
            if key in environment:
                if key in stack:
                    raise ValueError(f'{source}: cyclic env variable {key!r}')
                replacement = local_variables(environment[key], (*stack, key))
            elif name.startswith(('env:', 'config:')) or name in (
                    'workspaceFolder', 'workspaceFolderBasename', 'userHome', 'pathSeparator', 'execPath'):
                continue
            elif name in os.environ:
                replacement = os.environ[name]
            else:
                raise ValueError(f'{source}: unresolved variable {name!r}')
            if isinstance(replacement, list):
                if match.span() != (0, len(value)):
                    raise ValueError(f'{source}: array env variable must occupy the whole entry')
                return replacement
            value = value[:match.start()] + replacement + value[match.end():]
        return value

    project_prefix = '${workspaceFolder}/' + project.directory.relative_to(root).as_posix() if root_view else '${workspaceFolder}'

    def expand(value, default, location):
        if value == '${default}':
            if default is None:
                raise ValueError(f'{source}: no SDK default for {location}')
            return deepcopy(default)
        if isinstance(value, list):
            result = []
            for item in value:
                resolved = expand(item, default, location)
                result.extend(resolved if isinstance(resolved, list) else [resolved])
            if not all(isinstance(item, str) for item in result):
                raise ValueError(f'{source}: {location} must contain strings')
            # Compiler arguments can repeat option names with different values.
            return list(dict.fromkeys(result)) if location.endswith(('includePath', 'browse.path')) else result
        if isinstance(value, dict):
            result = deepcopy(default) if isinstance(default, dict) else {}
            for key, item in value.items():
                result[key] = expand(item, result.get(key), location + '.' + key)
            return result
        if isinstance(value, str):
            value = local_variables(value)
            if isinstance(value, list):
                return [item.replace('${workspaceFolder}', project_prefix).replace('${workspaceFolderBasename}', project.id) for item in value]
            return value.replace('${workspaceFolder}', project_prefix).replace('${workspaceFolderBasename}', project.id)
        return value

    result = expand(selected[0], defaults, 'configuration')

    def resolved_path(value):
        if not isinstance(value, str):
            raise ValueError(f'{source}: expected a path string')
        base = root if root_view else project.directory
        value = value.replace('${workspaceFolder}', base.as_posix()).replace('${userHome}', Path.home().as_posix()).replace(
            '${config:sdk.board}', board).replace('${config:sdk.configuration}', configuration)
        value = value.replace('${pathSeparator}', os.sep)
        value = re.sub(r'\$\{env:([^}]+)\}', lambda m: os.environ.get(m[1], m[0]), value)
        if '${' in value:
            raise ValueError(f'{source}: unresolved path {value!r}')
        path = Path(value.replace('\\', '/'))
        return (base / path).resolve() if not path.is_absolute() else path.resolve()

    if not isinstance(result.get('compileCommands'), list) or len(result['compileCommands']) != 1 or \
            resolved_path(result['compileCommands'][0]) != resolved_path(defaults['compileCommands'][0]):
        raise ValueError(f'{source}: compileCommands must use this project/configuration; prefer ${{default}}')
    browse = result.get('browse')
    if not isinstance(browse, dict) or resolved_path(browse.get('databaseFilename')) != resolved_path(defaults['browse']['databaseFilename']):
        raise ValueError(f'{source}: symbol database must use the SDK-isolated path; prefer ${{default}}')
    for paths in (result.get('includePath', []), result.get('forcedInclude', []), browse.get('path', [])):
        if not isinstance(paths, list):
            raise ValueError(f'{source}: include/browse paths must be arrays')
        for value in paths:
            path = resolved_path(value)
            if any(path.is_relative_to(root / category) for category in ('projects', 'examples')) and not path.is_relative_to(project.directory):
                raise ValueError(f'{source}: index path points at another project: {value}')
    return result
