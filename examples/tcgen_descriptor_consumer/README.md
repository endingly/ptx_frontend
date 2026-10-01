# Installed TCGEN descriptor consumer

This sample links only the installed resolved-IR package. It resolves a copy,
destroys its syntax AST, and checks the borrowed opaque source view against the
still-owned instruction. Separately, it supplies literal descriptor words to the
pure defined-field and layout APIs. Those words are caller knowledge; the
frontend does not prove that the copy source register holds them at runtime.

After installing the library, configure this directory with the install prefix
in `CMAKE_PREFIX_PATH`, build `tcgen_descriptor_consumer`, and run the binary.
The binary returns nonzero when an installed API or lifetime contract fails.
