#include <gtest/gtest.h>

#include <ptx_frontend/semantic/ptx_call_argument_compatibility.hpp>

namespace ptx_frontend::call_argument_compatibility {
namespace {

using Compatibility = CallArgumentCompatibility;
using StateSpace = CallArgumentStateSpace;

CallArgumentProperties scalar(StateSpace state_space = StateSpace::Register) {
  return {.state_space = state_space, .type_spelling = ".u32"};
}

TEST(CallArgumentCompatibility, AcceptsCompatibleScalarAndVectorValues) {
  auto formal = scalar();
  auto actual = scalar(StateSpace::Parameter);
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);

  formal.type_spelling = ".v4 .u32";
  actual.type_spelling = ".v4 .u32";
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);
}

TEST(CallArgumentCompatibility, ReportsUnsupportedCallStateSpaces) {
  auto formal = scalar(StateSpace::Local);
  const auto actual = scalar();
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::FormalStateSpaceMismatch);

  EXPECT_EQ(checkCallArgumentCompatibility(CallArgumentProperties{}, actual),
            Compatibility::FormalStateSpaceMismatch);

  formal = scalar();
  EXPECT_EQ(checkCallArgumentCompatibility(formal, scalar(StateSpace::Global)),
            Compatibility::ActualStateSpaceMismatch);
}

TEST(CallArgumentCompatibility, DefaultsPointerAlignmentToFourBytes) {
  EXPECT_EQ(PointerProperties{}.pointed_alignment, 4u);
}

TEST(CallArgumentCompatibility, ReportsTypeAndArrayMismatches) {
  const auto formal = scalar();
  auto actual = scalar();
  actual.type_spelling = ".u64";
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::TypeMismatch);

  actual = scalar();
  actual.is_array = true;
  actual.array_size = 4;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::ArrayMismatch);
}

TEST(CallArgumentCompatibility, ChecksParameterByteArrays) {
  CallArgumentProperties formal{
      .state_space = StateSpace::Parameter,
      .type_spelling = ".b8",
      .array_alignment = 16,
      .is_array = true,
      .array_size = 8,
  };
  auto actual = formal;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);

  actual.state_space = StateSpace::Register;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::ActualStateSpaceMismatch);

  actual = formal;
  actual.array_size = 4;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::ArraySizeMismatch);

  actual = formal;
  actual.array_alignment = 8;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::AlignmentMismatch);
}

TEST(CallArgumentCompatibility, AcceptsSizedActualForUnsizedArrayFormal) {
  const CallArgumentProperties formal{
      .state_space = StateSpace::Parameter,
      .type_speling = ".b8",
      .array_alignment = 8,
      .is_array = true,
  };
  auto actual = formal;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::ArraySizeMismatch);

  actual.array_size = 32;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);
}

TEST(CallArgumentCompatibility, ChecksPointerContractAsymmetrically) {
  auto formal = scalar();
  auto actual = formal;
  formal.pointer = PointerProperties{
      .pointed_state_space = PointedStateSpace::Global,
      .pointed_alignment = 16,
  };
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::PointerMismatch);

  actual.pointer = PointerProperties{
      .pointed_state_space = PointedStateSpace::Shared,
      .pointed_alignment = 32,
  };
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::PointedStateSpaceMismatch);

  actual.pointer = PointerProperties{
      .pointed_state_space = PointedStateSpace::Global,
      .pointed_alignment = 8,
  };
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::PointedAlignmentMismatch);

  actual.pointer->pointed_alignment = 32;
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);
}

TEST(CallArgumentCompatibility, GenericFormalPointerAcceptsConcreteActualSpace) {
  auto formal = scalar();
  auto actual = formal;
  formal.pointer = PointerProperties{.pointed_alignment = 8};
  actual.pointer = PointerProperties{
      .pointed_state_space = PointedStateSpace::Global,
      .pointed_alignment = 8,
  };
  EXPECT_EQ(checkCallArgumentCompatibility(formal, actual),
            Compatibility::Compatible);
}

}  // namespace
}  // namespace ptx_frontend::call_argument_compatibility
