"""GBNF grammar that forces the pre-filter model to emit ONLY the standard JSON.

llama.cpp's ``--grammar`` works at the token-sampling level: the model can only
produce tokens that keep the stream matching this grammar, so free text or
markdown is impossible. This is much stronger than a prompt-based constraint.

Reference: https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md
"""

GBNF_JSON_SCHEMA = r"""
# Force exactly one JSON object, no surrounding prose or code fence.
root              ::= "{" ws
                      "\"risk_score\"" ws ":" ws int0to3 ws "," ws
                      "\"has_problem\"" ws ":" ws bool ws "," ws
                      "\"problem_regions\"" ws ":" ws array ws "," ws
                      "\"need_high_precision_review\"" ws ":" ws bool ws
                      "}" ws

int0to3           ::= [0-3]
bool              ::= "true" | "false"

# problem_regions is an array of region objects; may be empty.
array             ::= "[" ws "]" | "[" ws region ("," ws region)* ws "]"

region            ::= "{" ws
                      "\"bbox\"" ws ":" ws bbox ws "," ws
                      "\"type\"" ws ":" ws ptype ws
                      ("," ws "\"note\"" ws ":" ws notestr)?
                      ws "}"

bbox              ::= "[" ws integer ws "," ws integer ws ","
                      ws integer ws "," ws integer ws "]"

# Recognised problem type values (Chinese, exactly one of these).
ptype             ::= "\"" "道路遮挡" "\""
                    | "\"" "植被扎堆" "\""
                    | "\"" "遮挡" "\""
                    | "\"" "过密" "\""
                    | "\"" "空旷" "\""
                    | "\"" "穿插" "\""

# Arbitrary short note text inside double quotes.
notestr           ::= "\"" [^"\\\n] (( "\\" [^] ) [^"\\\n])* "\""

integer           ::= [0-9]+
ws                ::= [ \t\n]*
"""
