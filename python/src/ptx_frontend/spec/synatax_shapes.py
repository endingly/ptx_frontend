from enum import IntFlag

from ptx_frontend.spec.model import OperandKind


class OperandSyntaxShape(IntFlag):
    """Syntax alternatives of the C++ ``syntax_ast::AstOperand`` variant."""

    IDENTIFIER_REF = 1 << 0
    IMMEDIATE = 1 << 1
    ADDRESS = 1 << 2
    VECTOR_MEMBER = 1 << 3
    VECTOR_PACK = 1 << 4
    PREDICATE = 1 << 5
    CALL_PARAMETER_LIST = 1 << 6
    CALL_TARGET = 1 << 7
    CALL_TARGET_SET = 1 << 8
    BRANCH_TARGET = 1 << 9
    BRANCH_TARGET_SET = 1 << 10
    REGISTER_PREDICATE_PAIR = 1 << 11
    NEGATED_IMMEDIATE = 1 << 12


OPERAND_SYNTAX_SHAPES = {
    OperandKind.REGISTER: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.IMMEDIATE: OperandSyntaxShape.IMMEDIATE,
    OperandKind.REGISTER_OR_IMMEDIATE: OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
    OperandKind.REGISTER_OR_SINK: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.SHFL_DESTINATION: OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    OperandKind.PREDICATE_PAIR: OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    OperandKind.PREDICATE_PAIR_OR_SINK: OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    OperandKind.MOV_SCALAR_SOURCE: (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.ADDRESS
        | OperandSyntaxShape.VECTOR_MEMBER
    ),
    OperandKind.CLUSTER_ADDRESS: (OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.ADDRESS),
    OperandKind.VECTOR_REGISTER: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.VECTOR_SPECIAL_REGISTER: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.PREDICATE: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.PREDICATE_OR_SINK: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.PREDICATE_SOURCE: (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.PREDICATE
        | OperandSyntaxShape.NEGATED_IMMEDIATE
    ),
    OperandKind.PREDICATE_OR_SPECIAL_REGISTER: (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.PREDICATE
        | OperandSyntaxShape.NEGATED_IMMEDIATE
    ),
    OperandKind.PREDICATE_OR_NOT: OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.PREDICATE,
    OperandKind.LABEL: OperandSyntaxShape.BRANCH_TARGET,
    OperandKind.SPECIAL_REGISTER: OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.VECTOR_MEMBER,
    OperandKind.SYMBOL: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.ADDRESS: OperandSyntaxShape.ADDRESS,
    OperandKind.REGISTER_VECTOR: OperandSyntaxShape.VECTOR_PACK,
    OperandKind.DESCRIPTOR: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.TYPED_TOKEN: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.MBARRIER_STATE_TOKEN: OperandSyntaxShape.IDENTIFIER_REF,
    OperandKind.TENSOR_COORDINATE: OperandSyntaxShape.VECTOR_PACK,
    OperandKind.MATRIX_FRAGMENT: OperandSyntaxShape.VECTOR_PACK,
    OperandKind.DIRECT_CALL_TARGET: OperandSyntaxShape.CALL_TARGET,
    OperandKind.INDIRECT_CALL_TARGET: OperandSyntaxShape.CALL_TARGET,
    OperandKind.INDIRECT_CALL_METADATA: OperandSyntaxShape.CALL_TARGET_SET,
    OperandKind.BRANCH_TARGET_SET: OperandSyntaxShape.BRANCH_TARGET_SET,
    OperandKind.CALL_RETURN_PARAMETER: OperandSyntaxShape.CALL_PARAMETER_LIST,
    OperandKind.CALL_ARGUMENTS: OperandSyntaxShape.CALL_PARAMETER_LIST,
}
