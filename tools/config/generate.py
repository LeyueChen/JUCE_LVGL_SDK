"""Resolve Kconfig fragments and generate shared SDK configuration."""
import json
import os
from pathlib import Path
import re


def generate(root: Path, app: Path, board: Path, output: Path, interactive=False,
             project_id=None, extra_fragments=()):
    import kconfiglib
    os.environ['srctree'] = str(root)
    config = kconfiglib.Kconfig(str(root / 'Kconfig'), warn_to_stderr=True)
    config.warn_assign_undef = True
    # Later fragments intentionally override board/project defaults.
    config.warn_assign_override = False
    config.warn_assign_redun = False
    fragments = [board / 'defconfig', app / 'app.conf', *extra_fragments]
    local = root / '.local/config' / (project_id or app.name) / (board.name + '.conf')
    if local.exists():
        fragments.append(local)
    requested = {}
    for index, fragment in enumerate(fragments):
        config.load_config(str(fragment), replace=index == 0)
        for line in fragment.read_text(encoding='utf-8').splitlines():
            match = re.fullmatch(r'CONFIG_(\w+)=(.*)', line.strip())
            if match:
                requested[match[1]] = match[2].strip('"')
    for name, expected in requested.items():
        symbol = config.syms.get(name)
        if symbol is None or symbol.str_value != expected:
            actual = symbol.str_value if symbol else '<unknown>'
            raise RuntimeError(f'Unsupported configuration CONFIG_{name}={expected}; resolved {actual}')
    if config.warnings:
        raise RuntimeError('Kconfig warnings must be resolved before building')
    topology = json.loads((board / 'topology.json').read_text(encoding='utf-8'))
    if len(topology['cores']) != int(config.syms['SDK_CORE_SLOTS'].str_value):
        raise RuntimeError('Board topology does not match configured logical core slots')
    if [core['id'] for core in topology['cores'] if core['enabled']] != [0]:
        raise RuntimeError('Only CPU0 is implemented; secondary cores must remain reserved')
    if interactive:
        import menuconfig
        menuconfig.menuconfig(config)
        local.parent.mkdir(parents=True, exist_ok=True)
        config.write_min_config(str(local))
    output.mkdir(parents=True, exist_ok=True)
    config.write_config(str(output / '.config'))
    config.write_autoconf(str(output / 'sdk_config.h'))
    values = {name: symbol.str_value for name, symbol in config.syms.items() if name.startswith('SDK_')}
    (output / 'config.json').write_text(json.dumps(values, indent=2) + '\n', encoding='utf-8')
    (output / 'topology.json').write_text(json.dumps(topology, indent=2) + '\n', encoding='utf-8')
    capabilities = '#pragma once\n#define SDK_CAP_FREERTOS 1\n#define SDK_CAP_AFFINITY 1\n#define SDK_CAP_SMP 0\n#define SDK_CAP_AMP 0\n#define SDK_CAP_SECONDARY_CORES 0\n#define SDK_CAP_IPC 0\n'
    capability_path = output / 'sdk_capabilities.h'
    if not capability_path.exists() or capability_path.read_text(encoding='utf-8') != capabilities:
        capability_path.write_text(capabilities, encoding='utf-8')
    cmake = '\n'.join(f'set(CONFIG_{name} "{value}")' for name, value in sorted(values.items()))
    (output / 'config.cmake').write_text(cmake + '\n', encoding='utf-8')
