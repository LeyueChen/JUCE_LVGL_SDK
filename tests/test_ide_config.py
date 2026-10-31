"""Project-owned JSONC is the source of both root and dedicated IDE views."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ide
import projects
from ide_config import read_jsonc


class ProjectIndexSource(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'out/ide-config-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        shutil.copytree(ROOT / 'templates', self.root / 'templates')
        self.audio = projects.scaffold(self.root, 'audio')
        self.gui = projects.scaffold(self.root, 'rakure_gui')
        self.source = self.audio.directory / '.vscode/c_cpp_properties.json'

    def profile(self, name='audio'):
        document = json.loads((self.root / '.vscode/c_cpp_properties.json').read_text())
        return next(item for item in document['configurations'] if item['name'] == name)

    def test_project_jsonc_defaults_env_and_path_rebasing_preserve_source(self):
        source = '''{
// URLs and punctuation inside strings must stay intact.
"version": 4,
"env": {"LOCAL": "${workspaceFolder}/user", "MORE": ["${LOCAL}/hints", "${LOCAL}/include"],},
"configurations": [{
 "name": "audio", "cStandard": "c17",
 "includePath": ["${default}", "${MORE}",],
 "defines": ["${default}", "AUDIO_IDE_ONLY",],
 "compilerArgs": ["/FI", "one.h", "/FI", "two.h", "/DURL=https://example.invalid/a,}"],
 "customConfigurationVariables": {"SOURCE": "audio-project"},
 "browse": {"path": ["${default}", "${LOCAL}/hints"],},
}],}
'''
        self.source.write_text(source, encoding='utf-8')
        gui_source = self.gui.directory / '.vscode/c_cpp_properties.json'
        gui = read_jsonc(gui_source)
        gui['env'] = {'LOCAL': '${workspaceFolder}/user/gui-only'}
        gui['configurations'][0]['includePath'].append('${LOCAL}')
        ide.write_json(gui_source, gui)
        before = self.source.read_bytes()
        ide.refresh_projects(self.root)
        audio = self.profile()
        self.assertEqual(audio['cStandard'], 'c17')
        self.assertIn('${workspaceFolder}/projects/audio/user/hints', audio['includePath'])
        self.assertIn('${workspaceFolder}/projects/audio/user/include', audio['includePath'])
        self.assertIn('AUDIO_IDE_ONLY', audio['defines'])
        self.assertEqual(audio['compilerArgs'], ['/FI', 'one.h', '/FI', 'two.h', '/DURL=https://example.invalid/a,}'])
        self.assertNotIn('gui-only', json.dumps(audio))
        self.assertNotIn('AUDIO_IDE_ONLY', json.dumps(self.profile('rakure_gui')))
        self.assertIn('${workspaceFolder}/projects/rakure_gui/user/gui-only', self.profile('rakure_gui')['includePath'])
        dedicated = json.loads(ide.generate(self.root, self.audio).read_text())
        self.assertNotIn('AUDIO_IDE_ONLY', dedicated['settings'].get('C_Cpp.default.defines', []))
        self.assertEqual(self.source.read_bytes(), before)
        # Editing the project source updates the generated entry on refresh.
        self.source.write_text(source.replace('c17', 'c11'), encoding='utf-8')
        ide.refresh_projects(self.root)
        self.assertEqual(self.profile()['cStandard'], 'c11')

    def test_wrong_name_cycle_and_cross_project_paths_fail_without_replacing_root(self):
        ide.refresh_projects(self.root)
        root_path = self.root / '.vscode/c_cpp_properties.json'
        original = root_path.read_bytes()
        pristine = read_jsonc(self.source)
        invalid = []
        wrong_name = json.loads(json.dumps(pristine))
        wrong_name['configurations'][0]['name'] = 'not-audio'
        invalid.append(wrong_name)
        cycle = json.loads(json.dumps(pristine))
        cycle['env'] = {'A': '${B}', 'B': '${A}'}
        cycle['configurations'][0]['includePath'].append('${A}')
        invalid.append(cycle)
        other_source = json.loads(json.dumps(pristine))
        other_source['configurations'][0]['includePath'].append('${workspaceFolder}/../../projects/rakure_gui/user')
        invalid.append(other_source)
        other_database = json.loads(json.dumps(pristine))
        other_database['configurations'][0]['compileCommands'] = ['${workspaceFolder}/../../out/rakure_gui/windows_sim/Debug/compile_commands.json']
        invalid.append(other_database)
        shared_symbols = json.loads(json.dumps(pristine))
        shared_symbols['configurations'][0]['browse']['databaseFilename'] = '${workspaceFolder}/shared.db'
        invalid.append(shared_symbols)
        for document in invalid:
            with self.subTest(document=document):
                ide.write_json(self.source, document)
                with self.assertRaises(ValueError):
                    ide.refresh_projects(self.root)
                self.assertEqual(root_path.read_bytes(), original)

    def test_missing_database_bootstraps_but_never_uses_another_projects_database(self):
        gui_output = projects.output_dir(self.root, self.gui, 'windows_sim', 'Debug')
        ide.write_json(gui_output / 'compile_commands.json', [{'file': str(self.gui.user / 'app/app.c'),
                       'directory': str(gui_output), 'arguments': ['gui-compiler.exe', '/I' + str(self.gui.user)]}])
        ide.refresh_projects(self.root)
        audio = self.profile()
        self.assertNotIn('rakure_gui', json.dumps(audio))
        self.assertNotIn('compilerPath', audio)
        self.assertNotIn('compilerPath', self.profile('rakure_gui'))
        before = (self.root / '.vscode/c_cpp_properties.json').read_bytes()
        ide.refresh_projects(self.root)
        self.assertEqual((self.root / '.vscode/c_cpp_properties.json').read_bytes(), before)

    def test_template_initializes_the_project_name(self):
        document = read_jsonc(self.gui.directory / '.vscode/c_cpp_properties.json')
        self.assertEqual(document['configurations'][0]['name'], 'rakure_gui')
        self.assertNotIn('@PROJECT_ID@', json.dumps(document))

    def test_root_project_entry_cannot_shadow_project_source(self):
        document = read_jsonc(self.source)
        document['configurations'][0]['compilerPath'] = 'project-source-cl.exe'
        ide.write_json(self.source, document)
        local_only = {'name': 'local-toolchain', 'compilerPath': 'local-cl.exe'}
        ide.write_json(self.root / '.vscode/c_cpp_properties.json', {'version': 4, 'configurations': [
                       {'name': 'audio', 'compilerPath': 'stale-root-cl.exe'}, local_only]})
        ide.refresh_projects(self.root)
        self.assertEqual(self.profile()['compilerPath'], 'project-source-cl.exe')
        self.assertEqual(self.profile('local-toolchain'), local_only)

    def test_refresh_preserves_equivalent_editor_jsonc_formatting(self):
        ide.refresh_projects(self.root)
        path = self.root / '.vscode/c_cpp_properties.json'
        document = read_jsonc(path)
        self.assertTrue(all('configurationProvider' not in item for item in document['configurations']))
        path.write_text('// Editor formatting\n' + json.dumps(document, indent=4) + '\n', encoding='utf-8')
        before = path.read_bytes()
        with patch.object(ide, 'write_json', wraps=ide.write_json) as write:
            ide.refresh_projects(self.root)
            self.assertFalse(any(call.args[0] == path for call in write.call_args_list))
        self.assertEqual(path.read_bytes(), before)

    def test_jsonc_invalid_comment_and_unknown_variable_report_source(self):
        self.source.write_text('{ /* unterminated', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'unterminated JSONC comment'):
            read_jsonc(self.source)
        document = ide.project_properties('audio')
        document['configurations'][0]['includePath'].append('${UNDEFINED_PROJECT_HINT}')
        ide.write_json(self.source, document)
        with self.assertRaisesRegex(ValueError, 'unresolved variable'):
            ide.refresh_projects(self.root)


if __name__ == '__main__':
    unittest.main()
