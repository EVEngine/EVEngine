"""story-scene-agent CLI entrypoint."""

from __future__ import annotations

import sys

from .orchestrator import main

if __name__ == "__main__":
    sys.exit(main())