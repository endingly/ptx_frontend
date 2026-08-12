"""_summary_
This file is used to verify the correctness of YAML syntax description files or backend description files, typically within a CI pipeline.
Raises:
    SystemExit: _description_
"""

import sys
from pathlib import Path
from argparse import ArgumentParser
import yaml
from jsonschema import Draft202012Validator


def add_argparser() -> ArgumentParser:
    parser = ArgumentParser(description="Validate a YAML file against a JSON schema.")
    parser.add_argument("schema", type=str, help="Path to the yaml schema file.")
    parser.add_argument("yaml", type=str, help="Path to the YAML file to validate.")
    return parser


if __name__ == "__main__":
    parser = add_argparser()
    args = parser.parse_args()

    schema_path = Path(args.schema)
    yaml_path = Path(args.yaml)

    schema = yaml.safe_load(schema_path.read_text())
    data = yaml.safe_load(yaml_path.read_text())

    validator = Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(data), key=lambda e: list(e.path))

    if errors:
        for e in errors:
            path = ".".join(str(x) for x in e.path) or "<root>"
            print(f"{yaml_path}:{path}: {e.message}")
        raise SystemExit(1)

    print(f"{yaml_path}: OK")
