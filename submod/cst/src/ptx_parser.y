%require "3.8"
%language "c++"
%define api.namespace {ptx_frontend::detail}
%define api.parser.class {GeneratedPtxParser}
%define api.value.type variant
%define api.token.constructor
%define parse.error custom
%define parse.lac full
%parse-param {PtxParserDriver& driver}
%lex-param {PtxParserDriver& driver}
%code requires {
#include "ptx_parser_driver.hpp"
}
%code provides {
namespace ptx_frontend::detail {
/** Feed one original token or the synthetic root selector to the generated parser. */
GeneratedPtxParser::symbol_type yylex(PtxParserDriver& driver);
}
}
%code {
#include <algorithm>
using namespace ptx_frontend::syntax_cst;
}
%token END 0
%token MODE_INSTRUCTION MODE_MODULE
%token <syntax_cst::TokenId> LITERAL IDENT CALL BRA BRX DOT_IDENT CAST UNIFIED
%token <syntax_cst::TokenId> LP RP QUESTION COLON SEMI COMMA AT LBRACKET RBRACKET LBRACE RBRACE
%token <syntax_cst::TokenId> PLUS MINUS NOT TILDE STAR SLASH PERCENT
%token <syntax_cst::TokenId> SHL SHR LT LE GT GE EQ NE AMP CARET PIPE AND OR
%token <syntax_cst::TokenId> DOT_GLOBAL DOT_CONST DOT_SHARED DOT_LOCAL DOT_PARAM DOT_WEAK ENTRY
%token <syntax_cst::TokenId> DOT_VERSION DOT_TARGET DOT_ADDRESS_SIZE DOT_LOC DOT_PRAGMA DOT_FILE DOT_SECTION
%token <syntax_cst::TokenId> DOT_EXTERN DOT_VISIBLE DOT_FUNC DOT_CALL_PROTOTYPE DOT_CALL_TARGETS DOT_BRANCH_TARGETS
%token <syntax_cst::TokenId> DOT_MAXNREG DOT_MAXNTID DOT_REQNTID DOT_MINNCTAPERSM DOT_REQNCTAPERCLUSTER
%token <syntax_cst::TokenId> DOT_EXPLICITCLUSTER DOT_MAXCLUSTERRANK DOT_NORETURN DOT_REG DOT_ALIGN DOT_PTR DOT_TEX
%token <syntax_cst::TokenId> STRING DOT EQ_ASSIGN
%token <syntax_cst::TokenId> INVALID
%type <syntax_cst::TokenId> identifier dot_identifier modifier unary_op add_op mul_op shift_op relation_op equality_op
%type <std::vector<syntax_cst::TokenId>> modifiers
%type <std::optional<syntax_cst::CstPredicate>> predicate
%type <InstructionHead> ordinary_head call_head branch_head indexed_head
%type <syntax_cst::CstInstruction> instruction
%type <syntax_cst::CstOperand> operand bracket_operand
%type <syntax_cst::CstVectorPack> vector
%type <std::vector<syntax_cst::CstVectorElement>> vector_elements
%type <syntax_cst::CstVectorElement> vector_element
%type <std::vector<syntax_cst::CstOperandElement>> operands operands_nonempty call_operands
%type <syntax_cst::CstCallParameterList> input_list return_list
%type <std::vector<syntax_cst::CstCallParameter>> input_parameters input_parameters_nonempty
%type <syntax_cst::CstAddressBase> address_base bracket_component input_parameter
%type <AddressContent> address_content
%type <BracketHeads> bracket_heads
%type <std::optional<syntax_cst::CstAddressOffset>> address_offset
%type <std::optional<syntax_cst::TokenId>> unified selector
%type <syntax_cst::CstNamedArrayIndex> named_index
%type <syntax_cst::CstModule> module
%type <std::vector<syntax_cst::CstModuleItem>> module_items
%type <syntax_cst::CstFunction> function
%type <std::vector<syntax_cst::CstFunctionBodyItem>> body
%type <ParsedExpression> expression numeric_expression operand_expression unary numeric_unary operand_unary postfix numeric_postfix atom numeric_atom cast_expression
%type <PtxParserDriver::Guard> paren_guard call_guard cast_guard cast_close_guard
%type <ParsedExpression> logical_or logical_and bit_or bit_xor bit_and equality relation shift sum product
%type <ParsedExpression> numeric_logical_or numeric_logical_and numeric_bit_or numeric_bit_xor numeric_bit_and numeric_equality numeric_relation numeric_shift numeric_sum numeric_product
%type <ParsedExpression> operand_logical_or operand_logical_and operand_bit_or operand_bit_xor operand_bit_and operand_equality operand_relation operand_shift operand_sum operand_product

