"""Locate host tools without changing machine-wide environment variables."""
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    print('+', subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def vs_root():
    finder = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    result = subprocess.check_output([str(finder), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not result:
        raise RuntimeError('Visual Studio Desktop development with C++ is required')
    return Path(result)


def tool(name):
    explicit = os.environ.get('SDK_' + name.upper().replace('-', '_'))
    found = explicit or shutil.which(name)
    if found:
        return str(found)
    if name == 'clang-format':
        candidate = vs_root() / 'VC/Tools/Llvm/x64/bin/clang-format.exe'
    else:
        base = vs_root() / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
        candidate = base / ('CMake/bin/cmake.exe' if name == 'cmake' else 'Ninja/ninja.exe')
    if not candidate.exists():
        raise RuntimeError(f'{name} missing; install the host tool or set SDK_{name.upper().replace("-", "_")}')
    return str(candidate)


def build_env():
    env = {key.upper(): value for key, value in os.environ.items()} if os.name == 'nt' else os.environ.copy()
    if os.name == 'nt' and not env.get('VSCMD_VER'):
        script = vs_root() / 'VC/Auxiliary/Build/vcvars64.bat'
        output = subprocess.check_output(f'cmd.exe /d /s /c ""{script}" >nul && set"', text=True, errors='replace')
        for line in output.splitlines():
            if '=' in line and not line.startswith('='):
                key, value = line.split('=', 1)
                env[key.upper()] = value
    if os.name == 'nt':
        env['_CL_'] = (env.get('_CL_', '') + ' /Z7').strip()
        env['VSLANG'] = '1033'
    scratch = ROOT / '.cache/tmp'
    scratch.mkdir(parents=True, exist_ok=True)
    env['TMP'] = env['TEMP'] = str(scratch)
    return env


def compiler_arguments(env):
    if os.name != 'nt':
        return [], None
    probe = ROOT / '.cache/toolchain/include-probe.c'
    probe.parent.mkdir(parents=True, exist_ok=True)
    probe.write_text('#include <stddef.h>\n', encoding='utf-8')
    compiler = shutil.which('cl', path=env.get('PATH', ''))
    if not compiler:
        raise RuntimeError('MSVC environment did not provide cl.exe')
    result = subprocess.check_output([compiler, '/nologo', '/showIncludes', '/EP', '/utf-8', str(probe)], env=env, stderr=subprocess.STDOUT)
    for line in result.splitlines():
        match = re.match(rb'^(.*?)(?=[A-Za-z]:[\\/])', line)
        if match and match[1].strip():
            try:
                prefix = match[1].decode('utf-8')
            except UnicodeDecodeError:
                prefix = match[1].decode('mbcs')
            return ['-DSDK_MSVC_INCLUDE_PREFIX=' + prefix.rstrip()], compiler
    return [], compiler
