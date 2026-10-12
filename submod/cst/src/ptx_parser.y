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
#define PTX_FETCH_LOOKAHEAD() do { if(yyla.empty()) { auto fresh=yylex(driver); yyla.move(fresh); } } while(false)
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
%token <syntax_cst::TokenId> ATTRIBUTE ALIAS LANGUAGE BLOCKS_CLUSTERS ABI_PRESERVE ABI_CONTROL VECTOR_TYPE MISSING_RBRACE
%precedence EMPTY_SUFFIX
%precedence SEMI COMMA
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
%type <AddressContent> address_content bracket_content
%type <BracketHeads> bracket_heads
%type <std::optional<syntax_cst::CstAddressOffset>> address_offset
%type <std::optional<syntax_cst::TokenId>> unified selector
%type <syntax_cst::CstNamedArrayIndex> named_index
%type <syntax_cst::CstModule> module
%type <std::vector<syntax_cst::CstModuleItem>> module_items
%type <syntax_cst::CstFunction> function
%type <std::vector<syntax_cst::CstFunctionBodyItem>> body
%type <syntax_cst::CstModuleItem> module_item
%type <syntax_cst::CstFunctionBodyItem> body_item
%type <syntax_cst::CstVariableDeclaration> declaration declaration_head declarators
%type <syntax_cst::CstVariableDeclarator> declarator declarator_head
%type <syntax_cst::TokenId> state_space qualifier base_type integer decimal
%type <std::vector<syntax_cst::TokenId>> qualifiers string_values integer_values resource_values
%type <OptionalTokenPair> alignment parameterized
%type <std::optional<syntax_cst::TokenId>> optional_semi vector_type
%type <std::vector<syntax_cst::CstArrayDimension>> dimensions
%type <syntax_cst::CstArrayDimension> dimension
%type <std::optional<syntax_cst::CstConstantExpression>> optional_expression
%type <ParsedInitializer> initializer
%type <InitializerElements> initializer_elements initializer_nonempty
%type <PtxParserDriver::Guard> initializer_open
%type <syntax_cst::CstAttribute> attribute
%type <syntax_cst::CstAttributeList> attributes attribute_list
%type <std::optional<syntax_cst::CstAttributeList>> optional_attributes
%type <syntax_cst::CstFunctionParameter> parameter parameter_head parameter_base
%type <syntax_cst::TokenId> parameter_space pointer_space invalid_parameter_space version_value section_name
%type <syntax_cst::TokenId> address_size file_index file_timestamp file_size
%type <syntax_cst::CstFunctionParameterList> parameter_list parameters parameters_nonempty
%type <std::optional<syntax_cst::CstFunctionParameterList>> optional_parameters
%type <syntax_cst::CstFunction> entry_head func_head function_header function_open
%type <std::optional<syntax_cst::TokenId>> noreturn blocks_clusters
%type <std::optional<syntax_cst::CstCallPrototypeAbiSuffix>> abi_preserve abi_control
%type <std::optional<syntax_cst::CstLanguageDirective>> optional_language
%type <syntax_cst::CstLanguageDirective> language
%type <syntax_cst::CstPragma> pragma
%type <syntax_cst::CstKernelResourceDirective> resource
%type <syntax_cst::CstFunction> entry_clauses
%type <syntax_cst::CstModuleDirective> module_directive
%type <syntax_cst::CstAliasDirective> alias
%type <BodyClose> body_close
%type <PtxParserDriver::BodyGuard> body_guard block_open
%type <syntax_cst::CstBlock> block
%type <syntax_cst::CstLocDirective> location
%type <std::optional<syntax_cst::CstLocInlineContext>> inline_context
%type <std::optional<syntax_cst::TokenId>> inline_offset
%type <syntax_cst::TokenId> prototype_sink section_atom raw_atom
%type <syntax_cst::TokenId> rejected_module_identifier
%type <syntax_cst::TokenId> dimension_open
%type <syntax_cst::TokenId> rejected_module_token rejected_body_token
%type <syntax_cst::TokenId> attribute_name source_token function_keyword loc_label inline_keyword
%type <syntax_cst::TokenId> loc_file loc_line loc_column inline_file inline_line inline_column loc_offset
%type <syntax_cst::CstCallPrototype> call_prototype
%type <std::optional<syntax_cst::CstFunctionParameterList>> prototype_returns
%type <syntax_cst::CstCallTargets> call_targets
%type <syntax_cst::CstBranchTargets> branch_targets branch_target_values
%type <syntax_cst::CstBranchTargetEntry> branch_target
%type <std::vector<syntax_cst::TokenId>> target_values raw_payload section_payload raw_group
%type <syntax_cst::CstSectionDirective> section
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
dot_identifier: DOT_IDENT | CAST | UNIFIED | ATTRIBUTE | ALIAS | LANGUAGE | BLOCKS_CLUSTERS | ABI_PRESERVE | ABI_CONTROL | VECTOR_TYPE;
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
| LBRACKET bracket_content vector RBRACKET {
    $$ = CstTensorOperand{$1,
        CstAddress{std::nullopt,std::move($2.base),std::move($2.offset),
                   std::nullopt,std::nullopt,{$1+1,$2.token_range.last}},
        $2.token_range.last,std::move($3),$4,{$1,$4+1}};
  }
| LBRACKET bracket_heads RBRACKET {
    $$ = CstFabricHandle{std::move($2.heads),std::move($2.commas),$1,$3,{$1,$3+1}};
  }
| LBRACKET bracket_heads COMMA vector RBRACKET {
    $2.commas.push_back($3);
    $$ = CstCompoundBracket{std::move($2.heads),std::move($2.commas),
                            std::move($4),$1,$5,{$1,$5+1}};
  };
bracket_content: address_content COMMA {
  // A coordinate pack permits the endpoint offset; scalar fabric components
  // reject it before parsing any component, even when that component is invalid.
  driver.markShifted($2); PTX_FETCH_LOOKAHEAD();
  if($1.offset && driver.token(driver.lookahead()).kind!=TokenKind::LBrace)
    driver.fail($2,"fabric handle endpoint cannot have an offset");
  $$=std::move($1);
};
bracket_heads:
  bracket_content bracket_component {
    $$.heads.push_back(std::move($1.base)); $$.heads.push_back(std::move($2));
    $$.commas.push_back($1.token_range.last);
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
  if($1.empty()) driver.fail(driver.lookahead(),"expected module item");
  TokenId last=driver.entry_first;
  for(const auto& item:$1) std::visit([&](const auto& node) {
    using T=std::decay_t<decltype(node)>;
    if constexpr(std::is_same_v<T,CstRecoveryNode>) { if(node.token_range) last=std::max(last,node.token_range->last); }
    else last=std::max(last,node.token_range.last);
  },item);
  $$={std::move($1),{driver.entry_first,std::max(last,driver.entry_first+1)}};
};
module_items:
  %empty { driver.itemBoundary(); }
| module_items module_item {
    $1.push_back(std::move($2)); $$=std::move($1); driver.itemBoundary();
  }
| module_items error {
    auto nodes=driver.synchronize(false);
    for(auto& node:nodes) $1.push_back(std::move(node));
    $$=std::move($1); yyclearin; yyerrok; driver.itemBoundary();
  };
module_item:
  function { $$=std::move($1); }