%%
input:
  MODE_INSTRUCTION instruction END { driver.root = std::move($2); }
| MODE_MODULE module END { driver.root = std::move($2); };
identifier: IDENT | CALL | BRA | BRX;
dot_identifier: DOT_IDENT | CAST | UNIFIED;
modifier: dot_identifier | DOT_GLOBAL | DOT_CONST | DOT_SHARED | DOT_LOCAL | DOT_PARAM | DOT_WEAK;
modifiers:
  %empty { $$ = {}; }
| modifiers modifier { $1.push_back($2); $$ = std::move($1); };
predicate:
  AT identifier { $$ = CstPredicate{$1, std::nullopt, $2, {$1, $2 + 1}}; }
| AT NOT identifier { $$ = CstPredicate{$1, $2, $3, {$1, $3 + 1}}; };
ordinary_head: IDENT modifiers {
  driver.markShifted($2.empty()?$1:$2.back());
  driver.beginStatement($1); $$ = {std::nullopt,$1,std::move($2),$1};
} | predicate IDENT modifiers {
  driver.markShifted($3.empty()?$2:$3.back());
  const auto first = $1 ? $1->token_range.first : $2;
  driver.beginStatement(first); $$ = {std::move($1), $2, std::move($3), first};
};
call_head: CALL modifiers {
  driver.markShifted($2.empty()?$1:$2.back());
  driver.beginStatement($1); $$ = {std::nullopt,$1,std::move($2),$1};
} | predicate CALL modifiers {
  driver.markShifted($3.empty()?$2:$3.back());
  const auto first = $1 ? $1->token_range.first : $2;
  driver.beginStatement(first); $$ = {std::move($1), $2, std::move($3), first};
};
branch_head: BRA modifiers {
  driver.markShifted($2.empty()?$1:$2.back());
  driver.beginStatement($1); $$ = {std::nullopt,$1,std::move($2),$1};
} | predicate BRA modifiers {
  driver.markShifted($3.empty()?$2:$3.back());
  const auto first = $1 ? $1->token_range.first : $2;
  driver.beginStatement(first); $$ = {std::move($1), $2, std::move($3), first};
};
indexed_head: BRX modifiers {
  driver.markShifted($2.empty()?$1:$2.back());
  driver.beginStatement($1); $$ = {std::nullopt,$1,std::move($2),$1};
} | predicate BRX modifiers {
  driver.markShifted($3.empty()?$2:$3.back());
  const auto first = $1 ? $1->token_range.first : $2;
  driver.beginStatement(first); $$ = {std::move($1), $2, std::move($3), first};
};
instruction:
  ordinary_head operands SEMI { $$ = driver.instruction(std::move($1), std::move($2), $3); }
| call_head call_operands SEMI { $$ = driver.instruction(std::move($1), std::move($2), $3); }
| branch_head identifier SEMI {
    std::vector<CstOperandElement> values;
    values.push_back({CstBranchTarget{{$2}, {$2, $2 + 1}}, std::nullopt});
    $$ = driver.instruction(std::move($1), std::move(values), $3);
  }
| indexed_head identifier COMMA identifier SEMI {
    std::vector<CstOperandElement> values;
    values.push_back({CstIdentifier{$2}, $3});
    values.push_back({CstBranchTargetSet{{$4}, {$4, $4 + 1}}, std::nullopt});
    $$ = driver.instruction(std::move($1), std::move(values), $5);
  };
operands:
  %empty { $$ = {}; }
