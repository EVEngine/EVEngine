"""Output-constraint schema for the pre-filter model.

We enforce strict JSON output via llama.cpp's ``json_schema`` field on
``/v1/chat/completions``. Unlike a hand-written GBNF grammar, the JSON Schema
path is robust to the double-quote escaping across the HTTP transport and to
multi-byte (Chinese) enum values, and it still constrains the model at the
token-sampling level so free-text / markdown is impossible.

This schema is the ONLY shape the model may emit.
"""

JSON_SCHEMA = {
    "type": "object",
    "properties": {
        "risk_score": {"type": "integer", "minimum": 0, "maximum": 3},
        "has_problem": {"type": "boolean"},
        "problem_regions": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "bbox": {
                        "type": "array",
                        "items": {"type": "integer"},
                        "minItems": 4,
                        "maxItems": 4,
                    },
                    "type": {
                        "type": "string",
                        "enum": ["遮挡", "过密", "空旷", "穿插", "道路遮挡", "植被扎堆"],
                    },
                    "note": {"type": "string"},
                },
                "required": ["bbox", "type"],
                "additionalProperties": False,
            },
        },
        "need_high_precision_review": {"type": "boolean"},
    },
    "required": [
        "risk_score",
        "has_problem",
        "problem_regions",
        "need_high_precision_review",
    ],
    "additionalProperties": False,
}
