"""Pure C++ literal spelling helpers, independent of rendering and semantics."""


def cpp_string(value: str) -> str:
    """Quote descriptor text using its established slash-and-quote contract."""
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