| declaration { $$=std::move($1); }
| module_directive { $$=std::move($1); }
| pragma { $$=std::move($1); }
| alias { $$=std::move($1); }
| section { $$=std::move($1); }
| reject_module rejected_module_token { $$=CstRecoveryNode{}; }
| qualifiers rejected_module_identifier COLON {
  driver.markShifted($3); PTX_FETCH_LOOKAHEAD();
  const auto kind=driver.token(driver.lookahead()).kind;
  std::string message="expected module directive, variable declaration, or function";
  if(kind==TokenKind::DotCallPrototype) message="'.callprototype' is only valid inside a function body";
  if(kind==TokenKind::DotCallTargets) message="'.calltargets' is only valid inside a function body";
  if(kind==TokenKind::DotBranchTargets) message="'.branchtargets' is only valid inside a function body";
  driver.fail(driver.lookahead(),std::move(message));
};
rejected_module_identifier: identifier {
  $$=$1; driver.markShifted($1); PTX_FETCH_LOOKAHEAD();
  if(driver.token(driver.lookahead()).kind!=TokenKind::Colon)
    driver.fail(driver.lookahead(),"expected module directive, variable declaration, or function");
};
reject_module: %empty {
  driver.rejectItem(false);
};
rejected_module_token:
  LITERAL | DOT_IDENT | CAST | UNIFIED | ATTRIBUTE | LANGUAGE | BLOCKS_CLUSTERS | ABI_PRESERVE | ABI_CONTROL | VECTOR_TYPE
| LP | RP | QUESTION | COLON | SEMI | COMMA | AT | LBRACKET | RBRACKET | LBRACE | RBRACE
| PLUS | MINUS | NOT | TILDE | STAR | SLASH | PERCENT | SHL | SHR | LT | LE | GT | GE | EQ | NE | AMP | CARET | PIPE | AND | OR
| DOT_LOC | DOT_CALL_PROTOTYPE | DOT_CALL_TARGETS | DOT_BRANCH_TARGETS | DOT_MAXNREG | DOT_MAXNTID | DOT_REQNTID
| DOT_MINNCTAPERSM | DOT_REQNCTAPERCLUSTER | DOT_EXPLICITCLUSTER | DOT_MAXCLUSTERRANK | DOT_NORETURN | DOT_ALIGN | DOT_PTR
| STRING | DOT | EQ_ASSIGN | INVALID;
qualifier: DOT_EXTERN | DOT_VISIBLE | DOT_WEAK;
qualifiers:
  %empty { $$={}; }
| qualifiers qualifier { $1.push_back($2); $$=std::move($1); };
state_space: DOT_REG | DOT_PARAM | DOT_LOCAL | DOT_SHARED | DOT_GLOBAL | DOT_CONST | DOT_TEX;
base_type: DOT_IDENT | CAST | UNIFIED | ALIAS | LANGUAGE | BLOCKS_CLUSTERS | ABI_PRESERVE | ABI_CONTROL;
decimal: LITERAL { $$=driver.literal($1,"decimal integer"); driver.markShifted($1); };
integer: LITERAL { $$=driver.literal($1,"integer literal",true,false); driver.markShifted($1); };
alignment:
  %empty { $$=std::nullopt; }
| DOT_ALIGN decimal { $$=std::pair{$1,$2}; };
vector_type: %empty { $$=std::nullopt; } | VECTOR_TYPE { $$=$1; };
optional_attributes:
  %empty { $$=std::nullopt; }
| attribute_list { $$=std::move($1); };
attribute_list: ATTRIBUTE LP attributes RP {
  $$=std::move($3); $$.directive=$1; $$.left_paren=$2; $$.right_paren=$4; $$.token_range={$1,$4+1};
  driver.markShifted($4);
};
attributes:
  attribute { $$.attributes.push_back(std::move($1)); }
| attributes COMMA attribute { $1.commas.push_back($2); $1.attributes.push_back(std::move($3)); $$=std::move($1); };
attribute:
  attribute_name {
    if(driver.token($1).text!=".managed") driver.fail($1,driver.token($1).text==".unified"?"invalid .attribute member arity":"unsupported .attribute member");
    $$={$1,{},{},{$1,$1+1}};
  }
| attribute_name LP integer COMMA integer RP {
    driver.markShifted($6);
    if(driver.token($1).text!=".unified") driver.fail($1,driver.token($1).text==".managed"?"invalid .attribute member arity":"unsupported .attribute member");
    $$={$1,{$3,$5},{$4},{$1,$6+1}};
  };
attribute_name: dot_identifier {
  driver.markShifted($1);
  if(driver.token($1).text!=".managed" && driver.token($1).text!=".unified")
    driver.fail($1,"unsupported .attribute member");
  $$=$1;
};
declaration_head: qualifiers state_space optional_attributes alignment vector_type base_type {
  CstVariableDeclaration value;
  value.qualifiers=std::move($1); value.state_space=$2; value.attributes=std::move($3);
  if($4) { value.align_directive=$4->first; value.alignment=$4->second; }
  value.vector_type=$5; value.type=$6;
  value.token_range.first=value.qualifiers.empty()?$2:value.qualifiers.front();
  driver.beginStatement(value.token_range.first);
  driver.declaration_space=driver.token($2).kind;
  driver.declaration_external=std::ranges::any_of(value.qualifiers,[&](auto id){return driver.token(id).kind==TokenKind::DotExtern;});
  driver.markShifted($6);
  $$=std::move(value);
};
declaration: declaration_head declarators SEMI {
  $$=std::move($1); $$.declarators=std::move($2.declarators); $$.commas=std::move($2.commas);
  $$.semicolon=$3; $$.token_range.last=$3+1; driver.markShifted($3);
};
declarators:
  declarator { $$.declarators.push_back(std::move($1)); }
| declarators COMMA declarator { $1.commas.push_back($2); $1.declarators.push_back(std::move($3)); $$=std::move($1); };
parameterized:
  %empty { $$=std::nullopt; driver.parameterized_declarator=false; }
| LT decimal GT { $$=std::pair{$1,$2}; driver.parameterized_declarator=true; };
optional_expression:
  %empty { $$=std::nullopt; }
| expression { $$=std::move($1.expression); };
dimension_open: LBRACKET {
  driver.markShifted($1);
  if(driver.parameterized_declarator) driver.fail($1,"parameterized variable names cannot declare arrays");
  $$=$1;
};
dimension: dimension_open optional_expression RBRACKET {
  $$={$1,std::move($2),$3,{$1,$3+1}}; driver.markShifted($3);
};
dimensions:
  %empty { $$.clear(); }
| dimensions dimension { $1.push_back(std::move($2)); $$=std::move($1); };
declarator_head: identifier parameterized dimensions {
  $$={}; $$.name=$1;
  if($2) { $$.left_angle=$2->first; $$.parameterized_count=$2->second; $$.right_angle=$2->second+1; }
  $$.array_dimensions=std::move($3);
  const auto last=!$$.array_dimensions.empty()?$$.array_dimensions.back().token_range.last:
      $$.right_angle?*$$.right_angle+1:$1+1;
  $$.token_range={$1,last}; driver.markShifted(last-1);
};
declarator:
  declarator_head { $$=std::move($1); }
