import re
from warnings import deprecated


def to_pascal_case(value: str) -> str:
    """Converts a string to PascalCase. Like "hello world" -> "HelloWorld"."""
    parts = re.split(r"[^A-Za-z0-9]+", value)

    return "".join(part[:1].upper() + part[1:] for part in parts if part)


@deprecated("Use to_file_stem instead")
def to_cpp_identifier(value: str) -> str:
    """Converts a string to a valid C++ identifier. Like "hello world" -> "hello_world"."""
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    result = re.sub(r"_+", "_", result)

    if not result:
        return "_"

    if result[0].isdigit():
        result = "_" + result

    return result


def to_file_stem(value: str) -> str:
    """Converts a string to a valid file stem. Like "hello world" -> "hello_world"."""
    result = re.sub(r"[^A-Za-z0-9]+", "_", value)
    result = re.sub(r"_+", "_", result)

    return result.strip("_").lower()


def file_stem_to_pascal_case(value: str) -> str:
    """Converts a file stem to PascalCase. Like "hello_world" -> "HelloWorld"."""
    parts = re.split(r"[^A-Za-z0-9]+", value)

    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def category_ir_header_name(category: str) -> str:
    """Returns the header file name for a given category."""
    return f"ptx_ir_{to_file_stem(category)}.gen.hpp"


def category_parser_header_name(category: str) -> str:
    """Returns the parser header file name for a given category."""
    return f"ptx_parser_{to_file_stem(category)}.gen.hpp"


def category_parser_source_name(category: str) -> str:
    """Returns the parser source file name for a given category."""
    return f"ptx_parser_{to_file_stem(category)}.gen.cpp"


def domain_parse_function_name(domain_name: str) -> str:
    """Returns the parse function name for a given domain."""
    return f"parse{to_pascal_case(domain_name)}"
