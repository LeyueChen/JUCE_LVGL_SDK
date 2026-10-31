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
    installer = Path(os.environ.get('ProgramFiles(x86)', os.environ.get('ProgramFiles', ''))) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    finder = os.environ.get('SDK_VSWHERE') or shutil.which('vswhere') or str(installer)
    if not Path(finder).is_file():
        raise RuntimeError('Visual Studio Installer/vswhere is missing; install Desktop development with C++ or set SDK_VSWHERE')
    result = subprocess.check_output([str(finder), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not result:
        raise RuntimeError('Visual Studio Desktop development with C++ is required')
    return Path(result)


def tool(name):
    explicit = os.environ.get('SDK_' + name.upper().replace('-', '_'))
    found = explicit or shutil.which(name)
    if found:
        executable = shutil.which(str(found))
        if not executable:
            raise RuntimeError(f'{name} executable is missing: {found}')
        return executable
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
    if os.name == 'nt' and (not env.get('VSCMD_VER') or env.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64'
                            or not shutil.which('cl', path=env.get('PATH', ''))):
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


def cache_values(output):
    path = output / 'CMakeCache.txt'
    if not path.exists():
        return None
    return {line.split('=', 1)[0].split(':', 1)[0]: line.split('=', 1)[1]
            for line in path.read_text(encoding='utf-8-sig').splitlines()
            if '=' in line and not line.startswith(('#', '//'))}


def same_path(left, right):
    try:
        return os.path.samefile(left, right)
    except OSError:
        return Path(left).resolve() == Path(right).resolve()


def validate_cache(output, root, project, *, compiler=None, ninja=None, python=None, cmake=None, configuration=None):
    values = cache_values(output)
    if values is None:
        return False

    def fail(reason):
        raise RuntimeError(f'{output / "CMakeCache.txt"}: {reason}. Preserve/move that build directory aside and rerun setup; nothing is deleted automatically.')

    def same(left, right):
        return same_path(left, right)

    if not values.get('CMAKE_HOME_DIRECTORY') or not values.get('SDK_PROJECT_DIR') or \
            not same(values['CMAKE_HOME_DIRECTORY'], root) or not same(values['SDK_PROJECT_DIR'], project.directory):
        fail('Build cache belongs to another checkout/project')
    if not values.get('CMAKE_CACHEFILE_DIR') or not same(values['CMAKE_CACHEFILE_DIR'], output):
        fail('Build cache belongs to another output directory')
    if values.get('CMAKE_GENERATOR') != 'Ninja':
        fail('Expected the Ninja generator')
    if configuration and values.get('CMAKE_BUILD_TYPE') != configuration:
        fail('Build configuration differs')
    for key, expected in (('CMAKE_C_COMPILER', compiler), ('CMAKE_CXX_COMPILER', compiler),
                          ('CMAKE_MAKE_PROGRAM', ninja), ('Python_EXECUTABLE', python), ('CMAKE_COMMAND', cmake)):
        cached = values.get(key, '')
        if not cached or not Path(cached).is_file():
            fail(f'{key} is missing or points to an unavailable tool: {cached!r}')
        if expected and not same(cached, expected):
            fail(f'{key} differs from the current host tool: {cached!r}')
    for key in ('CMAKE_RC_COMPILER', 'CMAKE_MT'):
        cached = values.get(key, '')
        if cached and Path(cached).is_absolute() and not Path(cached).is_file():
            fail(f'{key} points to an unavailable Windows SDK tool: {cached!r}')
    return True
