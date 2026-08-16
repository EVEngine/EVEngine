"""Async resource-agent protocol client.

The full protocol is documented in ``docs/RESOURCE_AGENT_PROTOCOL.md`` (the
source of truth / deliverable). This module is the runtime client stub: it
enqueues asset-fetch requests for the resource agent and reports their status,
persisting the queue to a JSON file so the resource agent can consume it
out-of-process and asynchronously.

Design mirrors the documented protocol:
  - ``resource_request`` {request_id, asset_id, style, requester, created_at}
  - status lifecycle: queued -> fetching -> ready | failed
"""

from __future__ import annotations

import json
import os
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

DEFAULT_QUEUE = Path(__file__).resolve().parent.parent / "data" / "resource_requests.json"


def _env_queue() -> Path:
    return Path(os.environ.get("EVE_CB_RESOURCE_QUEUE", str(DEFAULT_QUEUE)))


@dataclass
class ResourceRequest:
    request_id: str
    asset_id: str
    requester: str = "creative-brain"
    style: str = "generic"
    status: str = "queued"
    created_at: float = field(default_factory=time.time)

    def to_dict(self) -> dict:
        return {
            "request_id": self.request_id,
            "asset_id": self.asset_id,
            "requester": self.requester,
            "style": self.style,
            "status": self.status,
            "created_at": self.created_at,
        }


class ResourceBroker:
    """Persistent, file-backed async request queue for the resource agent."""

    def __init__(self, queue_path: Optional[Path] = None):
        self.queue_path = queue_path or _env_queue()
        self.queue_path.parent.mkdir(parents=True, exist_ok=True)
        self.requests: Dict[str, ResourceRequest] = {}
        self._load()

    def _load(self) -> None:
        if self.queue_path.exists():
            try:
                with open(self.queue_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                for r in data:
                    req = ResourceRequest(**r)
                    self.requests[req.request_id] = req
            except (json.JSONDecodeError, OSError, TypeError):
                self.requests = {}

    def _save(self) -> None:
        with open(self.queue_path, "w", encoding="utf-8") as f:
            json.dump(
                [r.to_dict() for r in self.requests.values()],
                f,
                indent=2,
                ensure_ascii=False,
            )

    def request(self, asset_id: str, style: str = "generic", requester: str = "creative-brain") -> ResourceRequest:
        rid = uuid.uuid4().hex[:12]
        req = ResourceRequest(
            request_id=rid, asset_id=asset_id, style=style, requester=requester
        )
        self.requests[rid] = req
        self._save()
        return req

    def status(self, request_id: str) -> Optional[str]:
        req = self.requests.get(request_id)
        return req.status if req else None

    def pending(self) -> List[ResourceRequest]:
        return [r for r in self.requests.values() if r.status in ("queued", "fetching")]

    def mark(self, request_id: str, status: str) -> None:
        if request_id in self.requests:
            self.requests[request_id].status = status
            self._save()

    def import_json(self, payload: dict) -> str:
        """Ingest a resource-agent ``resource_response`` (see protocol doc)."""
        rid = payload.get("request_id")
        if rid and rid in self.requests:
            status = "ready" if payload.get("ok") else "failed"
            self.mark(rid, status)
            return rid
        return ""
