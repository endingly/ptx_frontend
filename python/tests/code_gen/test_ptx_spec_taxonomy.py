from pathlib import Path
import unittest

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
SPEC_DIR = REPO_ROOT / "instructions/ptx_spec"


EXPECTED_FILES = {
    "arithmetic.yaml": ("arithmetic", "arithmetic", None),
    "comparison_and_selection.yaml": ("comparison_and_selection", "arithmetic", "9.7.6"),
    "logic_and_shift.yaml": ("logic_and_shift", "arithmetic", "9.7.8"),
    "data_movement_and_conversion.yaml": ("data_movement_and_conversion", "data_movement", "9.7.9"),
    "control_flow.yaml": ("control_flow", "control_flow", "9.7.13"),
    "parallel_synchronization_and_communication.yaml": (
        "parallel_synchronization_and_communication",
        "parallel_synchronization_and_communication",
        "9.7.14",
    ),
    "warp_level_matrix_multiply_accumulate.yaml": (
        "warp_level_matrix_multiply_accumulate", "matrix", "9.7.15"
    ),
    "miscellaneous.yaml": ("miscellaneous", "control_flow", "9.7.20"),
}

EXPECTED_SECTIONS = {
    "arithmetic.yaml": {
        "add": {"9.7.1.1", "9.7.3.3", "9.7.4.1", "9.7.5.1"},
        "sub": {"9.7.1.2", "9.7.3.4", "9.7.4.2", "9.7.5.2"},
        "mul": {"9.7.1.3", "9.7.3.5"}, "mad": {"9.7.1.4", "9.7.3.7"},
        "div": {"9.7.1.9", "9.7.3.8"}, "rem": {"9.7.1.10"},
        "min": {"9.7.1.13", "9.7.3.11"},
        "max": {"9.7.1.14", "9.7.3.12"},
        "abs": {"9.7.1.11", "9.7.3.9"},
        "fma": {"9.7.3.6", "9.7.4.4"},
    },
    "comparison_and_selection.yaml": {
        "set": {"9.7.6.1"},
        "setp": {"9.7.6.2"},
        "selp": {"9.7.6.3"},
        "slct": {"9.7.6.4"},
    },
    "logic_and_shift.yaml": {
        "and": {"9.7.8.1"}, "or": {"9.7.8.2"}, "xor": {"9.7.8.3"},
        "not": {"9.7.8.4"}, "shl": {"9.7.8.8"}, "shr": {"9.7.8.9"},
    },
    "data_movement_and_conversion.yaml": {
        "mov": {"9.7.9"}, "shfl": {"9.7.9.6"}, "ld": {"9.7.9.8"},
        "ldu": {"9.7.9.10"}, "st": {"9.7.9.11"}, "prefetch": {"9.7.9.16"},
        "cvta": {"9.7.9.21"}, "cvt": {"9.7.9.22"},
        "cp": {"9.7.9.26.3.1", "9.7.9.26.3.2", "9.7.9.26.3.3"},
    },
    "control_flow.yaml": {
        "bra": {"9.7.13.3"}, "brx": {"9.7.13.4"}, "call": {"9.7.13.5"},
        "ret": {"9.7.13.6"}, "exit": {"9.7.13.7"},
    },
    "parallel_synchronization_and_communication.yaml": {
        "bar": {"9.7.14.1"}, "membar": {"9.7.14.4"}, "fence": {"9.7.14.4"},
        "atom": {"9.7.14.5"}, "red": {"9.7.14.6"}, "vote": {"9.7.14.10"},
        "activemask": {"9.7.14.12"},
    },
    "warp_level_matrix_multiply_accumulate.yaml": {
        "mma": {"9.7.15.5.14"}, "ldmatrix": {"9.7.15.5.15"},
    },
    "miscellaneous.yaml": {"trap": {"9.7.20.4"}},
}


class PtxSpecTaxonomyTests(unittest.TestCase):
    def test_ptx_93_taxonomy_files_and_sections(self) -> None:
        paths = {path.name for path in SPEC_DIR.glob("*.yaml")}
        self.assertEqual(paths, set(EXPECTED_FILES))

        for name, (category, codegen_category, section) in EXPECTED_FILES.items():
            spec = load_yaml(SPEC_DIR / name)
            self.assertEqual(spec["ptx_isa"], "9.3")
            self.assertEqual(spec["category"], category)
            self.assertEqual(spec["codegen_category"], codegen_category)
            self.assertEqual(spec.get("section"), section)

            actual: dict[str, set[str]] = {}
            for instruction in spec["instructions"]:
                actual.setdefault(instruction["opcode"], set()).add(instruction["section"])
                self.assertTrue(instruction["section"].startswith(section or "9.7."))
            self.assertEqual(actual, EXPECTED_SECTIONS[name])


if __name__ == "__main__":
    unittest.main()