| declarator_head EQ_ASSIGN { driver.markShifted($2); driver.initializerAllowed($2); } initializer {
  $$=std::move($1); $$.equals=$2; $$.token_range.last=$4.initializer.token_range.last; $$.initializer=std::move($4.initializer);
};
initializer_open: LBRACE {
  if(driver.remaining_depth==0) driver.fail($1,"initializer tree depth limit exceeded");
  $$=driver.expectClosing(TokenKind::RBrace,"'}' in initializer");
  $$->anchor=$1; driver.initializer_anchor=$1; driver.initializer_child_pending=true;
  --driver.remaining_depth; driver.markShifted($1);
};
initializer:
  expression { const auto range=$1.expression.token_range; $$={CstInitializer{std::move($1.expression),range},$1.depth}; }
| dot_identifier EQ_ASSIGN <PtxParserDriver::Guard>{ $$=driver.expectClosing(TokenKind::Eof,""); --driver.remaining_depth; } expression {
  $3.reset(); const CstTokenRange range{$1,$4.expression.token_range.last};
  $$={CstInitializer{CstNamedInitializer{$1,$2,std::move($4.expression),range},range},$4.depth+1};
}
| identifier EQ_ASSIGN <PtxParserDriver::Guard>{ $$=driver.expectClosing(TokenKind::Eof,""); --driver.remaining_depth; } expression {
  $3.reset(); const CstTokenRange range{$1,$4.expression.token_range.last};
  $$={CstInitializer{CstNamedInitializer{$1,$2,std::move($4.expression),range},range},$4.depth+1};
}
| initializer_open initializer_elements RBRACE {
  const auto first=$1->anchor;
  $1.reset(); const CstTokenRange range{first,$3+1};
  $$={CstInitializer{CstInitializerList{first,std::move($2.elements),std::move($2.commas),$3,range},range},$2.depth+1};
  driver.markShifted($3);
};
initializer_elements:
  %empty { $$={}; }
| initializer_nonempty { $$=std::move($1); }
| initializer_nonempty COMMA { $1.commas.push_back($2); $$=std::move($1); };
initializer_nonempty:
  initializer { $$.depth=$1.depth; $$.elements.push_back(std::move($1.initializer)); }
| initializer_nonempty COMMA initializer { $1.commas.push_back($2); $1.depth=std::max($1.depth,$3.depth); $1.elements.push_back(std::move($3.initializer)); $$=std::move($1); };
parameter_space: DOT_REG { driver.markShifted($1); $$=$1; }
| DOT_PARAM { driver.markShifted($1); $$=$1; };
// Invalid state candidates reduce directly as a failed parameter, before a
// following type can trigger LAC. RP remains the empty-list terminator; commas
// retain their separate leading/separator/trailing productions below.
invalid_parameter_space:
  LITERAL | identifier | dot_identifier | LP | QUESTION | COLON | SEMI | AT | LBRACKET | RBRACKET
| PLUS | MINUS | NOT | TILDE | STAR | SLASH | PERCENT | SHL | SHR | LT | LE | GT | GE | EQ | NE | AMP | CARET | PIPE | AND | OR
| DOT_GLOBAL | DOT_CONST | DOT_SHARED | DOT_LOCAL | DOT_WEAK | ENTRY | DOT_VERSION | DOT_TARGET | DOT_ADDRESS_SIZE
| DOT_LOC | DOT_PRAGMA | DOT_FILE | DOT_SECTION | DOT_EXTERN | DOT_VISIBLE | DOT_FUNC | DOT_CALL_PROTOTYPE
| DOT_CALL_TARGETS | DOT_BRANCH_TARGETS | DOT_MAXNREG | DOT_MAXNTID | DOT_REQNTID | DOT_MINNCTAPERSM
| DOT_REQNCTAPERCLUSTER | DOT_EXPLICITCLUSTER | DOT_MAXCLUSTERRANK | DOT_NORETURN | DOT_ALIGN | DOT_PTR | DOT_TEX
| STRING | DOT | EQ_ASSIGN | INVALID | LBRACE | RBRACE;
pointer_space: DOT_CONST | DOT_GLOBAL | DOT_LOCAL | DOT_SHARED;
parameter_base: parameter_space alignment dot_identifier {
  driver.markShifted($3);
  if(driver.token($3).text==".v2" || driver.token($3).text==".v4")
    driver.fail($3,"vector function parameters are not supported");
  $$={}; $$.state_space=$1; if($2) { $$.align_directive=$2->first; $$.alignment=$2->second; } $$.type=$3;
};
parameter_head: parameter_base { $$=std::move($1); }
| parameter_base DOT_PTR { $1.pointer_directive=$2; $$=std::move($1); driver.markShifted($2); }
| parameter_base DOT_PTR pointer_space alignment {
  $1.pointer_directive=$2; $1.pointer_space=$3;
  if($4){$1.pointer_align_directive=$4->first;$1.pointer_alignment=$4->second;}
  $$=std::move($1);
}
| parameter_base DOT_PTR DOT_ALIGN decimal {
  $1.pointer_directive=$2; $1.pointer_align_directive=$3; $1.pointer_alignment=$4; $$=std::move($1);
};
parameter: invalid_parameter_space {
  driver.markShifted($1); driver.fail($1,"expected '.reg' or '.param' parameter");
}
| parameter_head identifier {
  $$=std::move($1); $$.name=$2; $$.token_range={$$.state_space,$2+1}; driver.markShifted($2);
}
| parameter_head identifier LBRACKET optional_expression RBRACKET {
  $$=std::move($1); $$.name=$2; $$.left_bracket=$3; $$.array_size=std::move($4); $$.right_bracket=$5;
  $$.token_range={$$.state_space,$5+1}; driver.markShifted($5);
};
parameters:
  %empty { $$={}; }
| parameters_nonempty { $$=std::move($1); }
| COMMA { driver.markShifted($1); driver.fail($1,"expected '.reg' or '.param' parameter"); };
parameters_nonempty:
  parameter { $$.parameters.push_back(std::move($1)); }
| parameters_nonempty COMMA parameter { $1.commas.push_back($2); $1.parameters.push_back(std::move($3)); $$=std::move($1); }
| parameters_nonempty COMMA RP {
  driver.markShifted($3); driver.fail($3,"expected '.reg' or '.param' parameter");
}
| parameters_nonempty COMMA COMMA {
  driver.markShifted($3); driver.fail($3,"expected '.reg' or '.param' parameter");
};
parameter_list: LP parameters RP {
  $$=std::move($2); $$.left_paren=$1; $$.right_paren=$3; $$.token_range={$1,$3+1}; driver.markShifted($3);
};
optional_parameters: %empty { $$=std::nullopt; } | parameter_list { $$=std::move($1); };
noreturn: %empty { $$=std::nullopt; } | DOT_NORETURN { $$=$1; };
abi_preserve: %empty { $$=std::nullopt; } | ABI_PRESERVE decimal { $$=CstCallPrototypeAbiSuffix{$1,$2,{$1,$2+1}}; };
abi_control: %empty { $$=std::nullopt; } | ABI_CONTROL decimal { $$=CstCallPrototypeAbiSuffix{$1,$2,{$1,$2+1}}; };
blocks_clusters: %empty { $$=std::nullopt; } | BLOCKS_CLUSTERS { $$=$1; };
optional_language: %empty { $$=std::nullopt; } | language { $$=std::move($1); };
entry_head: ENTRY identifier optional_parameters entry_clauses blocks_clusters optional_language {
  $$=std::move($4); $$.directive=$1; $$.name=$2; $$.parameters=std::move($3);
  $$.blocks_are_clusters=$5; $$.language=std::move($6); $$.token_range.first=$1;
};
func_head: DOT_FUNC optional_attributes optional_parameters identifier optional_parameters noreturn abi_preserve abi_control optional_language {
  $$={}; $$.directive=$1; $$.attributes=std::move($2); $$.return_parameters=std::move($3);
  $$.name=$4; $$.parameters=std::move($5); $$.noreturn_directive=$6;
  $$.abi_preserve=std::move($7); $$.abi_preserve_control=std::move($8); $$.language=std::move($9);
  $$.token_range.first=$1;
};
entry_clauses:
  %empty { $$={}; }
