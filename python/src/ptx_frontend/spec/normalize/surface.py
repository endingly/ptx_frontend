"""Validate the closed surface instruction matrix at the YAML boundary."""

from ptx_frontend.spec.model import (
    ModifierKind, ModifierPresence, OperandKind, OperandRole, OperandAccess,
    OperandRegisterWidthPolicy, OperandVectorTypePolicy, OperandTypeExpressionKind, SurfaceAddressingMode,
    SurfaceBoundaryMode, SurfaceGeometry, SurfaceInstructionSpec, SurfaceQuery,
    SurfaceReductionOperation, VariantSpec, modifier_spellings,
)
from .availability import normalize_availability


def _floor(availability: dict, ptx: str, sm: int) -> None:
    """Reject a canonical feature gate that permits an earlier ISA or target."""
    def version(value: object) -> tuple[int, ...]:
        """Convert a canonical decimal ISA version without floating point."""
        return tuple(int(part) for part in str(value).split('.'))
    clauses = availability.get('any_of', [availability])
    if any(version(clause.get('ptx', '0.0')) < version(ptx) or
           clause.get('sm', 0) < sm for clause in clauses):
        raise ValueError(f'surface availability requires PTX {ptx} / sm{sm}')


def normalize_surface_contract(raw: object, opcode: str) -> SurfaceInstructionSpec | None:
    """Require exact typed surface metadata only on the four surface families."""
    if raw is None:
        if opcode in {'suld', 'sust', 'sured', 'suq'}:
            raise ValueError(f'{opcode} variant requires a surface contract')
        return None
    if opcode not in {'suld', 'sust', 'sured', 'suq'} or not isinstance(raw, dict):
        raise ValueError('surface contract belongs to a surface opcode')
    if set(raw) - {'geometry', 'addressing', 'boundary', 'operation', 'query',
                   'vector_arity', 'indirect_availability'}:
        raise ValueError('surface contract has unknown fields')
    def domain(name: str, enum):
        """Classify one optional source spelling into its closed enum domain."""
        return enum(raw[name]) if raw.get(name) is not None else None
    arity = raw.get('vector_arity', 1)
    if type(arity) is not int or arity not in {1, 2, 4}:
        raise ValueError('surface vector arity must be 1, 2, or 4')
    indirect = normalize_availability(raw.get('indirect_availability', {}))
    _floor(indirect, '3.1', 20)
    result = SurfaceInstructionSpec(
        geometry=domain('geometry', SurfaceGeometry),
        addressing=domain('addressing', SurfaceAddressingMode),
        boundary=domain('boundary', SurfaceBoundaryMode),
        operation=domain('operation', SurfaceReductionOperation),
        query=domain('query', SurfaceQuery), vector_arity=arity,
        indirect_availability=indirect)
    if opcode == 'suq':
        if result.query is None or any((result.geometry, result.addressing,
                result.boundary, result.operation)) or arity != 1:
            raise ValueError('suq requires only a scalar surface query')
    else:
        if not all((result.geometry, result.addressing, result.boundary)) or result.query:
            raise ValueError('surface access requires geometry, addressing, and boundary')
        if opcode == 'suld' and result.addressing is not SurfaceAddressingMode.BYTE:
            raise ValueError('suld only supports byte addressing')
        if result.addressing is SurfaceAddressingMode.SAMPLE and result.geometry in {
                SurfaceGeometry.ARRAY_ONE_D, SurfaceGeometry.ARRAY_TWO_D}:
            raise ValueError('formatted surface accesses have no array geometry')
        if opcode == 'sured':
            if result.operation is None or arity != 1 or result.geometry in {
                    SurfaceGeometry.ARRAY_ONE_D, SurfaceGeometry.ARRAY_TWO_D}:
                raise ValueError('sured requires a scalar non-array reduction')
        elif result.operation:
            raise ValueError('only sured selects a surface reduction operation')
    return result


