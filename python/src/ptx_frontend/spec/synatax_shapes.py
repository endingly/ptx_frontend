from enum import IntFlag


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
    "reg": OperandSyntaxShape.IDENTIFIER_REF,
    "imm": OperandSyntaxShape.IMMEDIATE,
    "reg_or_imm": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.IMMEDIATE,
    "reg_or_sink": OperandSyntaxShape.IDENTIFIER_REF,
    "shfl_dest": OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    "pred_pair": OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    "pred_pair_or_sink": OperandSyntaxShape.REGISTER_PREDICATE_PAIR,
    "mov_scalar_src": (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.ADDRESS
        | OperandSyntaxShape.VECTOR_MEMBER
    ),
    "cluster_address": (OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.ADDRESS),
    "vector_reg": OperandSyntaxShape.IDENTIFIER_REF,
    "vector_sreg": OperandSyntaxShape.IDENTIFIER_REF,
    "pred": OperandSyntaxShape.IDENTIFIER_REF,
    "pred_or_sink": OperandSyntaxShape.IDENTIFIER_REF,
    "pred_source": (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.PREDICATE
        | OperandSyntaxShape.NEGATED_IMMEDIATE
    ),
    "pred_or_sreg": (
        OperandSyntaxShape.IDENTIFIER_REF
        | OperandSyntaxShape.IMMEDIATE
        | OperandSyntaxShape.PREDICATE
        | OperandSyntaxShape.NEGATED_IMMEDIATE
    ),
    "pred_or_not": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.PREDICATE,
    "label": OperandSyntaxShape.BRANCH_TARGET,
    "sreg": OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.VECTOR_MEMBER,
    "symbol": OperandSyntaxShape.IDENTIFIER_REF,
    "addr": OperandSyntaxShape.ADDRESS,
    "reg_vector": OperandSyntaxShape.VECTOR_PACK,
    "descriptor": OperandSyntaxShape.IDENTIFIER_REF,
    "typed_token": OperandSyntaxShape.IDENTIFIER_REF,
    "mbarrier_state_token": OperandSyntaxShape.IDENTIFIER_REF,
    "tensor_coordinate": OperandSyntaxShape.VECTOR_PACK,
    "matrix_fragment": OperandSyntaxShape.VECTOR_PACK,
    "direct_call_target": OperandSyntaxShape.CALL_TARGET,
    "indirect_call_target": OperandSyntaxShape.CALL_TARGET,
    "indirect_call_metadata": OperandSyntaxShape.CALL_TARGET_SET,
    "branch_target_set": OperandSyntaxShape.BRANCH_TARGET_SET,
    "call_return_param": OperandSyntaxShape.CALL_PARAMETER_LIST,
    "call_arguments": OperandSyntaxShape.CALL_PARAMETER_LIST,
}
