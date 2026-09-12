# C++ naming and legacy-interface policy

New handwritten C++ functions, methods, data members, and local helpers use
`snake_case`. Type names, concepts, and enum values continue to use the existing
project C++ style. This rule applies at declaration and use sites; generated
sources are owned by their generator and must be changed through that pipeline.

Public spellings are compatibility contracts. The absence of an in-repository
consumer does not authorize their removal or renaming: downstream users can
include installed headers or initialize public descriptors directly. A future
public rename needs a reviewed compatibility path or an explicitly versioned API
break.

## Audited retained spellings

| Spelling | Scope | Retention reason |
| --- | --- | --- |
| Existing public camelCase resolver entry points, including `resolveModule`, `resolveModuleOnly`, `resolveAndValidateModule`, `validateModule`, `checkModuleAvailability`, and `resolveInstruction` | Installed public resolution API | These established spellings remain source-compatible public API. The `snake_case` rule guides new handwritten/internal interfaces; it is not an implicit rename of existing overloads or entry points. |
| `detail::selectVariant` and its `resolved_ir::selectVariant` compatibility alias | Installed resolution-detail header; generated resolver calls it | A public template adapter. Renaming it would break direct downstream use and requires generator and API-migration coordination. New non-public helpers use `snake_case`. |
| `resolve_fields` | Resolution-detail interface; generated resolver calls it | Already `snake_case`; it remains a stable bridge between generated descriptors and handwritten resolution. |
| `SyntaxVariantDescriptor::get_required_modifier_num` | Public descriptor header | Existing installed method spelling and generated descriptor contract. Retain until a compatibility-preserving public API migration is explicitly approved. |
| `SyntaxInstructionDescriptor::Opcode_name` | Public aggregate descriptor and generator output | Existing aggregate-designator spelling is consumed by generated code and can be used by downstream aggregate initialization. Do not silently rename it. New descriptor fields use `snake_case`, such as `ResolvedInstructionDescriptor::opcode_name`. |
| `check_end` | Installed descriptor namespace and generator model | Historical namespace taxonomy, not a promise about checker completion. Its public generated descriptors make a mechanical namespace rename an API and generator migration, not a formatting cleanup. |

The retired commented-out `ptx_frontend_common` source/test targets were removed
from its CMake file rather than preserved as dormant build policy. The remaining
interface library and exported target are intentional.

## Formatting gate

CI requires `clang-format` major 21 and runs it with `--dry-run --Werror` over
only files returned by `git ls-files`. C/C++ paths under `generated`, `vendor`,
or `third_party`, and `.gen.*` files, are excluded because their owners are
generation or vendoring workflows. Regenerate those artifacts instead of applying
manual formatter changes.
