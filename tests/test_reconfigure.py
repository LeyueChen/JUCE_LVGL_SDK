"""Reconfiguration preserves compiler spelling and project/configuration ownership."""
from contextlib import ExitStack
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ide
import projects
import sdk
import toolchain


class ReconfigureContracts(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/reconfigure-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        shutil.copytree(ROOT / 'templates', self.root / 'templates')
        shutil.copytree(ROOT / 'boards', self.root / 'boards')
        shutil.copyfile(ROOT / 'Kconfig', self.root / 'Kconfig')
        self.audio = projects.scaffold(self.root, 'audio')
        self.basic = projects.scaffold(self.root, 'basic', self.root / 'examples/basic')
        with (self.audio.user / 'app.conf').open('a') as stream:
            stream.write('\nCONFIG_SDK_SCREEN_WIDTH=480\nCONFIG_SDK_SCREEN_HEIGHT=320\n')
        self.executables = {}
        for name in ('cmake', 'ninja', 'cl'):
            path = self.root / 'host tools' / (name + '.exe')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture executable; never launched')
            self.executables[name] = str(path)
        self.commands = []
        self.tamper_project = False
        self.tamper_python = False
        self.tamper_resolution = False

    def fake_cmake(self, command, **kwargs):
        self.commands.append(command)
        self.assertIn('-S', command, 'A failed configure must never reach build/run')
        output = Path(command[command.index('-B') + 1])
        old = toolchain.cache_values(output) or {}
        values = {str(arg)[2:].split('=', 1)[0]: str(arg).split('=', 1)[1] for arg in command if str(arg).startswith('-D')}
        values['CMAKE_C_COMPILER'] = values.get('CMAKE_C_COMPILER', old.get('CMAKE_C_COMPILER'))
        values['CMAKE_CXX_COMPILER'] = values.get('CMAKE_CXX_COMPILER', old.get('CMAKE_CXX_COMPILER'))
        values.update(CMAKE_HOME_DIRECTORY=str(self.root), CMAKE_CACHEFILE_DIR=str(output),
                      CMAKE_COMMAND=self.executables['cmake'], CMAKE_GENERATOR='Ninja')
        if self.tamper_project:
            values['SDK_PROJECT_DIR'] = str(self.basic.directory)
        if self.tamper_python:
            values.pop('Python_EXECUTABLE', None)
        (output / 'CMakeCache.txt').write_text(''.join(f'{key}:STRING={value}\n' for key, value in values.items()))
        project = projects.load(Path(values['SDK_PROJECT_DIR']))
        generated = output / 'generated'
        if self.tamper_resolution:
            resolved = json.loads((generated / 'config.json').read_text())
            resolved['SDK_SCREEN_WIDTH'] = '800'
            ide.write_json(generated / 'config.json', resolved)
        (generated / 'sdk_project.h').write_text(f'#define SDK_PROJECT_ID "{project.id}"\n')
        ide.write_json(output / 'compile_commands.json', [{'directory': str(output), 'file': str(project.user / 'app/app.c'),
                       'arguments': [values['CMAKE_C_COMPILER'], '/c', str(project.user / 'app/app.c')]}])

    def environment(self, stack):
        stack.enter_context(patch.object(sdk, 'ROOT', self.root))
        stack.enter_context(patch.object(sdk, 'build_env', return_value={}))
        stack.enter_context(patch.object(sdk, 'compiler_arguments', return_value=([], self.executables['cl'])))
        stack.enter_context(patch.object(sdk, 'tool', side_effect=lambda name: self.executables[name]))
        stack.enter_context(patch.object(sdk.dependencies, 'report', return_value=[]))
        stack.enter_context(patch.object(sdk, 'run', side_effect=self.fake_cmake))

    def test_existing_compiler_spelling_is_not_replaced(self):
        with ExitStack() as stack:
            self.environment(stack)
            sdk.main(['configure', '--project', 'audio'])
            self.assertTrue(any(str(arg).startswith('-DCMAKE_C_COMPILER=') for arg in self.commands[-1]))
            output = projects.output_dir(self.root, self.audio, 'windows_sim', 'Debug')
            cache = output / 'CMakeCache.txt'
            original = self.executables['cl']
            alias = str(Path(original).parent / '..' / 'host tools' / 'cl.exe')
            cache.write_text(cache.read_text().replace(original, alias))
            sdk.main(['configure', '--project', 'audio'])
            self.assertFalse(any(str(arg).startswith(('-DCMAKE_C_COMPILER=', '-DCMAKE_CXX_COMPILER=')) for arg in self.commands[-1]))
            self.assertEqual(toolchain.cache_values(output)['CMAKE_C_COMPILER'], alias)

    def test_audio_basic_audio_keeps_separate_caches_and_resolutions(self):
        with ExitStack() as stack:
            self.environment(stack)
            sdk.main(['configure', '--project', 'audio'])
            sdk.main(['configure', '--project', 'basic'])
            basic_output = projects.output_dir(self.root, self.basic, 'windows_sim', 'Debug')
            before = (basic_output / 'CMakeCache.txt').read_bytes()
            sdk.main(['configure', '--project', 'audio'])
            self.assertEqual((basic_output / 'CMakeCache.txt').read_bytes(), before)
            for project, size in ((self.audio, ('480', '320')), (self.basic, ('800', '480'))):
                output = projects.output_dir(self.root, project, 'windows_sim', 'Debug')
                self.assertEqual(Path(toolchain.cache_values(output)['SDK_PROJECT_DIR']), project.directory)
                values = json.loads((output / 'generated/config.json').read_text())
                self.assertEqual((values['SDK_SCREEN_WIDTH'], values['SDK_SCREEN_HEIGHT']), size)

    def test_reconfigure_fallback_stops_before_build(self):
        with ExitStack() as stack:
            self.environment(stack)
            self.tamper_project = True
            with self.assertRaisesRegex(RuntimeError, 'another checkout/project'):
                sdk.main(['build', '--project', 'audio'])
            self.assertEqual(len(self.commands), 1)

    def test_cmake_rejects_basic_in_another_project_output_before_tool_detection(self):
        temporary = tempfile.TemporaryDirectory(prefix='guard-check-', dir=ROOT / 'out')
        self.addCleanup(temporary.cleanup)
        output = Path(temporary.name) / 'windows_sim/Debug'
        result = subprocess.run([toolchain.tool('cmake'), '-S', str(ROOT), '-B', str(output)],
                                capture_output=True, text=True, errors='replace')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('refusing to configure another project here', ' '.join((result.stdout + result.stderr).split()))
        self.assertNotIn('compiler identification', result.stdout)

    def test_reconfigure_losing_python_stops_before_build(self):
        with ExitStack() as stack:
            self.environment(stack)
            self.tamper_python = True
            with self.assertRaisesRegex(RuntimeError, 'Python_EXECUTABLE'):
                sdk.main(['build', '--project', 'audio'])
            self.assertEqual(len(self.commands), 1)

    def test_reconfigure_losing_resolution_stops_before_build(self):
        with ExitStack() as stack:
            self.environment(stack)
            self.tamper_resolution = True
            with self.assertRaisesRegex(RuntimeError, 'changed the selected project configuration'):
                sdk.main(['build', '--project', 'audio'])
            self.assertEqual(len(self.commands), 1)


if __name__ == '__main__':
    unittest.main()
