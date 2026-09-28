from pathlib import Path
import unittest

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.resources import packaged_spec_dir

SPEC_DIR = packaged_spec_dir()


EXPECTED_FILES = {
    "arithmetic.yaml": ("arithmetic", "arithmetic", None),
    "comparison_and_selection.yaml": (
        "comparison_and_selection",
        "comparison_and_selection",
        "9.7.6",
    ),
    "logic_and_shift.yaml": ("logic_and_shift", "arithmetic", "9.7.8"),
    "data_movement_and_conversion.yaml": (
        "data_movement_and_conversion",
        "data_movement",
        "9.7.9",
    ),
    "control_flow.yaml": ("control_flow", "control_flow", "9.7.13"),
    "parallel_synchronization_and_communication.yaml": (
        "parallel_synchronization_and_communication",
        "parallel_synchronization_and_communication",
        "9.7.14",
    ),
    "warp_level_matrix_multiply_accumulate.yaml": (
        "warp_level_matrix_multiply_accumulate",
        "matrix",
        "9.7.15",
    ),
    "miscellaneous.yaml": ("miscellaneous", "control_flow", "9.7.20"),
}

EXPECTED_SECTIONS = {
    "arithmetic.yaml": {
        "add": {"9.7.1.1", "9.7.2.1", "9.7.3.3", "9.7.4.1", "9.7.5.1"},
        "addc": {"9.7.2.2"},
        "sub": {"9.7.1.2", "9.7.2.3", "9.7.3.4", "9.7.4.2", "9.7.5.2"},
        "subc": {"9.7.2.4"},
        "madc": {"9.7.2.6"},
        "mul": {"9.7.1.3", "9.7.3.5", "9.7.4.3"},
        "mad": {"9.7.1.4", "9.7.2.5", "9.7.3.7"},
        "clmad": {"9.7.1.5"},
        "mul24": {"9.7.1.6"},
        "mad24": {"9.7.1.7"},
        "sad": {"9.7.1.8"},
        "div": {"9.7.1.9", "9.7.3.8"},
        "rem": {"9.7.1.10"},
        "min": {"9.7.1.13", "9.7.3.11", "9.7.4.7"},
        "max": {"9.7.1.14", "9.7.3.12", "9.7.4.8"},
        "abs": {"9.7.1.11", "9.7.3.9", "9.7.4.6"},
        "neg": {"9.7.1.12", "9.7.3.10", "9.7.4.5"},
        "popc": {"9.7.1.15"},
        "clz": {"9.7.1.16"},
        "bfind": {"9.7.1.17"},
        "fns": {"9.7.1.18"},
        "brev": {"9.7.1.19"},
        "bfe": {"9.7.1.20"},
        "bfi": {"9.7.1.21"},
        "szext": {"9.7.1.22"},
        "bmsk": {"9.7.1.23"},
        "dp4a": {"9.7.1.24"},
        "dp2a": {"9.7.1.25"},
        "fma": {"9.7.3.6", "9.7.4.4", "9.7.5.3"},
        "testp": {"9.7.3.1"},
        "copysign": {"9.7.3.2"},
        "rcp": {"9.7.3.13"},
        "sqrt": {"9.7.3.15"},
        "rsqrt": {"9.7.3.16"},
        "sin": {"9.7.3.18"},
        "cos": {"9.7.3.19"},
        "lg2": {"9.7.3.20"},
        "ex2": {"9.7.3.21", "9.7.4.10"},
        "tanh": {"9.7.3.22", "9.7.4.9"}
    },
    "comparison_and_selection.yaml": {
        "set": {"9.7.6.1", "9.7.7.1"},
        "setp": {"9.7.6.2", "9.7.7.2"},
        "selp": {"9.7.6.3"},
        "slct": {"9.7.6.4"},
    },
    "logic_and_shift.yaml": {
        "and": {"9.7.8.1"},
        "or": {"9.7.8.2"},
        "xor": {"9.7.8.3"},
        "not": {"9.7.8.4"},
        "cnot": {"9.7.8.5"},
        "lop3": {"9.7.8.6"},
        "shf": {"9.7.8.7"},
        "shl": {"9.7.8.8"},
        "shr": {"9.7.8.9"},
    },
    "data_movement_and_conversion.yaml": {
        "mov": {"9.7.9"},
        "mapa": {"9.7.9.24"},
        "getctarank": {"9.7.9.25"},
        "shfl": {"9.7.9.6"},
        "ld": {"9.7.9.8", "9.7.9.9"},
        "ldu": {"9.7.9.10"},
        "st": {"9.7.9.11"},
        "prefetch": {"9.7.9.16"},
        "prefetchu": {"9.7.9.16"},
        "applypriority": {"9.7.9.17"},
        "discard": {"9.7.9.18"},
        "createpolicy": {"9.7.9.19"},
        "prmt": {"9.7.9.7"},
        "isspacep": {"9.7.9.20"},
        "cvta": {"9.7.9.21"},
        "cvt": {"9.7.9.22", "9.7.9.23"},
        "cp": {"9.7.9.26.3.1", "9.7.9.26.3.2", "9.7.9.26.3.3", "9.7.14.16.18"},
    },
    "control_flow.yaml": {
        "bra": {"9.7.13.3"},
        "brx": {"9.7.13.4"},
        "call": {"9.7.13.5"},
        "ret": {"9.7.13.6"},
        "exit": {"9.7.13.7"},
    },
    "parallel_synchronization_and_communication.yaml": {
        "bar": {"9.7.14.1", "9.7.14.2"},
        "barrier": {"9.7.14.1", "9.7.14.3"},
        "membar": {"9.7.14.4"},
        "fence": {"9.7.14.4"},
        "atom": {"9.7.14.5"},
        "red": {"9.7.14.6"},
        "vote": {"9.7.14.10"},
        "match": {"9.7.14.11"},
        "activemask": {"9.7.14.12"},
        "redux": {"9.7.14.13"},
        "griddepcontrol": {"9.7.14.14"},
        "clusterlaunchcontrol": {"9.7.14.18", "9.7.14.19"},
        "elect": {"9.7.14.15"},
        "mbarrier": {
            "9.7.14.16.12",
            "9.7.14.16.13",
            "9.7.14.16.14",
            "9.7.14.16.15",
            "9.7.14.16.16",
            "9.7.14.16.17",
            "9.7.14.16.19",
            "9.7.14.16.20",
            "9.7.14.16.21",
        },
    },
    "warp_level_matrix_multiply_accumulate.yaml": {
        "mma": {"9.7.15.5.14"},
        "ldmatrix": {"9.7.15.5.15"},
    },
    "miscellaneous.yaml": {"trap": {"9.7.20.4"}, "setmaxnreg": {"9.7.20.5"}},
}


