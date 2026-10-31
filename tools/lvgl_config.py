"""Evaluate LVGL build switches with the selected project's C preprocessor."""
import argparse
from pathlib import Path
import re
import subprocess


def cmake_path(path):
    return path.as_posix().replace('"', '\\"').replace(';', '\\;').replace('$', '\\$')


def generate(compiler, compiler_id, lvgl, config, includes, output, compiler_args=(), defines=()):
    internal = lvgl / 'include/lvgl/config/lv_conf_internal.h'
    # Discover only boolean switches consumed by upstream CMake. Other LV_USE_*
    # options can be header-name strings (e.g. LV_USE_UEFI_INCLUDE), not booleans.
    # Values/branches are evaluated by the build compiler, not parsed from text.
    build_scripts = sorted((lvgl / 'env_support/cmake').rglob('*.cmake'))
    candidates = set().union(*(set(re.findall(r'\bCONFIG_(LV_[A-Z0-9_]+)\b',
                                             path.read_text(encoding='utf-8')))
                               for path in build_scripts))
    symbols = sorted(symbol for symbol in candidates if not symbol.endswith('_')
                     and (symbol.startswith(('LV_USE_', 'LV_BUILD_')) or '_USE_' in symbol))
    output.parent.mkdir(parents=True, exist_ok=True)
    probe = output.with_suffix('.c')
    source = ['#include <lvgl/config/lv_conf_internal.h>']
    for symbol in symbols:
        source += [f'#if defined({symbol}) && ({symbol})', f'SDK_OPTION_{symbol} 1',
                   '#else', f'SDK_OPTION_{symbol} 0', '#endif']
    probe.write_text('\n'.join(source) + '\n', encoding='utf-8')
    definitions = [*defines, 'LV_KCONFIG_IGNORE', f'LV_CONF_PATH="{config.as_posix()}"']
    if compiler_id == 'MSVC':
        command = [compiler, '/nologo', '/TC', '/E', '/utf-8', '/Zc:preprocessor',
                   *compiler_args, *('/D' + value for value in definitions),
                   *('/I' + str(path) for path in includes), str(probe)]
    else:
        command = [compiler, '-E', '-x', 'c', *compiler_args,
                   *('-D' + value for value in definitions),
                   *('-I' + str(path) for path in includes), str(probe)]
    result = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace')
    if result.returncode:
        diagnostics = '\n'.join(line for line in result.stdout.splitlines()
                                if re.search(r'\b(?:error|warning)\s+[A-Z]?\d+', line, re.IGNORECASE))
        raise RuntimeError(f'Cannot evaluate LVGL configuration {config}:\n{diagnostics}\n{result.stderr}')
    values = dict(re.findall(r'^SDK_OPTION_(\w+)\s+([01])\s*$', result.stdout, re.MULTILINE))
    if set(values) != set(symbols):
        raise RuntimeError('LVGL preprocessor did not emit all build switches')
    # /E and -E retain #line markers for every included header, including
    # project helpers and sdk_config.h. CMake must reconfigure on their edits.
    dependencies = {internal.resolve(), config.resolve(), Path(__file__).resolve(),
                    *(path.resolve() for path in build_scripts)}
    for name in re.findall(r'^\s*#\s*(?:line\s+)?\d+\s+"([^"]+)"', result.stdout, re.MULTILINE):
        path = Path(name.replace('\\\\', '\\'))
        if path.is_file() and path.resolve() != probe.resolve():
            dependencies.add(path.resolve())
    lines = [f'set(CONFIG_{symbol} {values[symbol]})' for symbol in symbols]
    lines += ['set(SDK_LVGL_BUILD_OPTIONS ' + ' '.join('CONFIG_' + symbol for symbol in symbols) + ')']
    lines += ['set(SDK_LVGL_CONFIG_DEPENDENCIES',
              *(f'    "{cmake_path(path)}"' for path in sorted(dependencies)), ')']
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--compiler-id', required=True)
    parser.add_argument('--lvgl', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--include', type=Path, action='append', default=[])
    parser.add_argument('--compiler-arg', action='append', default=[])
    parser.add_argument('--define', action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    generate(args.compiler, args.compiler_id, args.lvgl, args.config, args.include,
             args.output, args.compiler_arg, args.define)


if __name__ == '__main__':
    main()