| operands_nonempty { $$ = std::move($1); };
operands_nonempty:
  operand { $$.push_back({std::move($1), std::nullopt}); }
| operands_nonempty COMMA operand {
    $1.back().trailing_comma = $2;
    $1.push_back({std::move($3), std::nullopt}); $$ = std::move($1);
  };
operand:
  operand_expression { $$ = driver.numericOperand(std::move($1)); }
| identifier { $$ = CstIdentifier{$1}; }
| identifier dot_identifier { $$ = CstVectorMember{{$1}, $2, {$1, $2 + 1}}; }
| identifier PIPE identifier { $$ = CstRegisterPredicatePair{{$1}, $2, {$3}, {$1, $3 + 1}}; }
| identifier add_op expression {
    const auto last = $3.expression.token_range.last;
    $$ = CstAddress{std::nullopt, CstIdentifier{$1},
        CstAddressOffset{$2, driver.numeric(std::move($3)), {$2, last}},
        std::nullopt, std::nullopt, {$1, last}};
  }
| identifier named_index {
    const auto last = $2.token_range.last;
    CstAddress address{.base=CstIdentifier{$1}, .token_range={$1,last}};
    address.named_index = std::move($2); $$ = std::move(address);
  }
| MINUS identifier selector {
    if ($3) $$ = CstNegatedRegisterOperand{$1,
        CstVectorMember{{$2}, *$3, {$2, *$3 + 1}}, {$1, *$3 + 1}};
    else $$ = CstNegatedRegisterOperand{$1, CstIdentifier{$2}, {$1, $2 + 1}};
  }
| NOT identifier { $$ = CstPredicateOperand{$1, $2, {$1, $2 + 1}}; }
| bracket_operand { $$ = std::move($1); }
| vector { $$ = std::move($1); }
| vector PIPE identifier {
    const auto first = $1.token_range.first;
    $$ = CstVectorPredicatePair{std::move($1), $2, {$3}, {first, $3 + 1}};
  };
selector: %empty { $$ = std::nullopt; } | dot_identifier { $$ = $1; };
named_index:
  LBRACKET identifier address_offset RBRACKET {
    $$ = {$1, CstIdentifier{$2}, std::move($3), $4, {$1, $4 + 1}};
  }
| LBRACKET numeric_expression RBRACKET {
    $$ = {$1, driver.numericBase(std::move($2)), std::nullopt, $3, {$1, $3 + 1}};
  };
address_offset:
  %empty { $$ = std::nullopt; }
| add_op expression {
    const auto last = $2.expression.token_range.last;
    $$ = CstAddressOffset{$1, driver.numeric(std::move($2)), {$1, last}};
  };
address_base:
  identifier { $$ = CstIdentifier{$1}; }
| numeric_product { $$ = driver.numericBase(std::move($1)); };
address_content: address_base address_offset {
  const auto first = std::visit([](const auto& value) {
    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, CstIdentifier>) return value.token;
    else return value.token_range.first;
  }, $1);
  const auto last = $2 ? $2->token_range.last : std::visit([](const auto& value) {
    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, CstIdentifier>) return value.token + 1;
    else return value.token_range.last;
  }, $1);
  $$ = {std::move($1), std::move($2), {first,last}};
};
unified: %empty { $$ = std::nullopt; } | UNIFIED { $$ = $1; };
bracket_operand:
  LBRACKET address_content RBRACKET unified {
    const auto last = $4 ? *$4 + 1 : $3 + 1;
    $$ = CstAddress{$1, std::move($2.base), std::move($2.offset), $3, $4, {$1,last}};
  }
| LBRACKET address_content COMMA vector RBRACKET {
    $$ = CstTensorOperand{$1,
        CstAddress{std::nullopt,std::move($2.base),std::move($2.offset),
                   std::nullopt,std::nullopt,{$1+1,$3}},
        $3,std::move($4),$5,{$1,$5+1}};
  }
| LBRACKET bracket_heads RBRACKET {
    $$ = CstFabricHandle{std::move($2.heads),std::move($2.commas),$1,$3,{$1,$3+1}};
  }
