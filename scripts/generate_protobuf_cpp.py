#!/usr/bin/env python3
"""Regenerate checked-in Protobuf C++ with the engine link-group exports."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path

import utf8_stdio


ROOT = Path(__file__).resolve().parent.parent
PROTO_GROUPS = {
    "dynamic_value.proto": "FOUNDATION",
    "authority_store.proto": "FOUNDATION",
    "decision_context.proto": "FOUNDATION",
    "game_save_session.proto": "FOUNDATION",
    "inventory_save.proto": "FOUNDATION",
    "policy_registry.proto": "FOUNDATION",
    "statepatch_store.proto": "FOUNDATION",
    "transaction_ledger.proto": "FOUNDATION",
    "rpg_actor_checkpoint.proto": "PLATFORM",
    "rpg_game_state.proto": "PLATFORM",
    "rpg_party_checkpoint.proto": "PLATFORM",
    "rpg_save_session.proto": "PLATFORM",
    "rpg_tracker.proto": "PLATFORM",
    "tactics_battle.proto": "DOMAINS",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protoc", default=shutil.which("protoc"), help="pinned Protobuf 35.1 protoc executable")
    args = parser.parse_args()
    if not args.protoc:
        parser.error("pass --protoc pointing to the pinned Protobuf 35.1 compiler")
    version = subprocess.run(
        [args.protoc, "--version"], capture_output=True, text=True, encoding="utf-8", check=True
    )
    if version.stdout.strip() != "libprotoc 35.1":
        parser.error(f"expected libprotoc 35.1, got {version.stdout.strip()}")

    output = ROOT / "proto" / "generated"
    for name, group in PROTO_GROUPS.items():
        subprocess.run(
            [
                args.protoc,
                f"--proto_path={ROOT / 'proto'}",
                f"--proto_path={ROOT / 'third-party' / 'protobuf' / 'src'}",
                f"--cpp_out=dllexport_decl=EVENGINE_API_{group}:{output}",
                name,
            ],
            check=True,
        )
        header = output / f"{Path(name).stem}.pb.h"
        source = header.read_text(encoding="utf-8")
        marker = "#include <limits>"
        if source.count(marker) != 1:
            raise RuntimeError(f"unexpected generated header layout: {header}")
        source = source.replace(marker, '#include "common/Export.h"\n\n' + marker, 1)
        with header.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write("\n".join(line.rstrip() for line in source.splitlines()) + "\n")
        generated_source = output / f"{Path(name).stem}.pb.cc"
        text = generated_source.read_text(encoding="utf-8")
        with generated_source.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write("\n".join(line.rstrip() for line in text.splitlines()) + "\n")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
