from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "instructions/ld_st_extension_gaps.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ld-st-extension-gaps-v1.schema.yaml"


REQUIRED_GAPS = {
    "ld-l1-evict-priority-m10-i02": ("ld", "cache_eviction", "M10-I02"),
    "st-l1-evict-priority-m10-i02": ("st", "cache_eviction", "M10-I02"),
    "ld-l2-cache-hint-policy-m10-i02": ("ld", "cache_policy", "M10-I02"),
    "st-l2-cache-hint-policy-m10-i02": ("st", "cache_policy", "M10-I02"),
    "ld-l1-evict-priority-deferred": ("ld", "cache_eviction", "deferred"),
    "st-l1-evict-priority-deferred": ("st", "cache_eviction", "deferred"),
    "ld-l2-evict-priority-deferred": ("ld", "cache_eviction", "deferred"),
    "st-l2-evict-priority-deferred": ("st", "cache_eviction", "deferred"),
    "ld-l2-prefetch-size": ("ld", "prefetch", "deferred"),
    "ld-unified-address": ("ld", "address_qualifier", "deferred"),
    "ld-shared-subspace": ("ld", "state_space_subqualifier", "deferred"),
    "st-shared-subspace": ("st", "state_space_subqualifier", "deferred"),
    "ld-scalar-b128": ("ld", "scalar_type", "deferred"),
    "st-scalar-b128": ("st", "scalar_type", "deferred"),
    "ld-global-nc": ("ld", "opcode_form", "deferred"),
    "st-async": ("st", "opcode_form", "deferred"),
}


class LdStExtensionGapsManifestTests(unittest.TestCase):
    def test_schema_and_required_gap_inventory(self) -> None:
        manifest = load_yaml(MANIFEST)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        gaps = manifest["gaps"]
        ids = [gap["id"] for gap in gaps]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({gap["opcode"] for gap in gaps}, {"ld", "st"})

        inventory = {
            gap["id"]: (gap["opcode"], gap["kind"], gap["disposition"])
            for gap in gaps
        }
        self.assertEqual(inventory, REQUIRED_GAPS)

        m10_i02_ids = {
            gap["id"] for gap in gaps if gap["id"].endswith("-m10-i02")
        }
        self.assertTrue(m10_i02_ids)
        self.assertEqual(
            {gap["id"] for gap in gaps if gap["disposition"] == "M10-I02"},
            m10_i02_ids,
        )


if __name__ == "__main__":
    unittest.main()
