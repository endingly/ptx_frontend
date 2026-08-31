#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>

namespace ptx_frontend::call_argument_compatibility {
namespace {

bool isCallStateSpace(CallArgumentStateSpace state_space) {
  return state_space == CallArgumentStateSpace::Register ||
         state_space == CallArgumentStateSpace::Parameter;
}

}  // namespace

CallArgumentCompatibility checkCallArgumentCompatibility(
    const CallArgumentProperties& formal, const CallArgumentProperties& actual) {
  if (!isCallStateSpace(formal.state_space) ||
      (formal.is_array &&
       formal.state_space != CallArgumentStateSpace::Parameter)) {
    return CallArgumentCompatibility::FormalStateSpaceMismatch;
  }
  if (!isCallStateSpace(actual.state_space) ||
      (formal.is_array &&
       actual.state_space != CallArgumentStateSpace::Parameter)) {
    return CallArgumentCompatibility::ActualStateSpaceMismatch;
  }
  if (formal.type_spelling != actual.type_spelling)
    return CallArgumentCompatibility::TypeMismatch;
  if (formal.is_array != actual.is_array)
    return CallArgumentCompatibility::ArrayMismatch;
  if (formal.is_array) {
    if (!actual.array_size)
      return CallArgumentCompatibility::ArraySizeMismatch;
    if (formal.array_size && formal.array_size != actual.array_size)
      return CallArgumentCompatibility::ArraySizeMismatch;
    if (formal.array_alignment != actual.array_alignment)
      return CallArgumentCompatibility::AlignmentMismatch;
  }
  if (formal.pointer.has_value() != actual.pointer.has_value())
    return CallArgumentCompatibility::PointerMismatch;
  if (!formal.pointer)
    return CallArgumentCompatibility::Compatible;

  if (formal.pointer->pointed_state_space &&
      formal.pointer->pointed_state_space !=
          actual.pointer->pointed_state_space) {
    return CallArgumentCompatibility::PointedStateSpaceMismatch;
  }
  if (actual.pointer->pointed_alignment < formal.pointer->pointed_alignment)
    return CallArgumentCompatibility::PointedAlignmentMismatch;
  return CallArgumentCompatibility::Compatible;
}

}  // namespace ptx_frontend::call_argument_compatibility