| LBRACKET bracket_heads COMMA vector RBRACKET {
    $2.commas.push_back($3);
    $$ = CstCompoundBracket{std::move($2.heads),std::move($2.commas),
                            std::move($4),$1,$5,{$1,$5+1}};
  };
bracket_heads:
  address_content COMMA bracket_component {
    if ($1.offset) driver.fail($2,"fabric handle endpoint cannot have an offset");
    $$.heads.push_back(std::move($1.base)); $$.heads.push_back(std::move($3));
    $$.commas.push_back($2);
  }
| bracket_heads COMMA bracket_component {
    $1.heads.push_back(std::move($3)); $1.commas.push_back($2); $$ = std::move($1);
  };
bracket_component:
  identifier { $$ = CstIdentifier{$1}; }
| numeric_expression { $$ = driver.numericBase(std::move($1)); };
vector:
  LBRACE vector_elements RBRACE {
    std::vector<TokenId> commas;
    for (TokenId i=$1+1; i<$3; ++i) {
      if (driver.token(i).kind == TokenKind::Comma) commas.push_back(i);
    }
    $$ = {$1,std::move($2),std::move(commas),$3,{$1,$3+1}};
  };
vector_elements:
  vector_element { $$.push_back(std::move($1)); }
| vector_elements COMMA vector_element { $1.push_back(std::move($3)); $$=std::move($1); };
vector_element:
  identifier { $$ = CstIdentifier{$1}; }
| identifier dot_identifier { $$ = CstVectorMember{{$1},$2,{$1,$2+1}}; }
| numeric_expression {
    $$ = std::visit([](auto&& value)->CstVectorElement{return std::move(value);},
                   driver.numeric(std::move($1)));
  };
input_parameter:
  identifier { $$ = CstIdentifier{$1}; }
| numeric_expression {
    $$ = std::visit([](auto&& value)->CstCallParameter{return std::move(value);},
                   driver.numeric(std::move($1)));
  };
input_parameters:
  %empty { $$ = {}; }
| input_parameters_nonempty { $$=std::move($1); };
input_parameters_nonempty:
  input_parameter { $$.push_back(std::move($1)); }
| input_parameters_nonempty COMMA input_parameter { $1.push_back(std::move($3)); $$=std::move($1); };
input_list: LP input_parameters RP {
  std::vector<TokenId> commas;
  for (TokenId i=$1+1;i<$3;++i)
    if(driver.token(i).kind==TokenKind::Comma) commas.push_back(i);
  $$ = {CstCallParameterListKind::Input,$1,std::move($2),std::move(commas),$3,{$1,$3+1}};
};
return_list: LP identifier RP {
  std::vector<CstCallParameter> parameters;
  parameters.push_back(CstIdentifier{$2});
  $$ = {CstCallParameterListKind::Return,$1,std::move(parameters),{},$3,{$1,$3+1}};
};
call_operands:
  identifier { $$.push_back({CstCallTarget{{$1},{$1,$1+1}},std::nullopt}); }
| identifier COMMA identifier {
    $$.push_back({CstCallTarget{{$1},{$1,$1+1}},$2});
    $$.push_back({CstCallTargetSet{{$3},{$3,$3+1}},std::nullopt});
  }
| identifier COMMA input_list {
    $$.push_back({CstCallTarget{{$1},{$1,$1+1}},$2});
    $$.push_back({std::move($3),std::nullopt});
  }
| identifier COMMA input_list COMMA identifier {
    $$.push_back({CstCallTarget{{$1},{$1,$1+1}},$2});
    $$.push_back({std::move($3),$4});
    $$.push_back({CstCallTargetSet{{$5},{$5,$5+1}},std::nullopt});
  }
| return_list COMMA identifier COMMA input_list {
    $$.push_back({std::move($1),$2});
    $$.push_back({CstCallTarget{{$3},{$3,$3+1}},$4});
    $$.push_back({std::move($5),std::nullopt});
  }
