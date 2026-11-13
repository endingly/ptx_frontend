#!/usr/bin/env python3
"""Run audit fixtures with bounded probe execution and retain exact output."""
import argparse
import pathlib
import subprocess


def main():
    """Write one combined transcript, retaining nonzero probe return codes."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', default='/tmp/ptx93-audit-build/ptx93_probe')
    parser.add_argument('--output', required=True)
    parser.add_argument('fixtures', nargs='+')
    args = parser.parse_args()
    with pathlib.Path(args.output).open('w') as output:
        for fixture in args.fixtures:
            output.write(f'=== {fixture} ===\n')
            output.write(pathlib.Path(fixture).read_text())
            output.write('\n--- probe ---\n')
            try:
                result = subprocess.run([args.probe, fixture], capture_output=True, text=True, timeout=30)
                output.write(result.stdout)
                output.write(result.stderr)
                output.write(f'exit={result.returncode}\n\n')
            except subprocess.TimeoutExpired:
                output.write('timeout=30s\n\n')


if __name__ == '__main__':
    main()
