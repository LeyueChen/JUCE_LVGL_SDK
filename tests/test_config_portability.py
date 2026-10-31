"""Configuration recovery and host relocation without installing tools or opening GUI."""
import json
import ctypes
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import bootstrap
import config_check
import ide
import projects
import toolchain
from ide_config import read_jsonc


class ConfigurationPortability(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/config-portability-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='checkout with spaces ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        shutil.copytree(ROOT / 'templates', self.root / 'templates')
        self.project = projects.scaffold(self.root, 'audio')
        self.output = projects.output_dir(self.root, self.project, 'windows_sim', 'Debug')
        self.compiler = self.root / 'host tools/VC/cl.exe'
        self.ninja = self.root / 'host tools/ninja.exe'
        self.python = self.root / '.venv/Scripts/python.exe'
        for path in (self.compiler, self.ninja, self.python):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'test fixture, never executed')

    def database(self, root=None, compiler=None):
        root = root or self.root
        project = projects.resolve(root, 'audio')
        output = projects.output_dir(root, project, 'windows_sim', 'Debug')
        ide.write_json(output / 'compile_commands.json', [{'directory': str(output), 'file': str(project.user / 'app/app.c'),
                       'arguments': [str(compiler or self.compiler), '/I' + str(project.user), '/c', str(project.user / 'app/app.c')]}])

    def ready_files(self):
        self.database()
        values = {'CMAKE_HOME_DIRECTORY': self.root, 'SDK_PROJECT_DIR': self.project.directory,
                  'CMAKE_CACHEFILE_DIR': self.output, 'CMAKE_C_COMPILER': self.compiler,
                  'CMAKE_CXX_COMPILER': self.compiler, 'CMAKE_MAKE_PROGRAM': self.ninja,
                  'CMAKE_COMMAND': self.ninja,
                  'SDK_LVGL_CONFIG_FILE': self.project.directory / 'config/lv_conf.h',
                  'Python_EXECUTABLE': self.python, 'CMAKE_GENERATOR': 'Ninja', 'CMAKE_BUILD_TYPE': 'Debug'}
        (self.output / 'CMakeCache.txt').write_text(''.join(f'{key}:STRING={value}\n' for key, value in values.items()))
        ide.refresh_projects(self.root)
        ide.generate(self.root, self.project)

    def test_configuration_is_identical_for_different_checkouts_and_vs_paths(self):
        second = self.root / 'other checkout'
        shutil.copytree(ROOT / 'templates', second / 'templates')
        other_project = projects.scaffold(second, 'audio')
        documents = []
        for root, project, compiler in ((self.root, self.project, 'C:/VS Community/VC/cl.exe'),
                                        (second, other_project, 'E:/Build Tools/VC/cl.exe')):
            self.database(root, compiler)
            ide.refresh_projects(root)
            workspace = read_jsonc(ide.generate(root, project))
            properties = read_jsonc(root / '.vscode/c_cpp_properties.json')
            self.assertNotIn('compilerPath', properties['configurations'][0])
            self.assertNotIn('C_Cpp.default.compilerPath', workspace['settings'])
            for document in (workspace, properties):
                serialized = json.dumps(document)
                self.assertNotIn(compiler, serialized)
                self.assertNotIn(str(root), serialized)
                self.assertNotIn(root.as_posix(), serialized)
            self.assertEqual(read_jsonc(project.directory / '.vscode/c_cpp_properties.json')['configurations'][0]['compilerPath'], '${default}')
            documents.append((workspace, properties))
        self.assertEqual(documents[0], documents[1])

    def test_incomplete_generated_json_is_not_ready_and_is_preserved_before_repair(self):
        self.ready_files()
        tools = {'compiler': str(self.compiler), 'ninja': str(self.ninja)}
        self.assertTrue(bootstrap.ide_ready(self.root, self.project, 'windows_sim', 'Debug', tools=tools))
        path = self.root / '.vscode/c_cpp_properties.json'
        damaged = path.read_bytes().rstrip()[:-1]
        path.write_bytes(damaged)
        self.assertFalse(bootstrap.ide_ready(self.root, self.project, 'windows_sim', 'Debug', tools=tools))
        with self.assertRaisesRegex(ValueError, 'line'):
            config_check.check(self.root)
        ide.refresh_projects(self.root, repair=True)
        backups = list((self.root / '.local/config-recovery').glob('*-c_cpp_properties.json'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), damaged)
        self.assertTrue(bootstrap.ide_ready(self.root, self.project, 'windows_sim', 'Debug', tools=tools))

    def test_invalid_project_source_is_never_replaced_by_repair(self):
        self.ready_files()
        path = self.project.directory / '.vscode/c_cpp_properties.json'
        source = path.read_bytes()[:-3]
        path.write_bytes(source)
        generated = self.root / '.vscode/c_cpp_properties.json'
        before = generated.read_bytes()
        with self.assertRaises(ValueError):
            ide.refresh_projects(self.root, repair=True)
        self.assertEqual(path.read_bytes(), source)
        self.assertEqual(generated.read_bytes(), before)

    def test_failed_atomic_replace_preserves_existing_file(self):
        path = self.root / 'atomic.json'
        path.write_bytes(b'{"original":true}')
        with patch.object(ide.os, 'replace', side_effect=PermissionError('file in use')):
            with self.assertRaises(PermissionError):
                ide.write_json(path, {'replacement': True})
        self.assertEqual(path.read_bytes(), b'{"original":true}')
        self.assertFalse(list(self.root.glob('atomic.json.*.tmp')))

    def test_jsonc_settings_are_read_without_rewriting_user_content(self):
        path = self.root / '.vscode/settings.json'
        path.parent.mkdir(parents=True)
        text = '// My local settings\n{"sdk.board":"windows_sim", "sdk.configuration":"Debug", "C_Cpp.intelliSenseCacheSize":0,"editor.tabSize":7,}\n'
        path.write_bytes(text.encode())
        ide.refresh_projects(self.root)
        ide.generate(self.root, self.project)
        self.assertEqual(path.read_bytes(), text.encode())

    def test_duplicate_keys_wrong_types_and_bad_task_references_are_rejected(self):
        path = self.root / 'duplicate.json'
        path.write_text('{"version":4,"version":3}')
        with self.assertRaisesRegex(ValueError, 'duplicate key'):
            read_jsonc(path)
        props = ide.project_properties('audio')
        props['configurations'][0]['compileCommands'] = 1
        with self.assertRaisesRegex(ValueError, 'string array'):
            config_check.validate_document(path.with_name('c_cpp_properties.json'), props)
        with self.assertRaisesRegex(ValueError, 'preLaunchTask'):
            config_check.validate_launch(path, {'configurations': [{'name': 'SDK: audio', 'preLaunchTask': 'missing'}]}, set())
        path.write_text('{/* JSONC only */"value":1,}')
        read_jsonc(path)
        with self.assertRaises(ValueError):
            read_jsonc(path, strict=True)

    def test_cache_with_missing_or_different_compiler_is_rejected_without_changes(self):
        self.ready_files()
        path = self.output / 'CMakeCache.txt'
        original = path.read_bytes()
        with self.assertRaisesRegex(RuntimeError, 'differs from the current host tool'):
            toolchain.validate_cache(self.output, self.root, self.project, compiler=self.ninja)
        self.assertEqual(path.read_bytes(), original)
        path.write_bytes(b'\n'.join(line for line in original.splitlines() if not line.startswith(b'CMAKE_HOME_DIRECTORY:')))
        with self.assertRaisesRegex(RuntimeError, 'another checkout/project'):
            toolchain.validate_cache(self.output, self.root, self.project)
        path.write_bytes(original)
        self.compiler.unlink()  # only the dummy executable created by this fixture
        with self.assertRaisesRegex(RuntimeError, 'unavailable tool'):
            toolchain.validate_cache(self.output, self.root, self.project)
        self.assertEqual(path.read_bytes(), original)

    def test_foreign_compile_database_and_missing_compiler_are_rejected(self):
        self.database(compiler='C:/missing compiler/cl.exe')
        with self.assertRaisesRegex(ValueError, 'compiler is unavailable'):
            config_check.read_database(self.root, self.project, self.output, check_compiler=True)
        path = self.output / 'compile_commands.json'
        entries = read_jsonc(path)
        entries[0]['directory'] = str(self.root / 'old checkout/output')
        ide.write_json(path, entries)
        with self.assertRaisesRegex(ValueError, 'another output/checkout'):
            ide.refresh_projects(self.root)

    def test_all_root_sdk_tasks_use_bootstrap_and_recovery_needs_no_active_config(self):
        tasks = read_jsonc(ROOT / '.vscode/tasks.json')['tasks']
        for task in tasks:
            if task['label'].startswith('SDK:'):
                self.assertTrue(task['command'].endswith('/sdk.cmd'))
                self.assertNotIn('.venv', json.dumps(task))
        for label in ('SDK: setup', 'SDK: check config', 'SDK: refresh projects'):
            task = next(task for task in tasks if task['label'] == label)
            self.assertNotIn('cpptools.activeConfigName', json.dumps(task))

    def test_windows_resource_compiler_and_compiler_file_alias_are_valid(self):
        alias = self.compiler.parent / '..' / self.compiler.parent.name / self.compiler.name
        if os.name == 'nt':
            buffer = ctypes.create_unicode_buffer(32768)
            if ctypes.windll.kernel32.GetShortPathNameW(str(self.compiler), buffer, len(buffer)):
                alias = Path(buffer.value)
        self.assertTrue(toolchain.same_path(alias, self.compiler))
        self.database(compiler=alias)
        path = self.output / 'compile_commands.json'
        entries = read_jsonc(path)
        entries.append({'directory': str(self.output), 'file': str(self.output / 'app_resources.rc'),
                        'arguments': [str(self.ninja), '/r', 'app_resources.rc']})
        ide.write_json(path, entries)
        self.assertEqual(len(config_check.read_database(self.root, self.project, self.output,
                             compiler=str(self.compiler), check_compiler=True)), 2)


if __name__ == '__main__':
    unittest.main()
