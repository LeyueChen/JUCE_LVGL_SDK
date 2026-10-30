from pathlib import Path
import argparse
from config.generate import generate
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--app', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
generate(root, Path(args.app), root / 'boards/windows_sim', Path(args.output))
