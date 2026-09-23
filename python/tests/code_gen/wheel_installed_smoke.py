from importlib import import_module
from importlib.metadata import distribution, version
from importlib.resources import files
from importlib.util import find_spec
import os

from ptx_frontend.code_gen.cpp_backend import CppDomain, load_cpp_backend
from ptx_frontend.code_gen.model import (
    InstructionSpec as CompatibilityInstructionSpec,
)
from ptx_frontend.spec.database import load_packaged_spec_database
from ptx_frontend.spec.model import InstructionSpec
from ptx_frontend.spec.model import OperandKind
from ptx_frontend.spec.resources import (
    packaged_backend_spec,
    packaged_backend_spec_schema,
    packaged_spec_dir,
    packaged_spec_schema,
)

EXPECTED_VERSION = os.environ["PTX_FRONTEND_EXPECTED_VERSION"]


def check_distribution_metadata() -> None:
    """Verify installed distribution metadata and entry-point policy."""

    installed = distribution("ptx_frontend")
    assert installed.metadata["Version"] == EXPECTED_VERSION
    assert version("ptx_frontend") == EXPECTED_VERSION

    assert not any(
        entry.name == "ptx-frontend-codegen" for entry in installed.entry_points
    )


def check_packaged_resources() -> None:
    """Verify schemas and PTX resources are available after installation."""

    assert packaged_spec_schema().is_file()
    assert packaged_backend_spec_schema().is_file()
    assert packaged_backend_spec().is_file()
    assert packaged_spec_dir().joinpath("arithmetic.yaml").is_file()


def check_module_layout() -> None:
    """Verify the flattened generator imports and absent corpus-tool surface."""

    absent_modules = (
        "ptx_frontend.code_gen._frontend",
        "ptx_frontend.code_gen.m12_natural_corpus",
        "ptx_frontend.scripts.regenerate_m12_corpus",
        "ptx_frontend.scripts.regenerate_nvcc",
        "ptx_frontend.code_gen.emit.natural_emission",
    )

    for module in absent_modules:
        assert find_spec(module) is None, module

    packaged_modules = (
        "ptx_frontend.code_gen.resolved_field_names",
        "ptx_frontend.spec.semantic_domains",
        "ptx_frontend.code_gen.cli",
        "ptx_frontend.code_gen.context",
        "ptx_frontend.code_gen.plan",
        "ptx_frontend.code_gen.emit.resolved_model",
        "ptx_frontend.code_gen.emit.resolved_resolver",
        "ptx_frontend.code_gen.emit.resolved_checker",
        "ptx_frontend.code_gen.emit.category_source",
        "ptx_frontend.code_gen.emit.references",
        "ptx_frontend.code_gen.emit.resolved_dispatch",
        "ptx_frontend.code_gen.emit.resolved_descriptors",
        "ptx_frontend.code_gen.emit.checker_descriptors",
        "ptx_frontend.code_gen.emit.syntax_descriptors",
        "ptx_frontend.code_gen.emit.value_domains",
        "ptx_frontend.scripts.gen_all",
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

    assert [operand.kind for operand in layouts["fma_rn_f32"][1:]] == [
        OperandKind.REGISTER_OR_IMMEDIATE
    ] * 3

    assert [operand.kind for operand in layouts["fma_f32x2"]] == [OperandKind.REGISTER] * 4

    assert [operand.kind for operand in layouts["fma_bf16x2"]] == [OperandKind.REGISTER] * 4

    for name in (
        "fma_mixed_f32_f16",
        "fma_mixed_f32_bf16",
    ):
        operands = layouts[name]

        assert [operand.kind for operand in operands] == [
            OperandKind.REGISTER,
            OperandKind.REGISTER,
            OperandKind.REGISTER,
            OperandKind.REGISTER_OR_IMMEDIATE,
        ]

        assert (
            operands[
                0
            ].type_expression.modifier_name  # pyright: ignore[reportOptionalMemberAccess]
            == "result_type"
        )
        assert (
            operands[
                3
            ].type_expression.modifier_name  # pyright: ignore[reportOptionalMemberAccess]
            == "result_type"
        )

    assert (
        layouts["fma_mixed_f32_f16"][
            1
        ].type_expression.modifier_name  # pyright: ignore[reportOptionalMemberAccess]
        == "input_type"
    )

    assert (
        layouts["fma_mixed_f32_bf16"][
            1
        ].type_expression.scalar_type  # pyright: ignore[reportOptionalMemberAccess]
        == "b16"
    )


def check_packaged_backend_model() -> None:
    """Read and validate the C++ backend YAML and schema from the installed wheel."""

    backend = load_cpp_backend(packaged_backend_spec())

    assert backend.backend_schema == "ptx-cpp-backend/v2"
    assert CppDomain.SCALAR_TYPES.value in backend.domains
    assert backend.domains[CppDomain.SCALAR_TYPES.value].values


def main() -> None:
    check_distribution_metadata()
    check_packaged_resources()
    check_module_layout()
    check_packaged_spec_model()
    check_packaged_backend_model()


if __name__ == "__main__":
    main()
