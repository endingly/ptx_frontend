from importlib import import_module
from importlib.metadata import distribution, version
from importlib.resources import files
from importlib.util import find_spec
import os

from ptx_frontend.code_gen.model import (
    InstructionSpec as CompatibilityInstructionSpec,
)
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec
from ptx_frontend.spec.resources import packaged_spec_schema

EXPECTED_VERSION = os.environ["PTX_FRONTEND_EXPECTED_VERSION"]


def check_distribution_metadata() -> None:
    """Verify installed distribution metadata and entry-point policy."""

    assert version("ptx_frontend") == EXPECTED_VERSION

    assert not any(
        entry.name == "ptx-frontend-codegen"
        for entry in distribution("ptx_frontend").entry_points
    )


def check_packaged_resources() -> None:
    """Verify schemas and PTX resources are available after installation."""

    assert packaged_spec_schema().is_file()

    resource_root = files("ptx_frontend.code_gen.resources")

    assert resource_root.joinpath("ptx-instr-v1.schema.yaml").is_file()

    assert resource_root.joinpath("ptx-cpp-backend-v1.schema.yaml").is_file()

    assert resource_root.joinpath("ptx_cpp_backend_spec/ptx_frontend.yaml").is_file()

    assert resource_root.joinpath("ptx_spec/arithmetic.yaml").is_file()


def check_module_layout() -> None:
    """Verify old generator paths are absent and relocated modules work."""

    legacy_modules = (
        "ptx_frontend.code_gen.__main__",
        "ptx_frontend.code_gen.cli",
        "ptx_frontend.code_gen.gen_resolved_checker_descriptor",
        "ptx_frontend.code_gen.gen_resolved_descriptor",
        "ptx_frontend.code_gen.gen_resolved_ir",
        "ptx_frontend.code_gen.gen_resolved_value_domains",
        "ptx_frontend.code_gen.gen_syntax_ast_arch",
        "ptx_frontend.code_gen.m12_natural_corpus",
    )

    for module in legacy_modules:
        assert find_spec(module) is None, module

    packaged_modules = (
        "ptx_frontend.code_gen._frontend.cli",
        "ptx_frontend.code_gen._frontend.gen_resolved_checker_descriptor",
        "ptx_frontend.code_gen._frontend.gen_resolved_descriptor",
        "ptx_frontend.code_gen._frontend.gen_resolved_ir",
        "ptx_frontend.code_gen._frontend.gen_resolved_value_domains",
        "ptx_frontend.code_gen._frontend.gen_syntax_ast_arch",
        "ptx_frontend.code_gen._frontend.m12_natural_corpus",
        "ptx_frontend.scripts.gen_all",
        "ptx_frontend.scripts.regenerate_m12_corpus",
        "ptx_frontend.scripts.validate_yaml",
    )

    # Import rather than merely calling find_spec so missing dependencies or
    # broken relocated imports are detected by the installed-wheel smoke test.
    for module in packaged_modules:
        import_module(module)


def check_packaged_spec_model() -> None:
    """Exercise the normalized specification model from the installed wheel."""

    assert InstructionSpec is CompatibilityInstructionSpec

    database = load_packaged_spec_database()

    assert database.instructions
    assert all(isinstance(item, InstructionSpec) for item in database.instructions)

    assert any(item.opcode == "add" for item in database.instructions)

    fma = next(item for item in database.instructions if item.opcode == "fma")

    assert tuple(variant.name for variant in fma.variants) == (
        "fma_rn_f32",
        "fma_directed_f32",
        "fma_rn_f64",
        "fma_directed_f64",
        "fma_f32x2",
        "fma_rn_f16",
        "fma_rn_f16x2",
        "fma_half_relu",
        "fma_half_oob",
        "fma_half_oob_relu",
        "fma_bf16",
        "fma_bf16x2",
        "fma_bf16_oob",
        "fma_bf16x2_oob",
        "fma_mixed_f32_f16",
        "fma_mixed_f32_bf16",
    )

    layouts = {
        variant.name: variant.operand_layouts[0].operands for variant in fma.variants
    }

    assert [operand.kind for operand in layouts["fma_rn_f32"][1:]] == ["reg_or_imm"] * 3

    assert [operand.kind for operand in layouts["fma_f32x2"]] == ["reg"] * 4

    assert [operand.kind for operand in layouts["fma_bf16x2"]] == ["reg"] * 4

    for name in (
        "fma_mixed_f32_f16",
        "fma_mixed_f32_bf16",
    ):
        operands = layouts[name]

        assert [operand.kind for operand in operands] == [
            "reg",
            "reg",
            "reg",
            "reg_or_imm",
        ]

        assert operands[0].type_expression.modifier_name == "result_type"
        assert operands[3].type_expression.modifier_name == "result_type"

    assert layouts["fma_mixed_f32_f16"][1].type_expression.modifier_name == "input_type"

    assert layouts["fma_mixed_f32_bf16"][1].type_expression.scalar_type == "b16"


def main() -> None:
    check_distribution_metadata()
    check_packaged_resources()
    check_module_layout()
    check_packaged_spec_model()


if __name__ == "__main__":
    main()
