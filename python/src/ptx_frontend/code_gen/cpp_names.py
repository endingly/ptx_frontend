"""Stable C++ form identities and ordered reference-observer spellings."""

from __future__ import annotations

from ptx_frontend.code_gen.context import GenerationInstruction
from ptx_frontend.ir.resolved_ir import ResolvedVariant


def form_name(entry: GenerationInstruction, variant: ResolvedVariant) -> str:
    """Return the unique final C++ identity of one semantic form."""

    return entry.cpp_name + variant.cpp_name


def method_name(cpp_type: str) -> str:
    """Return the stable observer callback for one foundation reference type."""

    if cpp_type not in REFERENCE_TYPES:
        raise ValueError(f"reference payload lacks typed observer callback: {cpp_type}")
    aliases = {
        "ResolvedPredicate": "predicate",
        "ResolvedRegisterRef": "reg",
        "RegOrImm": "reg_or_imm",
    }
    if cpp_type in aliases:
        return aliases[cpp_type]
    text = cpp_type.removeprefix("Resolved")
    result = ""
    for index, char in enumerate(text):
        if char.isupper() and index and (
            text[index - 1].islower()
            or (index + 1 < len(text) and text[index + 1].islower())
        ):
            result += "_"
        result += char.lower()
    return result


# Preserve declaration order: this tuple defines the generated observer surface.
# It lists C++ payload types, independently of reference-policy value kinds.
REFERENCE_TYPES: tuple[str, ...] = (
    "RegOrImm", "ResolvedAddress", "ResolvedBranchTarget",
    "ResolvedBranchTargetSet", "ResolvedCallArguments", "ResolvedCallParameterRef",
    "ResolvedCpAsyncSourceControl", "ResolvedFunctionRef", "ResolvedIndirectCallee",
    "ResolvedMbarrierStateToken", "ResolvedMovSource", "ResolvedPredicate",
    "ResolvedPredicateOrSink", "ResolvedPredicatePair", "ResolvedPredicatePairOrSink",
    "ResolvedPredicateSource", "ResolvedRegisterOrSink", "ResolvedRegisterRef",
    "ResolvedRegisterVector", "ResolvedShflSyncDestination", "ResolvedSymbolRef",
    "ResolvedValueVector",
    "ResolvedMovVectorSource",
    "ResolvedTensorCoordinate", "ResolvedTensorIm2colInfo", "ResolvedTensorOperand",
    "ResolvedSurfaceAccess", "ResolvedSurfaceQueryResource",
    "ResolvedVideoOperand",
    "ResolvedFabricHandle", "ResolvedTextureAccess", "ResolvedTextureQueryResource",
    "ResolvedTextureResult", "ResolvedStackToken", "ResolvedLocalAllocationResult",
    "TensorMemoryAddress", "ResolvedMatrixScaleSelector",
    "ResolvedSharedMatrixDescriptor", "ResolvedVectorRegisterRef",
)