| entry_clauses pragma { $1.pragmas.push_back(std::move($2)); $$=std::move($1); }
| entry_clauses resource { $1.resources.push_back(std::move($2)); $$=std::move($1); };
function_header:
  qualifiers entry_head { $$=std::move($2); $$.qualifiers=std::move($1); if(!$$.qualifiers.empty()) $$.token_range.first=$$.qualifiers.front(); }
| qualifiers func_head { $$=std::move($2); $$.qualifiers=std::move($1); if(!$$.qualifiers.empty()) $$.token_range.first=$$.qualifiers.front(); };
function_open: function_header LBRACE {
  $$=std::move($1); $$.left_brace=$2; $$.header_tokens=driver.tokenSpan($$.token_range.first,$2);
  driver.markShifted($2);
};
body_guard: %empty { $$=driver.openBody(*$<CstFunction>0.left_brace,false); };
function:
  function_header SEMI {
    if(driver.token($1.directive).kind==TokenKind::DotEntry) driver.fail($2,"an entry function must have a body");
    $$=std::move($1); $$.terminator=$2; $$.header_tokens=driver.tokenSpan($$.token_range.first,$2);
    $$.token_range.last=$2+1; driver.markShifted($2);
  }
| function_open body_guard body body_close {
  $$=std::move($1); $$.body=std::move($3); $$.right_brace=$4.first; $$.token_range.last=$4.second;
  if(!$4.first) $$.body.push_back(driver.inserted(TokenKind::RBrace,$4.second));
  $2.reset();
};
block_open: LBRACE { $$=driver.openBody($1,true); };
block: block_open body body_close {
  const auto first=$1->brace;
  $$={first,std::move($2),$3.first,{first,$3.second}};
  if(!$3.first) $$.body.push_back(driver.inserted(TokenKind::RBrace,$3.second));
  $1.reset();
};
body_close:
  RBRACE { $$={$1,$1+1}; driver.markShifted($1); }
| MISSING_RBRACE {
  const auto nested=driver.bodies.back()->nested;
  driver.diagnostics.push_back({driver.token($1).range,nested?"expected '}' at end of nested block":"expected '}' at end of function body",TokenKind::RBrace});
  $$={std::nullopt,$1};
};
body_boundary: %empty {
  driver.bodyBoundary();
  if(!yyla.empty() && yyla.kind()!=symbol_kind::S_MISSING_RBRACE && driver.missingClose(driver.lookahead())) {
    yyla.clear(); auto missing=make_MISSING_RBRACE(driver.lookahead()); yyla.move(missing);
  }
};
body:
  body_boundary { $$.clear(); }
| body body_item body_boundary { $1.push_back(std::move($2)); $$=std::move($1); }
| body error {
  auto nodes=driver.synchronize(true);
  for(auto& node:nodes) $1.push_back(std::move(node));
  $$=std::move($1); yyclearin; yyerrok; driver.bodyBoundary();
};
body_item:
  instruction { driver.markShifted($1.semicolon); $$=std::move($1); }
| declaration { $$=std::move($1); }
| identifier COLON { driver.markShifted($2); $$=CstLabel{$1,$2,{$1,$2+1}}; }
| block { $$=std::make_unique<CstBlock>(std::move($1)); }
| pragma { $$=std::move($1); }
| location { $$=std::move($1); }
| call_prototype { $$=std::move($1); }
| call_targets { $$=std::move($1); }
| branch_targets { $$=std::move($1); }
| reject_body rejected_body_token { $$=CstRecoveryNode{}; };
rejected_body_token:
  LITERAL | dot_identifier | LP | RP | QUESTION | COLON | SEMI | COMMA | LBRACKET | RBRACKET
| PLUS | MINUS | NOT | TILDE | STAR | SLASH | PERCENT | SHL | SHR | LT | LE | GT | GE | EQ | NE | AMP | CARET | PIPE | AND | OR
| DOT_MAXNREG | DOT_MAXNTID | DOT_REQNTID | DOT_MINNCTAPERSM | DOT_REQNCTAPERCLUSTER
| DOT_EXPLICITCLUSTER | DOT_MAXCLUSTERRANK | DOT_NORETURN | DOT_ALIGN | DOT_PTR
| STRING | DOT | EQ_ASSIGN | INVALID;
reject_body: %empty {
  driver.rejectItem(true);
};
optional_semi: %empty %prec EMPTY_SUFFIX { $$=std::nullopt; } | SEMI { $$=$1; driver.markShifted($1); };
string_values:
  STRING { $$={$1}; }
| string_values COMMA STRING { $1.push_back($3); $$=std::move($1); };
pragma: DOT_PRAGMA string_values SEMI {
  std::vector<TokenId> commas; for(std::size_t i=1;i<$2.size();++i) commas.push_back($2[i]-1);
  $$={$1,std::move($2),std::move(commas),$3,{$1,$3+1}}; driver.markShifted($3);
};
integer_values:
  LITERAL { $$={$1}; }
| STRING { $$={$1}; }
| integer_values COMMA LITERAL { $1.push_back($3); $$=std::move($1); }
| integer_values COMMA STRING { $1.push_back($3); $$=std::move($1); };
language: LANGUAGE integer_values {
  std::vector<TokenId> commas; for(std::size_t i=1;i<$2.size();++i) commas.push_back($2[i]-1);
  for(auto id:$2) { const auto kind=driver.token(id).kind; if(kind!=TokenKind::String && kind!=TokenKind::Decimal && kind!=TokenKind::Hex) driver.fail(id,"expected string or integer in .language"); }
  const auto last=$2.back(); $$={$1,std::move($2),std::move(commas),{$1,last+1}}; driver.markShifted(last);
};
resource_values:
  LITERAL { driver.literal($1,"kernel resource value"); driver.markShifted($1); $$={$1}; }
| resource_values COMMA {
    driver.markShifted($2);
    if($1.size()==3) { PTX_FETCH_LOOKAHEAD(); driver.fail(driver.lookahead(),"thread-count kernel resource directives accept at most three values"); }
  } LITERAL {
    driver.literal($4,"kernel resource value after comma"); driver.markShifted($4);
    $1.push_back($4); $$=std::move($1);
  };
resource:
  DOT_EXPLICITCLUSTER {
    driver.markShifted($1); PTX_FETCH_LOOKAHEAD();
    const auto kind=driver.token(driver.lookahead()).kind;
    if(kind==TokenKind::Decimal || kind==TokenKind::Comma)
      driver.fail(driver.lookahead(),".explicitcluster does not accept a value");
    $$={}; $$.directive=$1; $$.token_range={$1,$1+1};
  }
