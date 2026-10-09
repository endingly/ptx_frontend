from importlib import import_module
import hashlib
from importlib.metadata import distribution, version
from importlib.resources import files
from importlib.util import find_spec
import os

from ptx_frontend.code_gen.cpp_backend import CppDomain, load_cpp_backend
from ptx_frontend.code_gen.database import (
    get_packaged_spec_database as compatibility_get_packaged_spec_database,
)
from ptx_frontend.code_gen.model import (
    InstructionSpec as CompatibilityInstructionSpec,
)
from ptx_frontend.spec.database import get_packaged_spec_database
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
    installed_cp_spec = packaged_spec_dir().joinpath(
        "data_movement_and_conversion.yaml"
    ).read_bytes()
    assert hashlib.sha256(installed_cp_spec).hexdigest() == os.environ[
        "PTX_FRONTEND_EXPECTED_CP_SPEC_SHA256"
    ]
    assert packaged_spec_dir().joinpath("tensor_memory_data_movement.yaml").is_file()


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
        "ptx_frontend.code_gen.resolved_layout",
        "ptx_frontend.code_gen.emit.resolved_model",
        "ptx_frontend.code_gen.emit.resolved_source",
        "ptx_frontend.code_gen.emit.resolved_dispatch",
        "ptx_frontend.code_gen.emit.resolved_descriptors",
        "ptx_frontend.code_gen.emit.checker_descriptors",
        "ptx_frontend.code_gen.emit.syntax_descriptors",
        "ptx_frontend.code_gen.emit.value_domains",
        "ptx_frontend.code_gen.emit.matrix",
        "ptx_frontend.code_gen.emit.tcgen_descriptor_domains",
        "ptx_frontend.code_gen.emit.tcgen_mma_operations",
        "ptx_frontend.code_gen.emit.tensor_map_known_facts",
        "ptx_frontend.code_gen.emit.tensor_cache_controls",
        "ptx_frontend.ir.tensor_reduction",
        "ptx_frontend.spec.tcgen_descriptor_domains",
        "ptx_frontend.spec.tcgen_mma_operations",
        "ptx_frontend.spec.tensor_map_known_facts",
        "ptx_frontend.spec.normalize.matrix",
        "ptx_frontend.spec.normalize.tcgen_allocation",
        "ptx_frontend.spec.normalize.tcgen_copy_shift",
        "ptx_frontend.spec.normalize.tcgen_load_store",
        "ptx_frontend.spec.normalize.tcgen_mma",
        "ptx_frontend.spec.normalize.tcgen_sync",
        "ptx_frontend.scripts.gen_all",
        "ptx_frontend.scripts.validate_yaml",
    )

    # Import rather than merely calling find_spec so missing dependencies or
    # broken relocated imports are detected by the installed-wheel smoke test.
    for module in packaged_modules:
        import_module(module)

    from ptx_frontend.code_gen.emit.resolved_model import FORM_SHARD_SIZE
    from ptx_frontend.spec.tcgen_descriptor_domains import (
        validate_catalogue as validate_descriptor_catalogue,
    )
    from ptx_frontend.spec.tcgen_mma_operations import (
        F8F6F4KnownFacts,
        F8F6F4_SHAPES,
        MX8_SHAPES,
        Mx8KnownFacts,
        check_f8f6f4_known_facts,
        check_mx8_known_facts,
        validate_catalogue as validate_mma_catalogue,
    )
    from ptx_frontend.spec.tensor_map_known_facts import TensorFactRule

    assert FORM_SHARD_SIZE == 64
    assert len(TensorFactRule) == 22
    validate_descriptor_catalogue()
    validate_mma_catalogue()
    assert len(F8F6F4_SHAPES) == 4
    low = check_f8f6f4_known_facts(F8F6F4KnownFacts(
        group=1, m=64, n=8, k=32, d_type="F32", a_type="E2M1",
        b_type="E4M3", sparse=False, a_shared=False))
    assert "shape_or_output_type" not in low.violations
    assert "a_low_bit_packing_rule" in low.obligations
    assert len(MX8_SHAPES) == 2
    mx8 = check_mx8_known_facts(Mx8KnownFacts(
        group=1, m=128, n=16, k=32, d_type="F32", a_type="E4M3",
        b_type="E5M2", sparse=False, a_shared=False,
        scale_selector="absent", scale_type="UE8M0",
        scale_a_id=0, scale_b_id=3))
    assert mx8.known_facts_ok and mx8.scale_a_layout is not None
    from ptx_frontend.spec.tcgen_mma_operations import MxScaleLayoutId
    assert mx8.scale_a_layout.layout_id is MxScaleLayoutId.MX1
    assert "scale_b_layout" in mx8.obligations


def check_packaged_spec_model() -> None:
    """Exercise the normalized specification model from the installed wheel."""

    assert InstructionSpec is CompatibilityInstructionSpec
    assert get_packaged_spec_database is compatibility_get_packaged_spec_database

    database = get_packaged_spec_database()
    assert get_packaged_spec_database() is database

    assert database.instructions
    assert all(isinstance(item, InstructionSpec) for item in database.instructions)

    assert any(item.opcode == "add" for item in database.instructions)

    cp = next(item for item in database.instructions if item.opcode == "cp")
    assert len(cp.variants) == 473
    assert sum(
        "tensor_" in variant.name and variant.name.endswith("_cache_hint")
        for variant in cp.variants
    ) == 178

    tcgen = next(item for item in database.instructions
                 if item.opcode == "tcgen05")
    f8 = next(item for item in tcgen.variants
              if item.name == "tcgen05_mma_f8f6f4")
    assert len(f8.operand_layouts) == 4
    assert f8.modifier_order_aliases == ()
    assert all("scale_input_d" not in {operand.name for operand in layout.operands}
               for layout in f8.operand_layouts)
    mx8 = next(item for item in tcgen.variants
               if item.name == "tcgen05_mma_mxf8f6f4")
    assert len(mx8.operand_layouts) == 2
    assert {layout.name for layout in mx8.operand_layouts} == {"shared", "tensor"}
    assert all(tuple(operand.name for operand in layout.operands)[-3:] ==
               ("scale_a", "scale_b", "enable_input_d")
               for layout in mx8.operand_layouts)

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
