"""Project-native LVGL configuration, CMake change tracking and index isolation."""
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ide
import projects
import toolchain
import config_check


class ProjectLvglConfiguration(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/project-lvgl-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='config with spaces ', dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        shutil.copytree(ROOT / 'templates', self.root / 'templates')
        self.audio = projects.scaffold(self.root, 'audio')
        self.basic = projects.scaffold(self.root, 'basic', self.root / 'examples/basic')
        self.fixture = self.root / 'fixture'
        self.fixture.mkdir()
        (self.fixture / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(ProjectLvglContracts LANGUAGES NONE)
include("{ROOT.as_posix()}/cmake/LvglConfig.cmake")
sdk_configure_lvgl("${{PROJECT_DIRECTORY}}" "${{CMAKE_BINARY_DIR}}/generated")
add_custom_target(config_ready ALL
    COMMAND "${{CMAKE_COMMAND}}" -E compare_files
        "${{SDK_LVGL_CONFIG_FILE}}" "${{CMAKE_BINARY_DIR}}/generated/lv_conf.h")
''', encoding='utf-8')

    def configure(self, project, *, success=True):
        output = self.root / 'build' / project.id
        result = subprocess.run([toolchain.tool('cmake'), '-S', str(self.fixture), '-B', str(output), '-G', 'Ninja',
                                 '-DCMAKE_MAKE_PROGRAM=' + toolchain.tool('ninja'),
                                 '-DPROJECT_DIRECTORY=' + str(project.directory)], capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return output, result

    def test_project_configs_contain_the_complete_upstream_template_options(self):
        upstream = (ROOT / 'third_party/src/lvgl/lv_conf_template.h').read_text(encoding='utf-8')
        definitions = set(re.findall(r'^\s*#define\s+(\w+)', upstream, re.MULTILINE))
        sections = re.findall(r'/\*={10,}\n \* ([^\n]+)\n', upstream)
        self.assertGreater(len(definitions), 300)
        self.assertTrue(sections)
        for path in (ROOT / 'projects/audio/config/lv_conf.h', ROOT / 'examples/basic/config/lv_conf.h',
                     ROOT / 'templates/project/config/lv_conf.h'):
            with self.subTest(path=path):
                text = path.read_text(encoding='utf-8')
                self.assertIn('Configuration file for v9.6.0', text)
                self.assertIn('#if 1 /* Set this to "1" to enable content */', text)
                self.assertTrue(definitions <= set(re.findall(r'^\s*#define\s+(\w+)', text, re.MULTILINE)))
                for section in sections:
                    self.assertIn(' * ' + section + '\n', text)

    def test_scaffold_copies_independent_configs_and_preserves_existing_project(self):
        source = self.root / 'templates/project/config/lv_conf.h'
        audio = projects.lvgl_config(self.audio)
        basic = projects.lvgl_config(self.basic)
        baseline = source.read_bytes()
        self.assertEqual(audio.read_bytes(), baseline)
        self.assertEqual(basic.read_bytes(), baseline)
        audio.write_bytes(baseline.replace(b'LV_DEF_REFR_PERIOD 16', b'LV_DEF_REFR_PERIOD 25'))
        projects.scaffold(self.root, 'third_project')
        self.assertEqual(basic.read_bytes(), baseline)
        self.assertEqual(source.read_bytes(), baseline)
        self.assertIn(b'LV_DEF_REFR_PERIOD 25', audio.read_bytes())
        self.assertFalse(audio.samefile(basic))

    def test_missing_config_is_an_error_without_sdk_or_other_project_fallback(self):
        projects.lvgl_config(self.audio).unlink()  # own temporary fixture only
        with self.assertRaisesRegex(RuntimeError, 'provide its own LVGL configuration'):
            projects.lvgl_config(self.audio)
        _, result = self.configure(self.audio, success=False)
        self.assertIn('Project must provide its own LVGL configuration', ' '.join(result.stderr.split()))
        self.assertTrue(projects.lvgl_config(self.basic).exists())

    def test_cmake_copies_native_macros_and_updates_only_selected_project(self):
        audio = projects.lvgl_config(self.audio)
        basic = projects.lvgl_config(self.basic)
        audio.write_bytes(audio.read_bytes().replace(b'LV_DEF_REFR_PERIOD 16', b'LV_DEF_REFR_PERIOD 25'))
        audio_output, _ = self.configure(self.audio)
        basic_output, _ = self.configure(self.basic)
        generated = audio_output / 'generated/lv_conf.h'
        self.assertEqual(generated.read_bytes(), audio.read_bytes())
        basic_generated = basic_output / 'generated/lv_conf.h'
        before = (basic_generated.read_bytes(), basic_generated.stat().st_mtime_ns)
        audio.write_bytes(audio.read_bytes().replace(b'LV_DEF_REFR_PERIOD 25', b'LV_DEF_REFR_PERIOD 30'))
        result = subprocess.run([toolchain.tool('cmake'), '--build', str(audio_output)], capture_output=True, text=True, errors='replace')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(generated.read_bytes(), audio.read_bytes())
        self.assertEqual((basic_generated.read_bytes(), basic_generated.stat().st_mtime_ns), before)
        self.assertEqual(Path(toolchain.cache_values(audio_output)['SDK_LVGL_CONFIG_FILE']), audio)
        self.assertEqual(Path(toolchain.cache_values(basic_output)['SDK_LVGL_CONFIG_FILE']), basic)

    def test_config_index_paths_are_project_specific(self):
        ide.refresh_projects(self.root)
        profiles = json.loads((self.root / '.vscode/c_cpp_properties.json').read_text())['configurations']
        for project in (self.audio, self.basic):
            profile = next(item for item in profiles if item['name'] == project.id)
            expected = '${workspaceFolder}/' + project.directory.relative_to(self.root).as_posix() + '/config'
            self.assertIn(expected, profile['includePath'])
            self.assertIn(expected, profile['browse']['path'])
            other = 'basic' if project.id == 'audio' else 'audio'
            self.assertNotIn('/' + other + '/config', json.dumps(profile))
            workspace = json.loads(ide.generate(self.root, project).read_text())
            self.assertIn('${workspaceFolder}/../../' + project.directory.relative_to(self.root).as_posix() + '/config',
                          workspace['settings']['C_Cpp.default.includePath'])

    def test_other_projects_missing_config_does_not_block_selected_project(self):
        projects.lvgl_config(self.basic).unlink()  # own temporary fixture only
        config_check.check(self.root, generated=False)
        self.configure(self.audio)
        with self.assertRaisesRegex(RuntimeError, 'basic.*config'):
            config_check.check(self.root)


if __name__ == '__main__':
    unittest.main()