| DOT_MAXNREG decimal { $$={$1,{$2},{},{$1,$2+1}}; driver.markShifted($2); }
| DOT_MINNCTAPERSM decimal { $$={$1,{$2},{},{$1,$2+1}}; driver.markShifted($2); }
| DOT_MAXCLUSTERRANK decimal { $$={$1,{$2},{},{$1,$2+1}}; driver.markShifted($2); }
| DOT_MAXNTID resource_values {
  $$={}; $$.directive=$1;
  $$.values=std::move($2); for(std::size_t i=1;i<$$.values.size();++i) $$.commas.push_back($$.values[i]-1);
  $$.token_range={$1,$$.values.back()+1}; driver.markShifted($$.values.back());
}
| DOT_REQNTID resource_values {
  $$={}; $$.directive=$1;
  $$.values=std::move($2); for(std::size_t i=1;i<$$.values.size();++i) $$.commas.push_back($$.values[i]-1);
  $$.token_range={$1,$$.values.back()+1}; driver.markShifted($$.values.back());
}
| DOT_REQNCTAPERCLUSTER resource_values {
  $$={}; $$.directive=$1;
  $$.values=std::move($2); for(std::size_t i=1;i<$$.values.size();++i) $$.commas.push_back($$.values[i]-1);
  $$.token_range={$1,$$.values.back()+1}; driver.markShifted($$.values.back());
};
version_value: LITERAL {
  if(driver.token($1).kind!=TokenKind::F64) {
    // An expect-style rejection leaves the real token available to recovery,
    // even if generated lookahead has already fetched its successor.
    driver.recovery_error=$1;
    driver.fail($1,"expected PTX version",TokenKind::F64);
  }
  driver.markShifted($1); $$=$1;
};
address_size: LITERAL {
  if(driver.token($1).kind!=TokenKind::Decimal) driver.recovery_error=$1;
  $$=driver.literal($1,"address size"); driver.markShifted($1);
};
file_index: LITERAL {
  if(driver.token($1).kind!=TokenKind::Decimal && driver.token($1).kind!=TokenKind::Hex)
    driver.recovery_error=$1;
  $$=driver.literal($1,"file index",true,false); driver.markShifted($1);
};
file_timestamp: LITERAL {
  if(driver.token($1).kind!=TokenKind::Decimal) driver.recovery_error=$1;
  $$=driver.literal($1,"file timestamp"); driver.markShifted($1);
};
file_size: LITERAL {
  if(driver.token($1).kind!=TokenKind::Decimal) driver.recovery_error=$1;
  $$=driver.literal($1,"file size"); driver.markShifted($1);
};
module_directive:
  DOT_VERSION version_value optional_semi {
    $$={$1,{$2},{},$3,{$1,$3.value_or($2)+1}}; driver.markShifted($$.token_range.last-1);
  }
| DOT_ADDRESS_SIZE address_size optional_semi { $$={$1,{$2},{},$3,{$1,$3.value_or($2)+1}}; driver.markShifted($$.token_range.last-1); }
| DOT_TARGET target_values optional_semi {
  const auto last=$3.value_or($2.back()); std::vector<TokenId> commas;
  for(std::size_t i=1;i<$2.size();++i) commas.push_back($2[i]-1);
  $$={$1,std::move($2),std::move(commas),$3,{$1,last+1}}; driver.markShifted(last);
}
| DOT_FILE file_index STRING optional_semi { $$={$1,{$2,$3},{},$4,{$1,$4.value_or($3)+1}}; driver.markShifted($$.token_range.last-1); }
| DOT_FILE file_index STRING COMMA file_timestamp COMMA file_size optional_semi { $$={$1,{$2,$3,$5,$7},{$4,$6},$8,{$1,$8.value_or($7)+1}}; driver.markShifted($$.token_range.last-1); };
alias: ALIAS identifier COMMA identifier SEMI { $$={$1,$2,$3,$4,$5,{$1,$5+1}}; driver.markShifted($5); };
target_values:
  identifier { $$={$1}; }
| target_values COMMA identifier { $1.push_back($3); $$=std::move($1); };
loc_file: LITERAL { $$=driver.literal($1,"source file index",true,false); driver.markShifted($1); };
loc_line: LITERAL { $$=driver.literal($1,"source line number"); driver.markShifted($1); };
loc_column: LITERAL { $$=driver.literal($1,"source column position"); driver.markShifted($1); };
inline_file: LITERAL { $$=driver.literal($1,"inlined source file index",true,false); driver.markShifted($1); };
inline_line: LITERAL { $$=driver.literal($1,"inlined source line number"); driver.markShifted($1); };
inline_column: LITERAL { $$=driver.literal($1,"inlined source column position"); driver.markShifted($1); };
loc_offset: source_token { driver.markShifted($1); $$=driver.literal($1,"integer function name offset",true,false); };
function_keyword: source_token {
  driver.markShifted($1);
  if(driver.token($1).kind!=TokenKind::Ident || driver.token($1).text!="function_name")
    driver.fail($1,"expected 'function_name'");
  $$=$1;
};
loc_label: source_token {
  driver.markShifted($1);
  if(driver.token($1).kind!=TokenKind::Ident && driver.token($1).kind!=TokenKind::DotIdent)
    driver.fail($1,"expected function name label");
  $$=$1;
};
inline_keyword: source_token {
  driver.markShifted($1);
  if(driver.token($1).kind!=TokenKind::Ident || driver.token($1).text!="inlined_at")
    driver.fail($1,"expected 'inlined_at'");
  $$=$1;
};
inline_offset: %empty { $$=std::nullopt; } | PLUS loc_offset { $$=$2; };
inline_context:
  %empty %prec EMPTY_SUFFIX { $$=std::nullopt; }
| COMMA function_keyword loc_label inline_offset COMMA inline_keyword inline_file inline_line inline_column {
  $$=CstLocInlineContext{$1,$2,$3,$4?std::optional<TokenId>{*$4-1}:std::nullopt,$4,$5,$6,$7,$8,$9,{$1,$9+1}};
};
location: DOT_LOC loc_file loc_line loc_column inline_context optional_semi {
  const auto last=$6.value_or($5?$5->token_range.last-1:$4);
  $$={$1,$2,$3,$4,std::move($5),$6,{$1,last+1}}; driver.markShifted(last);
};
prototype_returns: optional_parameters {
  if($1 && $1->parameters.size()!=1) {
    driver.failRange(driver.sourceRange($1->token_range),".callprototype return parameter list must contain exactly one parameter");
  }
  $$=std::move($1);
};
prototype_sink: identifier { driver.markShifted($1); if(driver.token($1).text!="_") driver.fail($1,"expected '_' in .callprototype"); $$=$1; };
call_prototype: identifier COLON DOT_CALL_PROTOTYPE prototype_returns prototype_sink optional_parameters noreturn abi_preserve abi_control SEMI {
  $$={$1,$2,$3,std::move($4),$5,std::move($6),$7,std::move($8),std::move($9),$10,{$1,$10+1}};
  driver.markShifted($10);
};
call_targets: identifier COLON DOT_CALL_TARGETS target_values SEMI {
  std::vector<TokenId> commas; for(std::size_t i=1;i<$4.size();++i) commas.push_back($4[i]-1);
  $$={$1,$2,$3,std::move($4),std::move(commas),$5,{$1,$5+1}}; driver.markShifted($5);
};
branch_target: identifier parameterized {
  $$={}; $$.name=$1;
  if($2) { $$.left_angle=$2->first; $$.count=$2->second; $$.right_angle=$2->second+1; }
  $$.token_range={$1,$$.right_angle?*$$.right_angle+1:$1+1};
};
branch_target_values:
  branch_target { $$.targets.push_back(std::move($1)); }
