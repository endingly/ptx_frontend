# Installed TCGEN synchronization consumer

Configure this directory against a freshly installed `ptx_frontend` package.
The executable parses a complete module, resolves it, destroys the AST, then
checks the owned commit/fence identities and validates the module again.