| return_list COMMA identifier COMMA input_list COMMA identifier {
    $$.push_back({std::move($1),$2});
    $$.push_back({CstCallTarget{{$3},{$3,$3+1}},$4});
    $$.push_back({std::move($5),$6});
    $$.push_back({CstCallTargetSet{{$7},{$7,$7+1}},std::nullopt});
  };
module: module_items {
  const auto first=std::get<CstFunction>($1.front()).token_range.first;
  const auto last=std::get<CstFunction>($1.back()).token_range.last;
  $$={std::move($1),{first,last}};
};
module_items:
  function { $$.push_back(std::move($1)); }
| module_items function { $1.push_back(std::move($2)); $$=std::move($1); };
function: ENTRY identifier LP RP LBRACE body RBRACE {
  CstFunction function;
  function.directive=$1; function.name=$2;
  function.parameters=CstFunctionParameterList{$3,{},{},$4,{$3,$4+1}};
  function.header_tokens={$1,$2,$3,$4};
  function.left_brace=$5; function.body=std::move($6); function.right_brace=$7;
  function.token_range={$1,$7+1}; $$=std::move(function);
};
body:
  %empty { $$.clear(); }
| body instruction { $1.push_back(std::move($2)); $$=std::move($1); }
| body identifier COLON {
    $1.push_back(CstLabel{$2,$3,{$2,$3+1}}); $$=std::move($1);
  }
| body error SEMI {
    auto nodes=driver.recoverStatement($3);
    for(auto& node:nodes) $1.push_back(std::move(node));
    $$=std::move($1); yyerrok;
  };
expression:
  logical_or { $$=std::move($1); }
| logical_or QUESTION <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth,TokenKind::Colon,"':' in conditional expression"); } expression COLON {
    driver.expected=$3->previous_expected; driver.expected_name=$3->previous_name;
  } expression { $$=driver.conditional(std::move($1),$2,std::move($4),$5,std::move($7),std::move($3)); };
logical_or:
  logical_and { $$=std::move($1); }
| logical_or OR <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } logical_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
logical_and:
  bit_or { $$=std::move($1); }
| logical_and AND <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_or { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
bit_or:
  bit_xor { $$=std::move($1); }
| bit_or PIPE <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_xor { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
bit_xor:
  bit_and { $$=std::move($1); }
| bit_xor CARET <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
bit_and:
  equality { $$=std::move($1); }
| bit_and AMP <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } equality { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
equality:
  relation { $$=std::move($1); }
| equality equality_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } relation { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
relation:
  shift { $$=std::move($1); }
| relation relation_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } shift { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
shift:
  sum { $$=std::move($1); }
| shift shift_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } sum { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
sum:
  product { $$=std::move($1); }
| sum add_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } product { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
product:
  unary { $$=std::move($1); }
| product mul_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } unary { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_expression:
  numeric_logical_or { $$=std::move($1); }
| numeric_logical_or QUESTION <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth,TokenKind::Colon,"':' in conditional expression"); } expression COLON {
    driver.expected=$3->previous_expected; driver.expected_name=$3->previous_name;
  } expression { $$=driver.conditional(std::move($1),$2,std::move($4),$5,std::move($7),std::move($3)); };
numeric_logical_or:
  numeric_logical_and { $$=std::move($1); }
| numeric_logical_or OR <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } logical_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_logical_and:
  numeric_bit_or { $$=std::move($1); }
| numeric_logical_and AND <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_or { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_bit_or:
  numeric_bit_xor { $$=std::move($1); }
| numeric_bit_or PIPE <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_xor { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_bit_xor:
  numeric_bit_and { $$=std::move($1); }
| numeric_bit_xor CARET <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_bit_and:
  numeric_equality { $$=std::move($1); }
| numeric_bit_and AMP <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } equality { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_equality:
  numeric_relation { $$=std::move($1); }
| numeric_equality equality_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } relation { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_relation:
  numeric_shift { $$=std::move($1); }
| numeric_relation relation_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } shift { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_shift:
  numeric_sum { $$=std::move($1); }
| numeric_shift shift_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } sum { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_sum:
  numeric_product { $$=std::move($1); }
| numeric_sum add_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } product { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
numeric_product:
  numeric_unary { $$=std::move($1); }
| numeric_product mul_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } unary { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_expression:
  operand_logical_or { $$=std::move($1); }
| operand_logical_or QUESTION <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth,TokenKind::Colon,"':' in conditional expression"); } expression COLON {
    driver.expected=$3->previous_expected; driver.expected_name=$3->previous_name;
  } expression { $$=driver.conditional(std::move($1),$2,std::move($4),$5,std::move($7),std::move($3)); };