class PtxSpecTaxonomyTests(unittest.TestCase):
    def test_ptx_93_taxonomy_files_and_sections(self) -> None:
        paths = {
            entry.name
            for entry in SPEC_DIR.iterdir()
            if entry.is_file()
            and entry.name.endswith(".yaml")
            and not entry.name.endswith(".schema.yaml")
        }
        self.assertEqual(paths, set(EXPECTED_FILES))

        for name, (category, codegen_category, section) in EXPECTED_FILES.items():
            spec = load_yaml(SPEC_DIR.joinpath(name))

            self.assertEqual(spec["ptx_isa"], "9.3")
            self.assertEqual(spec["category"], category)
            self.assertEqual(spec["codegen_category"], codegen_category)
            self.assertEqual(spec.get("section"), section)

            actual: dict[str, set[str]] = {}
            for instruction in spec["instructions"]:
                actual.setdefault(instruction["opcode"], set()).add(
                    instruction["section"]
                )
                for variant in instruction["variants"]:
                    actual[instruction["opcode"]].add(
                        variant.get("section", instruction["section"])
                    )

                self.assertTrue(
                    instruction["section"].startswith(section or "9.7.")
                    or instruction["section"]
                    in EXPECTED_SECTIONS[name].get(instruction["opcode"], set())
                )

            self.assertEqual(actual, EXPECTED_SECTIONS[name])

    def test_standalone_cta_forms_use_barrier_taxonomy(self) -> None:
        """Keep standalone CTA forms separate from legacy and cluster forms."""
        spec = load_yaml(
            SPEC_DIR / "parallel_synchronization_and_communication.yaml"
        )
        instructions = {
            instruction["opcode"]: instruction
            for instruction in spec["instructions"]
        }
        variants = {
            variant["name"]: variant
            for variant in instructions["barrier"]["variants"]
        }
        self.assertEqual(
            set(variants),
            {"barrier_sync", "barrier_cta_sync", "barrier_arrive",
             "barrier_cta_arrive", "barrier_cluster_arrive",
             "barrier_cluster_wait", "barrier_red_popc_u32",
             "barrier_cta_red_popc_u32", "barrier_red_and_pred",
             "barrier_cta_red_and_pred", "barrier_red_or_pred",
             "barrier_cta_red_or_pred"},
        )
        for name in ("barrier_arrive", "barrier_cta_arrive",
                     "barrier_red_popc_u32", "barrier_cta_red_popc_u32",
                     "barrier_red_and_pred", "barrier_cta_red_and_pred",
                     "barrier_red_or_pred", "barrier_cta_red_or_pred"):
            self.assertEqual(
                variants[name].get("section", instructions["barrier"]["section"]),
                "9.7.14.1",
            )
        self.assertEqual(instructions["bar"]["section"], "9.7.14.1")

    def test_test_wait_qualifiers_are_paired_for_each_structural_form(self) -> None:
        """Require one explicit semantic/scope row per existing wait shape."""
        spec = load_yaml(
            SPEC_DIR / "parallel_synchronization_and_communication.yaml"
        )
        mbarrier = next(item for item in spec["instructions"]
                        if item["opcode"] == "mbarrier")
        waits = {item["name"]: item for item in mbarrier["variants"]
                 if item["name"].startswith("mbarrier_test_wait_")}
        paired = {name: item for name, item in waits.items()
                  if "_semantics_" in name}
        self.assertEqual(len(waits), 20)
        self.assertEqual(len(paired), 10)
        for name, variant in paired.items():
            base = name.replace("_semantics_", "_")
            self.assertIn(base, waits)
            self.assertEqual(variant["section"], "9.7.14.16.19")
            modifiers = variant["modifiers"]
            names = [modifier["name"] for modifier in modifiers]
            self.assertEqual(names.index("scope"), names.index("semantics") + 1)
            self.assertEqual(modifiers[names.index("semantics")]["presence"],
                             "required")
            self.assertEqual(modifiers[names.index("scope")]["presence"],
                             "required")
            self.assertEqual(variant.get("operands"), waits[base].get("operands"))
            self.assertEqual(variant.get("operand_layouts"),
                             waits[base].get("operand_layouts"))
            self.assertEqual(variant["constraints"], waits[base]["constraints"])

    def test_membar_levels_are_distinct_and_target_qualified(self) -> None:
        """Keep source `.gl` distinct from the backend GPU scope spelling."""
        spec = load_yaml(
            SPEC_DIR / "parallel_synchronization_and_communication.yaml"
        )
        membar = next(item for item in spec["instructions"]
                      if item["opcode"] == "membar")
        variants = {item["name"]: item for item in membar["variants"]}
        self.assertEqual(set(variants),
                         {"membar_cta", "membar_gl", "membar_sys",
                          "membar_proxy_alias", "membar_proxy_async",
                          "membar_proxy_async_shared_cluster"})
        for name, floor, value in (
            ("membar_cta", {"ptx": "1.4", "sm": 0}, "cta"),
            ("membar_gl", {"ptx": "1.4", "sm": 0}, "gl"),
            ("membar_sys", {"ptx": "2.0", "sm": 20}, "sys"),
        ):
            variant = variants[name]
            self.assertEqual(variant["availability"], floor)
            self.assertEqual(variant["operands"], [])
            self.assertEqual(variant["modifiers"][0]["value"], value)
            self.assertEqual(variant["modifiers"][0]["presence"], "fixed")
        backend = load_yaml(
            SPEC_DIR.parent / "ptx_cpp_backend_spec/ptx_frontend.yaml"
        )
        self.assertEqual(
            backend["domains"]["memory_scopes"]["values"]["gl"],
            "MemoryScope::Gpu",
        )
        alias = variants["membar_proxy_alias"]
        self.assertEqual(alias["availability"], {"ptx": "7.5", "sm": 60})
        self.assertEqual(alias["operands"], [])
        self.assertEqual(
            [(modifier["name"], modifier["kind"], modifier["presence"],
              modifier["value"], modifier["token"])
             for modifier in alias["modifiers"]],
            [("proxy", "flag", "fixed", True, ".proxy"),
             ("alias", "flag", "fixed", True, ".alias")],
        )
        for name, values, floor in (
            ("membar_proxy_async",
             ["async", "async.global", "async.shared::cta"],
             {"ptx": "8.0", "sm": 90}),
            ("membar_proxy_async_shared_cluster",
             ["async.shared::cluster"],
             {"any_of": [{"ptx": "8.0", "sm": 90,
                          "capabilities": ["cluster"]}]}),
        ):
            variant = variants[name]
            self.assertEqual(variant["availability"], floor)
            self.assertEqual(variant["operands"], [])
            self.assertEqual(variant["modifiers"][0]["token"], ".proxy")
            self.assertEqual(variant["modifiers"][1]["kind"], "proxy")
            self.assertEqual(variant["modifiers"][1]["domain"],
                             "async_proxy_kinds")
            self.assertEqual(variant["modifiers"][1]["values"], values)

    def test_try_wait_qualifiers_are_paired_for_each_structural_form(self) -> None:
        """Keep explicit qualifiers paired across all try-wait layouts."""
        spec = load_yaml(
            SPEC_DIR / "parallel_synchronization_and_communication.yaml"
        )
        mbarrier = next(item for item in spec["instructions"]
                        if item["opcode"] == "mbarrier")
        waits = {item["name"]: item for item in mbarrier["variants"]
                 if item["name"].startswith("mbarrier_try_wait_")}
        paired = {name: item for name, item in waits.items()
                  if "_semantics_" in name}
        self.assertEqual(len(waits), 20)
        self.assertEqual(len(paired), 10)
        for name, variant in paired.items():
            base = name.replace("_semantics_", "_")
            self.assertIn(base, waits)
            self.assertEqual(variant["section"], "9.7.14.16.19")
            modifiers = variant["modifiers"]
            names = [modifier["name"] for modifier in modifiers]
            self.assertEqual(names.index("scope"), names.index("semantics") + 1)
            self.assertEqual(modifiers[names.index("semantics")]["presence"],
                             "required")
            self.assertEqual(modifiers[names.index("scope")]["presence"],
                             "required")
            self.assertEqual(variant["operand_layouts"],
                             waits[base]["operand_layouts"])
            self.assertEqual(variant["constraints"], waits[base]["constraints"])


if __name__ == "__main__":
    unittest.main()