def validate_surface_variant(opcode: str, variant: VariantSpec) -> None:
    """Keep source modifiers, operand topology, types, and feature gates coherent."""
    contract = variant.surface
    if contract is None:
        return
    fixed = ({'query': f'.{contract.query.value}'} if opcode == 'suq' else {
        'addressing': f'.{contract.addressing.value}',
        'geometry': f'.{contract.geometry.value}',
        'boundary': f'.{contract.boundary.value}',
        **({'operation': f'.{contract.operation.value}'} if contract.operation else {}),
        **({'vector': f'.v{contract.vector_arity}'} if contract.vector_arity != 1 else {}),
    })
    allowed_names = set(fixed) | {'dtype'}
    if opcode in {'suld', 'sust'} and contract.addressing is SurfaceAddressingMode.BYTE:
        allowed_names.add('cache')
    names = [modifier.name for modifier in variant.modifiers]
    if len(names) != len(set(names)) or set(names) - allowed_names or not (set(fixed) | {'dtype'}) <= set(names):
        raise ValueError('surface modifier slots are outside the closed grammar')
    order = (['query', 'dtype'] if opcode == 'suq' else
             ['addressing'] + (['operation'] if contract.operation else []) +
             ['geometry'] + (['cache'] if 'cache' in names else []) +
             (['vector'] if contract.vector_arity != 1 else []) + ['dtype', 'boundary'])
    if names != order or variant.modifier_order_aliases:
        raise ValueError('surface modifier slots must follow the canonical Syntax order')
    for modifier in variant.modifiers:
        if modifier.name in fixed and (
                modifier.kind is not ModifierKind.FLAG or
                modifier.presence is not ModifierPresence.FIXED or
                modifier.value is not True or modifier.values or
                modifier.token != fixed[modifier.name]):
            raise ValueError('surface fixed modifier contradicts its typed slot')
        if modifier.name == 'dtype' and (
                modifier.kind is not ModifierKind.TYPE or
                modifier.presence is not ModifierPresence.REQUIRED or
                modifier.domain != 'scalar_types'):
            raise ValueError('surface dtype requires one scalar type slot')
        if modifier.name in {'dtype', 'cache'} and any(value.token not in {None, '.' + str(value.value)} for value in modifier.values):
            raise ValueError('surface typed value spelling contradicts its semantic value')
        if modifier.name == 'cache' and (
                modifier.kind is not ModifierKind.CACHE or
                modifier.presence is not ModifierPresence.OPTIONAL or
                modifier.default != 'unspecified'):
            raise ValueError('surface cache requires the optional typed cache slot')
    tokens = {spelling for modifier in variant.modifiers
              for spelling in modifier_spellings(modifier)}
    if len(variant.operand_layouts) != 1:
        raise ValueError('surface forms require one exact operand layout')
    operands = variant.operand_layouts[0].operands
    if opcode == 'suq':
        dtype = next(modifier for modifier in variant.modifiers if modifier.name == 'dtype')
        if {value.value for value in dtype.values} != {'b32'} or f'.{contract.query.value}' not in tokens or '.b32' not in tokens:
            raise ValueError('suq query or type contradicts surface metadata')
        if len(operands) != 2 or operands[0].kind is not OperandKind.REGISTER or operands[1].kind is not OperandKind.SURFACE_QUERY_RESOURCE:
            raise ValueError('suq requires a scalar destination and surface query resource')
        data, resource = operands
        if (data.type_expression is None or
                data.type_expression.kind is not OperandTypeExpressionKind.MODIFIER or
                data.type_expression.modifier_name != 'dtype' or
                data.role is not OperandRole.DESTINATION or data.access is not OperandAccess.WRITE or
                data.register_width_policy is not OperandRegisterWidthPolicy.SAME_WIDTH or
                resource.role is not OperandRole.SOURCE or resource.access is not OperandAccess.READ or
                resource.type_expression is not None):
            raise ValueError('suq requires dtype-selected destination write and untyped resource read')
        _floor(variant.availability, {
            SurfaceQuery.WIDTH: '1.5', SurfaceQuery.HEIGHT: '1.5',
            SurfaceQuery.DEPTH: '1.5', SurfaceQuery.CHANNEL_DATA_TYPE: '2.1',
            SurfaceQuery.CHANNEL_ORDER: '2.1', SurfaceQuery.ARRAY_SIZE: '4.1',
            SurfaceQuery.MEMORY_LAYOUT: '4.2'}[contract.query], 0)
        return
    required = {f'.{contract.geometry.value}', f'.{contract.addressing.value}',
                f'.{contract.boundary.value}'}
    if contract.operation:
        required.add(f'.{contract.operation.value}')
    if not required <= tokens or (contract.vector_arity == 1 and tokens & {'.v1', '.v2', '.v4'}) or (contract.vector_arity != 1 and f'.v{contract.vector_arity}' not in tokens):
        raise ValueError('surface modifier spelling contradicts typed contract')
    if len(operands) != 2:
        raise ValueError('surface access requires exactly two operands')
    access, data = (operands[1], operands[0]) if opcode == 'suld' else operands
    if access.kind is not OperandKind.SURFACE_ACCESS or access.surface_geometry is not contract.geometry:
        raise ValueError('surface access geometry contradicts typed contract')
    if (access.role is not OperandRole.SOURCE or access.access is not OperandAccess.READ or
            access.type_expression is not None or
            data.role is not (OperandRole.DESTINATION if opcode == 'suld' else OperandRole.SOURCE) or
            data.access is not (OperandAccess.WRITE if opcode == 'suld' else OperandAccess.READ)):
        raise ValueError('surface operand roles and accesses contradict instruction direction')
    expected = ((OperandKind.REGISTER if opcode == 'suld' else OperandKind.REGISTER_OR_IMMEDIATE) if contract.vector_arity == 1 else (OperandKind.REGISTER_VECTOR if opcode == 'suld' else OperandKind.VALUE_VECTOR))
    if data.kind is not expected or (contract.vector_arity != 1 and data.vector_arities != (contract.vector_arity,)):
        raise ValueError('surface data shape contradicts vector arity')
    dtype = next((modifier for modifier in variant.modifiers if modifier.name == 'dtype'), None)
    if dtype is None or data.type_expression is None or data.type_expression.kind is not OperandTypeExpressionKind.MODIFIER or data.type_expression.modifier_name != 'dtype':
        raise ValueError('surface data must select dtype')
    types = {value.value for value in dtype.values}
    allowed = {'b8', 'b16', 'b32', 'b64'}
    if contract.addressing is SurfaceAddressingMode.SAMPLE:
        allowed = {'b32'}
    if opcode == 'sured':
        if contract.addressing is SurfaceAddressingMode.SAMPLE:
            allowed = {'b32', 'b64'} if contract.operation in {SurfaceReductionOperation.MIN, SurfaceReductionOperation.MAX} else {'b32'}
        else:
            allowed = {SurfaceReductionOperation.ADD: {'u32', 'u64', 's32'},
                       SurfaceReductionOperation.MIN: {'u32', 's32', 'u64', 's64'},
                       SurfaceReductionOperation.MAX: {'u32', 's32', 'u64', 's64'},
                       SurfaceReductionOperation.AND: {'b32'},
                       SurfaceReductionOperation.OR: {'b32'}}[contract.operation]
    if contract.vector_arity == 4 and 'b64' in types:
        raise ValueError('surface vectors cannot exceed 128 bits')
    narrow = bool(types & {'b8', 'b16'})
    expected_width = OperandRegisterWidthPolicy.EQUAL_OR_WIDER if narrow else OperandRegisterWidthPolicy.SAME_WIDTH
    if (narrow and not types <= {'b8', 'b16'}) or data.register_width_policy is not expected_width:
        raise ValueError('surface widening belongs only to a separate narrow dtype group')
    if contract.vector_arity != 1 and (data.vector_type_policy is not OperandVectorTypePolicy.ELEMENT or data.vector_allow_sink):
        raise ValueError('surface data vectors require ordinary nonsink element lanes')
    if not types or not types <= allowed:
        raise ValueError('surface dtype is outside the legal operation matrix')
    ptx, sm = ('2.0', 20) if opcode == 'sured' or contract.addressing is SurfaceAddressingMode.SAMPLE else ('1.5', 0)
    if opcode != "sured" and contract.addressing is SurfaceAddressingMode.BYTE and contract.geometry in {SurfaceGeometry.THREE_D, SurfaceGeometry.ARRAY_ONE_D, SurfaceGeometry.ARRAY_TWO_D}:
        ptx, sm = '3.0', 20
    if contract.boundary is not SurfaceBoundaryMode.TRAP:
        ptx, sm = max(ptx, '2.0'), 20
    if opcode == 'sured' and contract.operation in {SurfaceReductionOperation.MIN, SurfaceReductionOperation.MAX} and types & {'u64', 's64', 'b64'}:
        ptx, sm = '8.1', 50
    _floor(variant.availability, ptx, sm)
    caches = [modifier for modifier in variant.modifiers if modifier.kind is ModifierKind.CACHE]
    if caches and (opcode == 'sured' or contract.addressing is SurfaceAddressingMode.SAMPLE):
        raise ValueError('surface formatted/reduction forms have no cache modifier')
    for cache in caches:
        allowed_cache = {'ca', 'cg', 'cs', 'cv'} if opcode == 'suld' else {'wb', 'cg', 'cs', 'wt'}
        for value in cache.values:
            if value.value not in allowed_cache:
                raise ValueError('surface cache operator is invalid for its direction')
            _floor(value.availability, '2.0', 20)