| branch_target_values COMMA branch_target { $1.commas.push_back($2); $1.targets.push_back(std::move($3)); $$=std::move($1); };
branch_targets: identifier COLON DOT_BRANCH_TARGETS branch_target_values SEMI {
  $$=std::move($4); $$.label=$1; $$.colon=$2; $$.directive=$3; $$.semicolon=$5; $$.token_range={$1,$5+1}; driver.markShifted($5);
};
section_name: source_token {
  driver.markShifted($1);
  if(driver.token($1).kind!=TokenKind::Ident && driver.token($1).kind!=TokenKind::DotIdent)
    driver.fail($1,"expected section name");
  $$=$1;
};
section: DOT_SECTION section_name LBRACE section_payload RBRACE optional_semi {
  const auto last=$6.value_or($5); $$={$1,$2,$3,std::move($4),$5,$6,{$1,last+1}}; driver.markShifted(last);
};
section_payload:
  %empty { $$.clear(); }
| section_payload section_atom { $1.push_back($2); $$=std::move($1); }
| section_payload raw_group { $1.insert($1.end(),$2.begin(),$2.end()); $$=std::move($1); };
raw_payload:
  %empty { $$.clear(); }
| raw_payload raw_atom { $1.push_back($2); $$=std::move($1); }
| raw_payload raw_group { $1.insert($1.end(),$2.begin(),$2.end()); $$=std::move($1); };
raw_group: LBRACE raw_payload RBRACE {
  $$=std::move($2); $$.insert($$.begin(),$1); $$.push_back($3);
};
section_atom:
  LITERAL | identifier | dot_identifier | LP | RP | QUESTION | COLON | SEMI | COMMA | AT | LBRACKET | RBRACKET
| PLUS | MINUS | NOT | TILDE | STAR | SLASH | PERCENT | SHL | SHR | LT | LE | GT | GE | EQ | NE | AMP | CARET | PIPE | AND | OR
| DOT_LOC | DOT_CALL_PROTOTYPE | DOT_CALL_TARGETS | DOT_BRANCH_TARGETS | DOT_MAXNREG | DOT_MAXNTID | DOT_REQNTID
| DOT_MINNCTAPERSM | DOT_REQNCTAPERCLUSTER | DOT_EXPLICITCLUSTER | DOT_MAXCLUSTERRANK | DOT_NORETURN | DOT_ALIGN | DOT_PTR
| STRING | DOT | EQ_ASSIGN | INVALID;
raw_atom:
  section_atom | state_space | qualifier | ENTRY | DOT_FUNC | DOT_VERSION | DOT_TARGET | DOT_ADDRESS_SIZE | DOT_FILE | DOT_SECTION | DOT_PRAGMA;
