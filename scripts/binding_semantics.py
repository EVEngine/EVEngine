"""Validate engine-owned source semantic roles against the actual binding catalogue."""
import json
import re


def attach_semantics(contracts, sources, root, files, mask_source):
    roles = {"scene_ref", "scene_define", "scene_write", "event_log", "event_read",
             "event_emit", "event_type", "event_subject", "quest_read", "quest_complete", "quest_schema"}
    known = {contract.key: contract for contract in contracts}
    # Constructor names come from native registration, never from capitalization
    # guesses or project annotations. Ambiguous module factories remain unknown.
    registrations = {}
    for path, text in sources.items():
        if not path.is_relative_to(root / "src/modules"):
            continue
        module = path.relative_to(root / "src/modules").parts[0]
        code = mask_source(text)
        names = re.findall(r"(?m)^\s*Module_REG\s*\(\s*([A-Za-z_]\w*)\s*\)\s*;\s*$", code)
        names += re.findall(r'\bModuleRegister\s+\w+\s*\(\s*"([A-Za-z_]\w*)"', code)
        registrations.setdefault(module, set()).update(names)
    for contract in contracts:
        names = registrations.get(contract.module, set())
        if len(names) == 1:
            contract.semantics = dict(factory=next(iter(names)), role="", resultClass="", identityArgument=-1, relatedArgument=-1)
    for path in files:
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("schemaVersion") != 1 or not isinstance(data.get("bindings"), dict):
            raise ValueError(f"Invalid binding semantics: {path}")
        module = path.parent.name
        factory = data.get("factory", "")
        if not isinstance(factory, str) or not re.fullmatch(r"[A-Za-z_]\w*", factory):
            raise ValueError("Invalid semantic factory")
        source = "\n".join(mask_source(text) for file, text in sources.items()
                           if file.is_relative_to(root / "src/modules" / module))
        if not (re.search(r"\bModule_REG\s*\(\s*" + re.escape(factory) + r"\s*\)", source)
                or re.search(r'\bModuleRegister\s+\w+\s*\(\s*"' + re.escape(factory) + '"', source)):
            raise ValueError(f"Semantic factory has no native registration: {factory}")
        enabled = any(contract.module == module for contract in contracts)
        for contract in contracts:
            if contract.module == module:
                contract.semantics = dict(factory=factory, role="", resultClass="", identityArgument=-1, relatedArgument=-1)
        for name, rule in data["bindings"].items():
            key = f"{module}/{name}"
            if enabled and key not in known:
                raise ValueError(f"Semantic binding is absent: {key}")
            if rule.get("role") not in roles:
                raise ValueError(f"Unknown semantic role: {key}")
            value = dict(factory=factory, role=rule["role"], resultClass=rule.get("resultClass", ""),
                         identityArgument=rule.get("identityArgument", -1), relatedArgument=rule.get("relatedArgument", -1))
            if "schema" in rule:
                schema = rule["schema"]
                if (not isinstance(schema, dict) or not isinstance(schema.get("kind"), str)
                        or not isinstance(schema.get("identityField"), str) or not isinstance(schema.get("fields"), dict)
                        or not schema["kind"] or not schema["identityField"]
                        or any(not isinstance(field, str) or kind not in {"string", "array", "object", "integer", "boolean", "number"}
                               for field, kind in schema["fields"].items())):
                    raise ValueError(f"Invalid semantic source schema: {key}")
                value["schema"] = schema
            if not isinstance(value["resultClass"], str):
                raise ValueError(f"Invalid semantic return class: {key}")
            for field in ("identityArgument", "relatedArgument"):
                index = value[field]
                if type(index) is not int or index < -1 or (enabled and index >= len(known[key].parameters)):
                    raise ValueError(f"Invalid semantic argument: {key}/{field}")
                if enabled and index >= 0 and known[key].parameters[index].script_type != "string":
                    raise ValueError(f"Semantic identity argument must be string: {key}/{field}")
            if enabled:
                if value["resultClass"] and not any(c.script_class == value["resultClass"] for c in contracts):
                    raise ValueError(f"Semantic return class is absent: {key}")
                known[key].semantics = value
