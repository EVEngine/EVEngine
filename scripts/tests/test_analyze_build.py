import importlib.util
import sys
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "analyze_build.py"
SPEC = importlib.util.spec_from_file_location("analyze_build", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def test_reports_header_fanout_templates_and_objects(tmp_path):
    source = tmp_path / "repo"
    engine = source / "src" / "engine"
    build = source / "build" / "linux-debug"
    (engine / "common").mkdir(parents=True)
    build.mkdir(parents=True)

    (engine / "common" / "Box.h").write_text(
        "#pragma once\ntemplate <class T> struct Box { T value; };\n", encoding="utf-8"
    )
    (engine / "One.cpp").write_text('#include "common/Box.h"\n', encoding="utf-8")
    (engine / "Two.cpp").write_text('#include "common/Box.h"\n', encoding="utf-8")
    (build / "One.cpp.o").write_bytes(b"x" * 1024)

    headers = MODULE.direct_header_metrics(source)
    objects = MODULE.object_metrics(build)

    assert headers[0].path == "src/engine/common/Box.h"
    assert headers[0].fanout == 2
    assert headers[0].templates == 1
    assert objects[0][0] == 1024
