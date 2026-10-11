#include <ptx_frontend/resolved_ir/ptx_resolved_ir_checker_support.hpp>
#include <ptx_frontend/resolved_ir/ptx_video.hpp>

#include <array>
#include <string>

namespace ptx_frontend::resolved_ir::checker {
namespace {

/** Find one canonical generated field identity without interpreting spellings. */
const FieldView* video_field(std::span<const FieldView> fields,
                             std::string_view id) {
  for (const auto& field : fields)
    if (field.field_id == id)
      return &field;
  return nullptr;
}

/** Test child provenance against a retained wrapper or instruction range. */
bool video_range_inside(SourceRange child, SourceRange outer) {
  const auto before = [](SourcePos left, SourcePos right) {
    return left.line < right.line ||
           (left.line == right.line && left.column <= right.column);
  };
  return child.start.line > 0 && child.start.column > 0 &&
         outer.start.line > 0 && outer.start.column > 0 && outer.end.line > 0 &&
         outer.end.column > 0 && outer.start != outer.end &&
         before(outer.start, outer.end) && before(outer.start, child.start) &&
         before(child.end, outer.end);
}

/** Match an owned selector alternative to the canonical slot's family. */
bool video_selection_matches(const VideoSelector& selector,
                             VideoSelectorPolicy policy) {
  if (!video_selector_is_well_formed(selector))
    return false;
  switch (policy) {
    case VideoSelectorPolicy::OptionalScalar:
    case VideoSelectorPolicy::RequiredScalar:
      return std::holds_alternative<VideoByteSelector>(selector) ||
             std::holds_alternative<VideoHalfSelector>(selector);
    case VideoSelectorPolicy::HalfSwizzle:
      return std::holds_alternative<VideoHalfSwizzle>(selector);
    case VideoSelectorPolicy::ByteSwizzle:
      return std::holds_alternative<VideoByteSwizzle>(selector);
    case VideoSelectorPolicy::HalfMask:
      return std::holds_alternative<VideoHalfMask>(selector);
    case VideoSelectorPolicy::ByteMask:
      return std::holds_alternative<VideoByteMask>(selector);
    case VideoSelectorPolicy::None:
      return false;
  }
  return false;
}

/** Require a physical scalar 32-bit integer or bit carrier when declared. */
bool video_register_is_valid(const ResolvedRegisterRef& reg) {
  if (reg.register_class != ResolvedRegisterClass::General ||
      reg.vector_width || reg.spelling == "_" ||
      (reg.symbol_id && !reg.declared_type))
    return false;
  if (!reg.declared_type)
    return true;
  const auto kind = base::scalar_kind(*reg.declared_type);
  return base::scalar_size_of(*reg.declared_type) == 4 &&
         (kind == base::ScalarKind::Signed ||
          kind == base::ScalarKind::Unsigned || kind == base::ScalarKind::Bit);
}

}  // namespace

CheckResult check_video_operands(std::span<const OperandDescriptor> descriptors,
                                 std::span<const FieldView> fields,
                                 std::span<const OperandView> operands,
                                 const Context& context) {
  if (std::ranges::none_of(
          descriptors, [](const auto& slot) { return slot.video.has_value(); }))
    return {};
  CheckDiagnostics diagnostics;
  const auto reject = [&](SourceRange range, std::string message) {
    diagnostics.push_back(
        {CheckDiagnosticKind::RuleViolation, range, std::move(message)});
  };
  std::array<const ResolvedVideoOperand*, 4> values{};
  std::array<const VideoOperandDescriptor*, 4> slots{};
  if (descriptors.size() < 3 || descriptors.size() > 4) {
    reject(context.instruction_range,
           "Video layout requires three or four operands.");
    return std::unexpected(std::move(diagnostics));
  }
  for (size_t index = 0; index < descriptors.size(); ++index) {
    const auto& descriptor = descriptors[index];
    if (!descriptor.video) {
      reject(context.instruction_range,
             "Video layout contains an untyped operand slot.");
      continue;
    }
    const auto& slot = *descriptor.video;
    slots[index] = &slot;
    const OperandView* view = nullptr;
    for (const auto& operand : operands)
      if (operand.field_id == descriptor.target_field_id) {
        view = &operand;
        break;
      }
    const SourceRange range = view && !view->locations.empty()
                                  ? view->locations.front()
                                  : context.instruction_range;
    if (!view || view->actual_shape != OperandShape::VideoOperand ||
        !view->video_operand) {
      reject(range,
             "Video operand payload is missing or has an invalid shape.");
      continue;
    }
    const auto& value = *view->video_operand;
    values[index] = &value;
    if (static_cast<size_t>(slot.position) != index ||
        (index == 0 && (descriptor.role != OperandRole::Destination ||
                        descriptor.access != OperandAccess::Write)) ||
        (index != 0 && (descriptor.role != OperandRole::Source ||
                        descriptor.access != OperandAccess::Read)))
      reject(range, "Video operand role contradicts its selected layout.");
    if ((value.negated && !slot.allow_negate) ||
        value.negated != value.minus_range.has_value() ||
        (value.minus_range && *value.minus_range == SourceRange{}))
      reject(range,
             "Video arithmetic-negation provenance is invalid for this slot.");
    if (value.selector) {
      if (!video_selection_matches(value.selector->value, slot.selector))
        reject(range,
               "Video selector family, indices, or mask order is invalid.");
    } else if (slot.selector == VideoSelectorPolicy::RequiredScalar) {
      reject(range, "Video merge destination requires a written selector.");
    }
    if (const auto* reg =
            std::get_if<ResolvedRegisterRef>(&value.value.value)) {
      const SourceRange child_range =
          value.value.locs.empty() ? SourceRange{} : value.value.locs.front();
      if (!valid_register_component(*reg) ||
          (reg->component &&
           (value.selector || reg->component->range != child_range ||
            !video_range_inside(child_range, range) ||
            (context.instruction_range != SourceRange{} &&
             !video_range_inside(child_range, context.instruction_range)))))
        reject(range, "Video component provenance is invalid.");
      if (!video_register_is_valid(*reg))
        reject(range,
               "Video registers require scalar 32-bit integer/bit carriers.");
    } else {
      const auto& immediate = std::get<ResolvedImmediate>(value.value.value);
      ScalarType expected = slot.type_use == VideoOperandTypeUse::BitCarrier
                                ? ScalarType::B32
                                : ScalarType::U32;
      if (slot.type_use == VideoOperandTypeUse::ModifierField) {
        const auto* field = video_field(fields, slot.type_field_id);
        if (!field || !field->video_type ||
            (*field->video_type != VideoType::S32 &&
             *field->video_type != VideoType::U32)) {
          reject(
              range,
              "Video immediate requires a valid typed interpretation field.");
          continue;
        }
        expected = *field->video_type == VideoType::S32 ? ScalarType::S32
                                                        : ScalarType::U32;
      }
      if (!slot.allow_immediate || slot.lanes != VideoLanes::Scalar ||
          index == 0 || value.negated || value.selector ||
          immediate.type != expected ||
          !valid_value_vector_immediate(immediate))
        reject(range,
               "Video immediate is forbidden or has inconsistent integer "
               "provenance.");
    }
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  const auto& contract = *slots[0];
  for (const auto* slot : slots)
    if (slot && (slot->lanes != contract.lanes ||
                 slot->operation != contract.operation))
      reject(context.instruction_range,
             "Video operand topology is inconsistent.");
  bool sat = false;
  bool po = false;
  const auto flag = [&](std::string_view id, bool& selected) {
    if (id.empty())
      return;
    const auto* field = video_field(fields, id);
    if (!field || !field->bool_value)
      reject(context.instruction_range,
             "Video Boolean control field is unavailable.");
    else
      selected = *field->bool_value;
  };
  flag(contract.sat_field_id, sat);
  flag(contract.po_field_id, po);
  VideoSecondaryOp secondary = VideoSecondaryOp::None;
  for (const auto& field : fields)
    if (field.video_secondary_op)
      secondary = *field.video_secondary_op;
  const bool has_c = values[3] != nullptr;
  if (contract.lanes == VideoLanes::Scalar) {
    if (contract.operation == VideoOperation::Mad) {
      if (!has_c || secondary != VideoSecondaryOp::None ||
          ((values[1]->negated != values[2]->negated) && values[3]->negated) ||
          (po &&
           (values[1]->negated || values[2]->negated || values[3]->negated)))
        reject(context.instruction_range,
               "Video mad product/c negation or po controls are invalid.");
    } else {
      const bool merged = values[0]->selector.has_value();
      if ((!has_c && (merged || secondary != VideoSecondaryOp::None)) ||
          (has_c && (merged != (secondary == VideoSecondaryOp::None))))
        reject(context.instruction_range,
               "Video plain, secondary, and merge layouts have inconsistent "
               "controls.");
    }
  } else if (contract.lanes == VideoLanes::Two ||
             contract.lanes == VideoLanes::Four) {
    if (!has_c ||
        (secondary != VideoSecondaryOp::None &&
         secondary != VideoSecondaryOp::Add) ||
        (sat && secondary == VideoSecondaryOp::Add))
      reject(context.instruction_range,
             "SIMD video requires four carriers and forbids saturation with "
             "accumulation.");
  } else {
    reject(context.instruction_range, "Video lane topology is invalid.");
  }
  if (!diagnostics.empty())
    return std::unexpected(std::move(diagnostics));
  return {};
}

}  // namespace ptx_frontend::resolved_ir::checker
