"""Validate the selected dependencies for direct CMake configurations."""
import argparse
import json
from pathlib import Path
import dependencies
from ide import write_json


def required(values):
    names = {'juce', 'freertos', 'lvgl'}
    names.update(name.lower() for name in ('CJSON', 'LITTLEFS', 'FLASHDB', 'NANOPB', 'MULTIBUTTON', 'EASYLOGGER', 'RTNEURAL')
                 if values.get('SDK_' + name) == 'y')
    names.update(name for name in ('esp_dsp', 'esp_nn') if values.get('SDK_' + name.upper() + '_REF') == 'y')
    return names


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    write_json(args.output, dependencies.report(root, required(json.loads(args.config.read_text()))))
