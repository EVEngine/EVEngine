"""Shared lexical masking for source-derived build declarations."""

import re

# Same-length lexical mask: comments and literals cannot invent test declarations
# or upset the brace matching used to separate cases from adjacent helpers.
LEXEME = re.compile(
    r'//[^\n]*|/\*.*?\*/|R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|(?<![A-Za-z0-9_])(?:u8|[uUL])?\'(?:\\.|[^\'\\\r\n])*\'', re.DOTALL
)
def mask_cpp(text: str) -> str:
    """Keep source positions/newlines while hiding comments and literal contents."""
    return LEXEME.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), text)