operand_logical_or:
  operand_logical_and { $$=std::move($1); }
| operand_logical_or OR <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } logical_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_logical_and:
  operand_bit_or { $$=std::move($1); }
| operand_logical_and AND <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_or { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_bit_or:
  operand_bit_xor { $$=std::move($1); }
| operand_bit_or PIPE <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_xor { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_bit_xor:
  operand_bit_and { $$=std::move($1); }
| operand_bit_xor CARET <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } bit_and { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_bit_and:
  operand_equality { $$=std::move($1); }
| operand_bit_and AMP <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } equality { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_equality:
  operand_relation { $$=std::move($1); }
| operand_equality equality_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } relation { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_relation:
  operand_shift { $$=std::move($1); }
| operand_relation relation_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } shift { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_shift:
  operand_sum { $$=std::move($1); }
| operand_shift shift_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } sum { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_sum:
  operand_product { $$=std::move($1); }
| operand_sum add_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } product { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };
operand_product:
  operand_unary { $$=std::move($1); }
| operand_product mul_op <PtxParserDriver::Guard>{ $$=driver.enter($2,$1.depth); } unary { $$=driver.binary(std::move($1),$2,std::move($4),std::move($3)); };

unary:
  postfix { $$=std::move($1); }
| unary_op <PtxParserDriver::Guard>{ $$=driver.enter($1); } unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| cast_expression { $$=std::move($1); };
numeric_unary:
  numeric_postfix { $$=std::move($1); }
| unary_op <PtxParserDriver::Guard>{ $$=driver.enter($1); } unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| cast_expression { $$=std::move($1); };
operand_unary:
  numeric_postfix { $$=std::move($1); }
| PLUS <PtxParserDriver::Guard>{ $$=driver.enter($1); } unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| MINUS <PtxParserDriver::Guard>{ $$=driver.enter($1); } numeric_unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| NOT <PtxParserDriver::Guard>{ $$=driver.enter($1); } numeric_unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| TILDE <PtxParserDriver::Guard>{ $$=driver.enter($1); } unary
  { $$=driver.unary($1,std::move($3),std::move($2)); }
| cast_expression { $$=std::move($1); };
cast_expression: LP CAST cast_close_guard RP cast_guard unary {
  $$=driver.cast($1,$2,$4,std::move($6),std::move($5));
};
postfix:
  atom { $$=std::move($1); }
| postfix LP call_guard expression RP {
  $$=driver.call(std::move($1),$2,std::move($4),$5,std::move($3));
};
numeric_postfix:
  numeric_atom { $$=std::move($1); }
| numeric_postfix LP call_guard expression RP {
  $$=driver.call(std::move($1),$2,std::move($4),$5,std::move($3));
};
atom:
  identifier { $$=driver.leaf($1,true); }
| numeric_atom { $$=std::move($1); };
numeric_atom:
  LITERAL { $$=driver.leaf($1,false); }
| LP paren_guard expression RP {
  $$=driver.parenthesized($1,std::move($3),$4,std::move($2));
};
paren_guard: %empty {
  $$=driver.enter($<syntax_cst::TokenId>0,0,TokenKind::RParen,"')'");
};
call_guard: %empty {
  $$=driver.enter($<syntax_cst::TokenId>0,$<ParsedExpression>-1.depth,
                  TokenKind::RParen,"')' after initializer operator");
};
cast_close_guard: %empty {
  driver.markShifted($<syntax_cst::TokenId>0);
  $$=driver.expectClosing(TokenKind::RParen,"')' after constant cast");
};
cast_guard: %empty {
  driver.markShifted($<syntax_cst::TokenId>0);
  $<PtxParserDriver::Guard>-1.reset();
  $$=driver.enter($<syntax_cst::TokenId>-3);
};
unary_op: PLUS | MINUS | NOT | TILDE;
add_op: PLUS | MINUS;
mul_op: STAR | SLASH | PERCENT;
shift_op: SHL | SHR;
relation_op: LT | LE | GT | GE;
equality_op: EQ | NE;
%%

/** Retain source-located parser diagnostics while generated error rules synchronize. */
void ptx_frontend::detail::GeneratedPtxParser::report_syntax_error(const context& ctx) const {
  symbol_kind_type expected_tokens[128];
  const auto count=ctx.expected_tokens(expected_tokens,128);
  bool operand=false, semi=false, rp=false, colon=false;
  bool at=false, end=false, rb=false, comma=false, bracket=false, brace=false;
  bool lp=false, right_brace=false;
  for(int i=0;i<count;++i) {
    operand|=expected_tokens[i]==symbol_kind::S_LITERAL;
    semi|=expected_tokens[i]==symbol_kind::S_SEMI;
    rp|=expected_tokens[i]==symbol_kind::S_RP;
    colon|=expected_tokens[i]==symbol_kind::S_COLON;
    at|=expected_tokens[i]==symbol_kind::S_AT;
    end|=expected_tokens[i]==symbol_kind::S_YYEOF;
    rb|=expected_tokens[i]==symbol_kind::S_RBRACKET;
    comma|=expected_tokens[i]==symbol_kind::S_COMMA;
    bracket|=expected_tokens[i]==symbol_kind::S_LBRACKET;
    brace|=expected_tokens[i]==symbol_kind::S_LBRACE;
    lp|=expected_tokens[i]==symbol_kind::S_LP;
    right_brace|=expected_tokens[i]==symbol_kind::S_RBRACE;
  }
  CstParseDiagnostic diagnostic{driver.token(driver.lookahead()).range,"expected operand",std::nullopt};
  const auto current=driver.lookahead();
  // LAC may reject lookahead before an empty context action runs. The live
  // generated stack still identifies the owning production without reparsing
  // source tokens or consuming the offending lookahead.
  std::string_view spelling;
  int head=-1;
  for(int i=0;i<yystack_.size();++i) {
    const auto kind=yystack_[i].kind();
    if(kind==symbol_kind::S_call_head || kind==symbol_kind::S_CALL) spelling="call";
    else if(kind==symbol_kind::S_branch_head || kind==symbol_kind::S_BRA) spelling="bra";
    else if(kind==symbol_kind::S_indexed_head || kind==symbol_kind::S_BRX) spelling="brx";
    else if(kind!=symbol_kind::S_ordinary_head) continue;
    head=i; break;
  }
  bool returns=false, inputs=false, call_paren=false, call_input=false, seen_comma=false;
  for(int i=head-1;i>=0;--i) {
    const auto kind=yystack_[i].kind();
    returns|=kind==symbol_kind::S_return_list;
    inputs|=kind==symbol_kind::S_input_list;
    seen_comma|=kind==symbol_kind::S_COMMA;
    if(kind==symbol_kind::S_LP) { call_paren=true; call_input=seen_comma; }
  }
  const auto previous=current>0 ? driver.token(current-1).kind : TokenKind::Eof;
  const auto offending=driver.token(current).kind;
  if(end) diagnostic.message="expected end of input";
  else if(at) {
    diagnostic.message="expected instruction opcode";
    diagnostic.expected_kind=TokenKind::Ident;
  } else if(!driver.expected && spelling=="call") {
    if(call_paren && !call_input) {
      if(rp) {
        diagnostic.message=offending==TokenKind::Comma ?
            "a call return parameter list must contain exactly one name" :
            "expected ')' in call parameter list";
        if(offending!=TokenKind::Comma) diagnostic.expected_kind=TokenKind::RParen;
      } else {
        diagnostic.message="expected call return parameter";
        diagnostic.expected_kind=TokenKind::Ident;
      }
    } else if(call_paren && call_input) {
      if(operand && (previous==TokenKind::LParen || previous==TokenKind::Comma))
        diagnostic.message=previous==TokenKind::Comma && offending==TokenKind::RParen ?
            "call argument list cannot end with a trailing comma" :
            "expected identifier or immediate call argument";
      else if(rp) {
        diagnostic.message="expected ')' in call parameter list";
        diagnostic.expected_kind=TokenKind::RParen;
      } else diagnostic.message="expected constant expression";
    } else if(comma && !semi) {
      if(yystack_[0].kind()==symbol_kind::S_return_list) {
        diagnostic.message="expected ',' after call return parameter";
        diagnostic.expected_kind=TokenKind::Comma;
      } else diagnostic.message="a call with a return parameter requires an input parameter list";
    } else if(returns && lp && !semi) {
      diagnostic.message="expected call input parameter list after the target";
    } else if(!semi) {
      diagnostic.message=inputs || (!lp && !returns) ?
          "expected call target set or prototype" : "expected call target";
      diagnostic.expected_kind=TokenKind::Ident;
    } else {
      diagnostic.message="expected ';'"; diagnostic.expected_kind=TokenKind::Semicolon;
    }
  } else if(!operand && !semi && !rp && !rb && !colon && !comma && !brace && !bracket && !right_brace) {
    diagnostic.expected_kind=TokenKind::Ident;
    if(current>0 && (driver.token(current-1).kind==TokenKind::At ||
        driver.token(current-1).kind==TokenKind::Exclamation))
      diagnostic.message="expected predicate identifier";
    else if(spelling=="bra") diagnostic.message="expected branch label target";
    else if(spelling=="brx") diagnostic.message=previous==TokenKind::Comma ?
        "expected brx.idx branch target list" : "expected brx.idx index register";
    else if(previous==TokenKind::Pipe) diagnostic.message="expected predicate after '|'";
    else diagnostic.message="expected instruction opcode";
  } else if(!operand && colon) {
    diagnostic.message="expected ':' in conditional expression";
    diagnostic.expected_kind=TokenKind::Colon;
  } else if(!operand && rp && driver.expected==TokenKind::RParen) {
    diagnostic.message="expected "+driver.expected_name;
    diagnostic.expected_kind=TokenKind::RParen;
  } else if(rb && !operand) {
    diagnostic.message="expected ']'"; diagnostic.expected_kind=TokenKind::RBracket;
  } else if(comma && !operand && !semi && spelling=="brx") {
    diagnostic.message="expected ',' after brx.idx index register";
    diagnostic.expected_kind=TokenKind::Comma;
  } else if(bracket && brace) {
    diagnostic.message="expected operand"; diagnostic.expected_kind=TokenKind::Ident;
  } else if(operand && !bracket && current>0 &&
      (driver.token(current-1).kind==TokenKind::LBrace ||
       driver.token(current-1).kind==TokenKind::Comma) && !driver.expected) {
    if(previous==TokenKind::LBrace && offending==TokenKind::RBrace)
      diagnostic.message="vector operand cannot be empty";
    else if(brace && previous==TokenKind::Comma) {
      diagnostic.message="expected fabric handle component";
      diagnostic.expected_kind=TokenKind::Ident;
    } else diagnostic.message="expected identifier or immediate vector element";
  } else if(right_brace && !operand) {
    diagnostic.message="expected '}'"; diagnostic.expected_kind=TokenKind::RBrace;
  } else if(semi) {
    if(offending==TokenKind::Comma && spelling=="bra")
      diagnostic.message="direct branch accepts exactly one label target";
    else if(offending==TokenKind::Comma && spelling=="brx")
      diagnostic.message="brx.idx accepts exactly an index and target list";
    else { diagnostic.message="expected ';'"; diagnostic.expected_kind=TokenKind::Semicolon; }
  } else if(operand) diagnostic.message="expected constant expression";
  driver.diagnose(std::move(diagnostic));
}

/** Semantic-action failures carry their exact diagnostic through this callback. */
void ptx_frontend::detail::GeneratedPtxParser::error(const std::string& message) {
  driver.generatedError(message);
}