source_token: raw_atom | LBRACE | RBRACE;
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
  bool initial_address_base=false, named_index_close=false, coordinates_close=false, fabric_close=false;
  for(int i=0;i<yystack_.size();++i) {
    const auto kind=yystack_[i].kind();
    coordinates_close|=kind==symbol_kind::S_vector;
    fabric_close|=kind==symbol_kind::S_bracket_heads || kind==symbol_kind::S_bracket_component;
    if(kind==symbol_kind::S_LBRACKET) {
      initial_address_base=i==0;
      named_index_close=i+1<yystack_.size() && yystack_[i+1].kind()==symbol_kind::S_identifier;
      break;
    }
    if(kind==symbol_kind::S_ordinary_head || kind==symbol_kind::S_call_head ||
        kind==symbol_kind::S_module_items || kind==symbol_kind::S_body) break;
  }
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
    diagnostic.message=named_index_close?"expected ']' after named-array index":
        coordinates_close?(fabric_close?"expected ']' after compound bracket coordinates":"expected ']' after tensor coordinates"):
        fabric_close?"expected ']' after fabric handle":"expected ']'";
    diagnostic.expected_kind=TokenKind::RBracket;
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
  } else if(operand && initial_address_base && !named_index_close) {
    diagnostic.message="expected address base"; diagnostic.expected_kind=TokenKind::Ident;
  } else if(operand) diagnostic.message="expected constant expression";
  if(driver.entry_mode==EntryMode::Module && head<0) {
    bool section_owner=false, file_owner=false, version_owner=false;
    bool location_owner=false, header_owner=false, declaration_owner=false;
    bool other_owner=false, target_owner=false, attribute_owner=false, abi_owner=false;
    bool call_targets_owner=false, branch_targets_owner=false, prototype_owner=false;
    bool parameter_owner=false, parameter_base_owner=false, variable_name_owner=false;
    bool pointer_owner=false, pragma_owner=false, alias_owner=false, language_owner=false;
    bool resource_owner=false, single_resource=false, has_left_brace=false;
    bool header_name_pending=false, header_identifier=false, header_parameter=false;
    bool parameter_complete=false, prototype_sink_seen=false, prototype_sink_pending=false;
    bool file_index_seen=false, file_string_seen=false;
    int file_commas=0;
    std::string_view location_expected;
    std::optional<TokenKind> location_kind;
    for(int i=0;i<yystack_.size();++i) {
      const auto kind=yystack_[i].kind();
      if(kind==symbol_kind::S_module_items || kind==symbol_kind::S_body) break;
      header_identifier|=kind==symbol_kind::S_identifier;
      header_parameter|=kind==symbol_kind::S_LP;
      parameter_complete|=kind==symbol_kind::S_parameter || kind==symbol_kind::S_parameters_nonempty ||
          (kind==symbol_kind::S_parameter_head && header_identifier);
      prototype_sink_seen|=kind==symbol_kind::S_prototype_sink;
      if(kind==symbol_kind::S_DOT_CALL_PROTOTYPE)
        prototype_sink_pending=!prototype_sink_seen && !parameter_owner && !header_parameter;
      file_index_seen|=kind==symbol_kind::S_file_index;
      file_string_seen|=kind==symbol_kind::S_STRING;
      file_commas+=kind==symbol_kind::S_COMMA;
      if(kind==symbol_kind::S_ENTRY || kind==symbol_kind::S_DOT_FUNC)
        header_name_pending=!header_identifier && !header_parameter;
      has_left_brace|=kind==symbol_kind::S_LBRACE;
      if(location_expected.empty()) {
        if(kind==symbol_kind::S_inline_line) { location_expected="inlined source column position"; location_kind=TokenKind::Decimal; }
        else if(kind==symbol_kind::S_inline_file) { location_expected="inlined source line number"; location_kind=TokenKind::Decimal; }
        else if(kind==symbol_kind::S_inline_keyword) location_expected="inlined source file index";
        else if(kind==symbol_kind::S_loc_line) { location_expected="source column position"; location_kind=TokenKind::Decimal; }
        else if(kind==symbol_kind::S_loc_file) { location_expected="source line number"; location_kind=TokenKind::Decimal; }
        else if(kind==symbol_kind::S_DOT_LOC) location_expected="source file index";
        else if(kind==symbol_kind::S_PLUS) location_expected="integer function name offset";
        else if(kind==symbol_kind::S_function_keyword) location_expected="function name label";
      }
      section_owner|=kind==symbol_kind::S_DOT_SECTION;
      file_owner|=kind==symbol_kind::S_DOT_FILE;
      version_owner|=kind==symbol_kind::S_DOT_VERSION;
      location_owner|=kind==symbol_kind::S_DOT_LOC;
      target_owner|=kind==symbol_kind::S_DOT_TARGET;
      attribute_owner|=kind==symbol_kind::S_ATTRIBUTE;
      abi_owner|=kind==symbol_kind::S_ABI_PRESERVE || kind==symbol_kind::S_ABI_CONTROL;
      call_targets_owner|=kind==symbol_kind::S_DOT_CALL_TARGETS;
      branch_targets_owner|=kind==symbol_kind::S_DOT_BRANCH_TARGETS;
      prototype_owner|=kind==symbol_kind::S_DOT_CALL_PROTOTYPE;
      parameter_owner|=kind==symbol_kind::S_parameter_space || kind==symbol_kind::S_parameter_base ||
          kind==symbol_kind::S_parameter_head || kind==symbol_kind::S_parameter;
      parameter_base_owner|=kind==symbol_kind::S_parameter_base || kind==symbol_kind::S_parameter_head;
      pointer_owner|=kind==symbol_kind::S_DOT_PTR;
      variable_name_owner|=kind==symbol_kind::S_declaration_head;
      pragma_owner|=kind==symbol_kind::S_DOT_PRAGMA;
      alias_owner|=kind==symbol_kind::S_ALIAS;
      language_owner|=kind==symbol_kind::S_LANGUAGE;
      single_resource|=kind==symbol_kind::S_DOT_MAXNREG || kind==symbol_kind::S_DOT_MINNCTAPERSM || kind==symbol_kind::S_DOT_MAXCLUSTERRANK;
      resource_owner|=single_resource || kind==symbol_kind::S_DOT_MAXNTID || kind==symbol_kind::S_DOT_REQNTID || kind==symbol_kind::S_DOT_REQNCTAPERCLUSTER;
      header_owner|=kind==symbol_kind::S_ENTRY || kind==symbol_kind::S_DOT_FUNC ||
          kind==symbol_kind::S_entry_head || kind==symbol_kind::S_func_head ||
          kind==symbol_kind::S_function_header;
      declaration_owner|=kind==symbol_kind::S_state_space || kind==symbol_kind::S_declaration_head ||
          kind==symbol_kind::S_declarators;
      other_owner|=kind==symbol_kind::S_DOT_TARGET || kind==symbol_kind::S_DOT_ADDRESS_SIZE ||
          kind==symbol_kind::S_DOT_PRAGMA || kind==symbol_kind::S_ALIAS ||
          kind==symbol_kind::S_DOT_CALL_PROTOTYPE || kind==symbol_kind::S_DOT_CALL_TARGETS ||
          kind==symbol_kind::S_DOT_BRANCH_TARGETS;
    }
    if(current>1 && offending==TokenKind::Comma && previous==TokenKind::Decimal &&
        (driver.token(current-2).kind==TokenKind::DotMaxnreg ||
         driver.token(current-2).kind==TokenKind::DotMinnctapersm ||
         driver.token(current-2).kind==TokenKind::DotMaxclusterrank)) {
      diagnostic.message="this kernel resource directive accepts one value"; diagnostic.expected_kind.reset();
    } else if(driver.bodies.size() && current==driver.statement_first && (offending==TokenKind::DotCallPrototype ||
        offending==TokenKind::DotCallTargets || offending==TokenKind::DotBranchTargets)) {
      diagnostic.message="'"+std::string(driver.token(current).text)+"' requires a preceding function-local label";
      diagnostic.expected_kind.reset();
    } else if(prototype_owner && semi) {
      diagnostic.message="expected ';' after .callprototype"; diagnostic.expected_kind=TokenKind::Semicolon;
    } else if(prototype_sink_pending) {
      diagnostic.message="expected '_' in .callprototype"; diagnostic.expected_kind=TokenKind::Ident;
    } else if(call_targets_owner || branch_targets_owner) {
      diagnostic.expected_kind.reset();
      if(branch_targets_owner && previous==TokenKind::Lt) {
        diagnostic.message="expected branch target count"; diagnostic.expected_kind=TokenKind::Decimal;
      } else if(branch_targets_owner && previous==TokenKind::Decimal && !semi) {
        diagnostic.message="expected '>' after branch target count"; diagnostic.expected_kind=TokenKind::Gt;
      } else if(semi) {
        diagnostic.message=call_targets_owner?"expected ';' after .calltargets":"expected ';' after .branchtargets";
        diagnostic.expected_kind=TokenKind::Semicolon;
      } else if(previous==TokenKind::Comma && offending==TokenKind::Semicolon)
        diagnostic.message=call_targets_owner?"call target list cannot end with a trailing comma":"branch target list cannot end with a trailing comma";
      else if(offending==TokenKind::Semicolon)
        diagnostic.message=call_targets_owner?".calltargets requires at least one function target":".branchtargets requires at least one label target";
      else { diagnostic.message=call_targets_owner?"expected call target function":"expected branch target label"; diagnostic.expected_kind=TokenKind::Ident; }
    } else if(parameter_owner && rb && !operand) {
      diagnostic.message="expected ']'"; diagnostic.expected_kind=TokenKind::RBracket;
    } else if(parameter_complete && rp &&
        !(offending==TokenKind::LBracket && previous==TokenKind::RBracket)) {
      diagnostic.message="expected ')'"; diagnostic.expected_kind=TokenKind::RParen;
    } else if(header_name_pending) {
      diagnostic.message="expected function name"; diagnostic.expected_kind=TokenKind::Ident;
    } else if(parameter_owner && previous==TokenKind::DotAlign) {
      diagnostic.message=pointer_owner?"expected pointer alignment":"expected parameter alignment";
      diagnostic.expected_kind=TokenKind::Decimal;
    } else if(header_owner && offending==TokenKind::LBracket && previous==TokenKind::RBracket) {
      diagnostic.message="multidimensional function parameters are not supported"; diagnostic.expected_kind.reset();
    } else if(parameter_owner) {
      diagnostic.message=parameter_base_owner?"expected parameter name":"expected parameter type";
      diagnostic.expected_kind=parameter_base_owner?TokenKind::Ident:TokenKind::DotIdent;
    } else if(attribute_owner && comma) {
      diagnostic.message="expected ',' in .unified attribute"; diagnostic.expected_kind=TokenKind::Comma;
    } else if(attribute_owner && rp) {
      diagnostic.message="expected ')' in .unified attribute"; diagnostic.expected_kind=TokenKind::RParen;
    } else if(attribute_owner) {
      diagnostic.message="expected attribute name"; diagnostic.expected_kind=TokenKind::DotIdent;
    } else if(abi_owner) {
      diagnostic.message=prototype_owner?"expected ABI-preserve register count":"expected ABI preserved register count";
      diagnostic.expected_kind=TokenKind::Decimal;
    } else if(section_owner) {
      diagnostic.message=has_left_brace?"expected section closing brace":"expected section opening brace";
      diagnostic.expected_kind=has_left_brace?TokenKind::RBrace:TokenKind::LBrace;
    } else if(version_owner) {
      diagnostic.message="expected PTX version"; diagnostic.expected_kind=TokenKind::F64;
    } else if(file_owner && comma) {
      diagnostic.message="expected comma before file size"; diagnostic.expected_kind=TokenKind::Comma;
    } else if(file_owner && !file_index_seen) {
      diagnostic.message="expected file index"; diagnostic.expected_kind.reset();
    } else if(file_owner && file_string_seen && file_commas) {
      diagnostic.message=file_commas==1?"expected file timestamp":"expected file size";
      diagnostic.expected_kind=TokenKind::Decimal;
    } else if(other_owner && previous==TokenKind::DotAddressSize) {
      diagnostic.message="expected address size"; diagnostic.expected_kind=TokenKind::Decimal;
    } else if(target_owner) {
      diagnostic.message=previous==TokenKind::Comma?"expected target option":"expected target architecture";
      diagnostic.expected_kind=TokenKind::Ident;
    } else if(declaration_owner && previous==TokenKind::Lt) {
      diagnostic.message="expected parameterized count"; diagnostic.expected_kind=TokenKind::Decimal;
    } else if(declaration_owner && previous==TokenKind::Decimal && current>1 && driver.token(current-2).kind==TokenKind::Lt) {
      diagnostic.message="expected '>'"; diagnostic.expected_kind=TokenKind::Gt;
    } else if(declaration_owner && previous==TokenKind::DotAlign) {
      diagnostic.message="expected variable alignment"; diagnostic.expected_kind=TokenKind::Decimal;
    } else if(variable_name_owner && !semi && !operand && !bracket) {
      diagnostic.message="expected variable name"; diagnostic.expected_kind=TokenKind::Ident;
    } else if(declaration_owner && !semi && !operand && !bracket) {
      diagnostic.message="expected variable type"; diagnostic.expected_kind=TokenKind::DotIdent;
    } else if(resource_owner) {
      diagnostic.message=previous==TokenKind::Comma?"expected kernel resource value after comma":"expected kernel resource value";
      diagnostic.expected_kind=TokenKind::Decimal;
    } else if(pragma_owner) {
      diagnostic.message=semi?"expected pragma terminator":previous==TokenKind::Comma?"expected pragma string after comma":"expected pragma string";
      diagnostic.expected_kind=semi?TokenKind::Semicolon:TokenKind::String;
    } else if(alias_owner && comma) {
      diagnostic.message="expected ',' in .alias"; diagnostic.expected_kind=TokenKind::Comma;
    } else if(language_owner) {
      diagnostic.message="expected string or integer in .language"; diagnostic.expected_kind.reset();
    } else if(location_owner && comma) {
      diagnostic.message="expected comma before inlined_at"; diagnostic.expected_kind=TokenKind::Comma;
    } else if(file_owner && !operand) {
      diagnostic.message="expected file name"; diagnostic.expected_kind=TokenKind::String;
    } else if(location_owner) {
      diagnostic.message="expected "+std::string(location_expected);
      diagnostic.expected_kind=location_kind;
    } else if(header_owner && (brace || semi)) {
      diagnostic.expected_kind.reset();
      if(offending==TokenKind::Eof) diagnostic.message="expected function body or prototype terminator";
      else if(offending==TokenKind::DotPragma) diagnostic.message="'.pragma' is only valid in an entry function header";
      else if(offending==TokenKind::DotMaxnreg || offending==TokenKind::DotMaxntid ||
          offending==TokenKind::DotReqntid || offending==TokenKind::DotMinnctapersm ||
          offending==TokenKind::DotReqnctapercluster || offending==TokenKind::DotExplicitcluster ||
          offending==TokenKind::DotMaxclusterrank)
        diagnostic.message="kernel resource directives are only valid in an entry function header";
      else diagnostic.message="unsupported function header token '"+std::string(driver.token(current).text)+"'";
    } else if(driver.bodies.empty() && !section_owner && !file_owner && !version_owner &&
        !location_owner && !header_owner && !declaration_owner && !other_owner) {
      diagnostic.message="expected module directive, variable declaration, or function";
      diagnostic.expected_kind.reset();
    }
  }
  // LAC can prove an operand is complete but reject the following statement
  // token before numeric normalization runs. Validate only the live operand's
  // completed expression; comma/head boundaries exclude prior plain registers.
  if(semi && head>=0 && spelling.empty()) {
    std::optional<TokenId> reg;
    for(int i=0;i<head && !reg;++i) {
      const auto kind=yystack_[i].kind();
      if(kind==symbol_kind::S_COMMA) break;
      switch(kind) {
        case symbol_kind::S_expression:
        case symbol_kind::S_numeric_expression:
        case symbol_kind::S_operand_expression:
        case symbol_kind::S_unary:
        case symbol_kind::S_numeric_unary:
        case symbol_kind::S_operand_unary:
        case symbol_kind::S_postfix:
        case symbol_kind::S_numeric_postfix:
        case symbol_kind::S_atom:
        case symbol_kind::S_numeric_atom:
        case symbol_kind::S_cast_expression:
        case symbol_kind::S_logical_or:
        case symbol_kind::S_logical_and:
        case symbol_kind::S_bit_or:
        case symbol_kind::S_bit_xor:
        case symbol_kind::S_bit_and:
        case symbol_kind::S_equality:
        case symbol_kind::S_relation:
        case symbol_kind::S_shift:
        case symbol_kind::S_sum:
        case symbol_kind::S_product:
        case symbol_kind::S_numeric_logical_or:
        case symbol_kind::S_numeric_logical_and:
        case symbol_kind::S_numeric_bit_or:
        case symbol_kind::S_numeric_bit_xor:
        case symbol_kind::S_numeric_bit_and:
        case symbol_kind::S_numeric_equality:
        case symbol_kind::S_numeric_relation:
        case symbol_kind::S_numeric_shift:
        case symbol_kind::S_numeric_sum:
        case symbol_kind::S_numeric_product:
        case symbol_kind::S_operand_logical_or:
        case symbol_kind::S_operand_logical_and:
        case symbol_kind::S_operand_bit_or:
        case symbol_kind::S_operand_bit_xor:
        case symbol_kind::S_operand_bit_and:
        case symbol_kind::S_operand_equality:
        case symbol_kind::S_operand_relation:
        case symbol_kind::S_operand_shift:
        case symbol_kind::S_operand_sum:
        case symbol_kind::S_operand_product:
          reg=driver.numericRegister(yystack_[i].value.as<ParsedExpression>().expression.token_range);
          break;
        default: break;
      }
    }
    if(reg) diagnostic={driver.token(*reg).range,
        "registers cannot participate in constant expressions",std::nullopt};
  }
  driver.diagnose(std::move(diagnostic));
}

/** Semantic-action failures carry their exact diagnostic through this callback. */
void ptx_frontend::detail::GeneratedPtxParser::error(const std::string& message) {
  driver.generatedError(message);
}
