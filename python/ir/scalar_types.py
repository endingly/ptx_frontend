"""Canonical PTX scalar-type registry shared by all IR generators."""

from __future__ import annotations


SCALAR_TYPE_CPP_ENUM_NAMES: dict[str, str] = {
    "u8": "U8",
    "u8x4": "U8x4",
    "u16": "U16",
    "u16x2": "U16x2",
    "u32": "U32",
    "u64": "U64",
    "s8": "S8",
    "s8x4": "S8x4",
    "s16": "S16",
    "s16x2": "S16x2",
    "s32": "S32",
    "s64": "S64",
    "b8": "B8",
    "b16": "B16",
    "b32": "B32",
    "b64": "B64",
    "b128": "B128",
    "f16": "F16",
    "f16x2": "F16x2",
    "f32": "F32",
    "f32x2": "F32x2",
    "f64": "F64",
    "bf16": "BF16",
    "bf16x2": "BF16x2",
    "e4m3x2": "E4m3x2",
    "e5m2x2": "E5m2x2",
    "pred": "Pred",
    "tf32": "TF32",
    "e4m3": "E4m3",
    "e5m2": "E5m2",
}
