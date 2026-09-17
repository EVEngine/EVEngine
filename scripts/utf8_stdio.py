#!/usr/bin/env python3
"""Make a repository script's text streams UTF-8 on every host.

CI runs these scripts on Linux, where ``sys.stdout`` and ``sys.stderr`` are
UTF-8.  A Windows host is not automatically UTF-8: as soon as the output is
redirected -- an IDE terminal, ``> log.txt``, ``| tee``, or a tool capturing the
command -- Python encodes text with the ANSI code page from the locale, which is
cp936 (GBK) on a Chinese Windows.  Printing a diagnostic containing a character
that code page cannot represent then raises ``UnicodeEncodeError`` and turns a
checker into a traceback.

That is reachable, not theoretical: this repository is full of characters cp936
cannot encode (``↔``, ``²``, ``³``, ``⇒``, ``•``, emoji), and checkers print text
they read back out of the tree -- for example ``scripts/check_examples.py
--print-missing-rows`` prints each example README's own title.

Call :func:`enable_utf8_stdio` once from a program's entry point, never at import
time, so importing a script module stays free of side effects::

    import utf8_stdio

    ...

    if __name__ == "__main__":
        utf8_stdio.enable_utf8_stdio()
        raise SystemExit(main())

``scripts/tests/test_utf8_stdio.py`` enforces that convention for every program
under ``scripts/``, and pins the failure this module exists to remove.

Scope: this normalises the process's *text streams*.  It cannot change the
locale-derived default that ``open()`` and ``subprocess`` use for *other* text,
so code that decodes a child process or writes a data file must still pass
``encoding="utf-8"`` explicitly; the Makefile/CI paths that run unit tests use
``python3 -X utf8`` so runner-generated text is UTF-8 too.
"""

from __future__ import annotations

import sys
from typing import TextIO

# Codec error handler used by default: unlike "strict" it never raises, so a
# stray surrogate or an unforeseen character cannot abort a checker.  Python
# itself uses backslashreplace for the default sys.stderr policy.
DEFAULT_ERRORS = "backslashreplace"


def _reconfigure(stream: TextIO, errors: str) -> bool:
    """Re-encode one stream as UTF-8; return True when it was reconfigured.

    Streams that cannot be reconfigured are left untouched rather than treated
    as an error: a test may have replaced ``sys.stdout`` with a plain object,
    and a detached or already-closed stream must not turn into a second
    failure while the caller is reporting a first one.
    """
    reconfigure = getattr(stream, "reconfigure", None)
    if reconfigure is None:
        return False
    try:
        reconfigure(encoding="utf-8", errors=errors)
    except (ValueError, OSError):
        return False
    return True


def enable_utf8_stdio(errors: str = DEFAULT_ERRORS) -> None:
    """Re-encode ``sys.stdout`` and ``sys.stderr`` as UTF-8 for this process.

    Idempotent, and safe to call when the streams are redirected, wrapped or
    replaced: a stream that cannot be reconfigured is skipped silently.

    @param errors Codec error handler to install (default
        :data:`DEFAULT_ERRORS`, which never raises).
    @return None
    """
    _reconfigure(sys.stdout, errors)
    _reconfigure(sys.stderr, errors)
