from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from config.generate import generate


class ConfigurationContracts(unittest.TestCase):
    def resolve(self, fragment):
        parent = ROOT / 'out/config-contracts'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as temporary:
            app = Path(temporary) / 'fixture'
            app.mkdir()
            (app / 'app.conf').write_text(fragment, encoding='utf-8')
            output = Path(temporary) / 'generated'
            generate(ROOT, app, ROOT / 'boards/windows_sim', output)
            return (output / 'sdk_config.h').read_text(encoding='utf-8')

    def test_product_can_disable_a_component(self):
        header = self.resolve('CONFIG_SDK_CJSON=n\n')
        self.assertNotIn('#define CONFIG_SDK_CJSON 1', header)

    def test_reserved_core_cannot_silently_be_enabled(self):
        with self.assertRaisesRegex(RuntimeError, 'SDK_CPU1_ENABLED'):
            self.resolve('CONFIG_SDK_CPU1_ENABLED=y\n')

    def test_unknown_option_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'SDK_UNKNOWN'):
            self.resolve('CONFIG_SDK_UNKNOWN=y\n')

    def test_user_sources_do_not_import_platform_headers(self):
        sources = [path for category in ('examples', 'projects', 'templates') for user in (ROOT / category).glob('*/user') for path in user.rglob('*') if 'tests' not in path.relative_to(user).parts]
        self.assertTrue(sources)
        for path in sources:
            if path.suffix in ('.c', '.h', '.cpp'):
                text = path.read_text(encoding='utf-8').lower()
                for forbidden in ('windows.h', 'sdk/host_bridge.h', 'juce_gui_basics'):
                    self.assertNotIn(forbidden, text, str(path))


if __name__ == '__main__':
    unittest.main()
