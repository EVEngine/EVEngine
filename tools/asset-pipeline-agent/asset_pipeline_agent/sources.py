"""素材检索封装：对接公开合规素材 API（Poly Haven / Sketchfab）+ 干跑仿真源。

强制前置约束：
- 仅 CC0 / CC-BY 等商用免费合规授权；禁止付费资源、禁止违规爬取。
- 每个候选都携带作者 / 授权 / 来源链接，供管线永久留存版权信息。
"""

from __future__ import annotations

import json
import os
import urllib.request
import urllib.parse
import urllib.error
from typing import Any, Dict, List

from .report import ALLOWED_LICENSES, Candidate


class SourceError(RuntimeError):
    pass


def _http_json(url: str, timeout: int = 30, token: str = "") -> Any:
    headers = {"User-Agent": "EVEngine-asset-pipeline/0.1"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        raise SourceError(f"HTTP {e.code} GET {url}") from e
    except urllib.error.URLError as e:
        raise SourceError(f"GET {url}: {e.reason}") from e


class PolyHavenSource:
    """Poly Haven：全部资产 CC0，天然合规。模型以 .blend 为主。"""

    def __init__(self, cfg: Dict[str, Any]):
        self.cfg = cfg
        self.index_url = cfg.get("index_url", "https://api.polyhaven.com/assets")
        self.file_root = cfg.get("file_root", "https://dl.polyhaven.org/file/ph-assets")
        self.asset_type = cfg.get("type", "model")
        self.timeout = int(cfg.get("timeout_s", 30))

    def search(self, request) -> List[Candidate]:
        url = f"{self.index_url}?t={urllib.parse.quote(self.asset_type)}"
        data = _http_json(url, self.timeout)
        if not isinstance(data, dict):
            return []
        out: List[Candidate] = []
        query = (request.description + " " + request.style).lower()
        for asset_id, meta in data.items():
            if not isinstance(meta, dict):
                continue
            name = str(meta.get("name", asset_id))
            tri = int(meta.get("triangle_count") or 0) or 0
            if request.max_triangles and tri > request.max_triangles:
                continue
            if request.min_triangles and tri < request.min_triangles:
                continue
            score = 1.0
            # 语义相关初筛（简单关键词命中 + 始终保留候选让 LLM 精排）。
            if query and (query.split()[0] in name.lower()):
                score = 0.95
            out.append(Candidate(
                source="poly_haven",
                asset_id=asset_id,
                name=name,
                download_url=f"{self.file_root}/{asset_id}/blend/{asset_id}.blend",
                license="cc0",                      # Poly Haven 全部 CC0
                author=str(meta.get("author", {}) or {}).get("name", "") if isinstance(meta.get("author"), dict) else "",
                author_url="https://polyhaven.com",
                attribution=f"Poly Haven '{name}' (CC0 1.0) by {meta.get('author', 'unknown')} — https://polyhaven.com/a/{asset_id}",
                triangles=tri,
                style=str(meta.get("categories", []) or []),
                thumb=str(meta.get("thumbnail", "") or ""),
                score=score,
            ))
        out.sort(key=lambda c: c.score, reverse=True)
        return out


class SketchfabSource:
    """Sketchfab API v3：仅拉取合规授权（cc0/cc-by/cc-by-sa）且可下载的模型。"""

    def __init__(self, cfg: Dict[str, Any]):
        self.cfg = cfg
        self.search_url = cfg.get("search_url", "https://api.sketchfab.com/v3/search")
        self.download_url = cfg.get("download_url", "https://api.sketchfab.com/v3/models/{uid}/download")
        self.token = os.environ.get(cfg.get("token_env", "SKETCHFAB_TOKEN"), "")
        self.timeout = int(cfg.get("timeout_s", 30))

    def search(self, request) -> List[Candidate]:
        if not self.token:
            raise SourceError("SketchfabSource: SKETCHFAB_TOKEN 未设置，跳过该源")
        params = {
            "type": "models",
            "q": f"{request.description} {request.style}",
            "downloadable": "true",
            "formats": "gltf",
            "licenses": ",".join(request.license if request.license in ALLOWED_LICENSES else ["cc0"]),
            "count": 24,
        }
        url = self.search_url + "?" + urllib.parse.urlencode(params)
        data = _http_json(url, self.timeout, token=self.token)
        out: List[Candidate] = []
        for it in data.get("results", []):
            if not isinstance(it, dict):
                continue
            lic = (it.get("license") or {}).get("id") or (it.get("license") or {}).get("slug") or "cc0"
            if lic not in ALLOWED_LICENSES:
                continue  # 硬过滤：仅合规授权
            if not it.get("isDownloadable"):
                continue
            uid = str(it.get("uid", ""))
            if not uid:
                continue
            thumb = ""
            if it.get("thumbnails", {}).get("images"):
                thumb = it["thumbnails"]["images"][0].get("url", "")
            out.append(Candidate(
                source="sketchfab",
                asset_id=uid,
                name=str(it.get("name", uid)),
                download_url=self.download_url.format(uid=uid),
                license=lic,
                author=str((it.get("user") or {}).get("username", "")),
                author_url=str((it.get("user") or {}).get("profileUrl", "")),
                attribution=f"Sketchfab '{it.get('name', uid)}' ({lic}) by {(it.get('user') or {}).get('username', 'unknown')} — {(it.get('user') or {}).get('profileUrl', '')}",
                triangles=int((it.get("faceCount") or 0) or 0),
                style=str(it.get("categories", []) or []),
                thumb=thumb,
                score=float(it.get("score", 0) or 0),
            ))
        out.sort(key=lambda c: c.score, reverse=True)
        return out


class DryRunSource:
    """无网络 / 无 API Key 时的仿真源，用于验证闭环与测试。"""

    def __init__(self, cfg: Dict[str, Any]):
        self.cfg = cfg

    def search(self, request) -> List[Candidate]:
        out: List[Candidate] = []
        for i in range(3):
            tri = 1000 * (i + 1)
            if request.max_triangles and tri > request.max_triangles:
                continue
            if request.min_triangles and tri < request.min_triangles:
                continue
            out.append(Candidate(
                source="dry_run",
                asset_id=f"dry_{request.purpose}_{i}",
                name=f"dry_{request.purpose}_{i}",
                download_url=f"dry://asset/{request.purpose}_{i}",
                license="cc0",
                author="dry-run author",
                author_url="https://example.test/attribution",
                attribution=f"Dry-run {i} (CC0) — demo attribution for compliance testing",
                triangles=tri,
                style=request.style,
                score=1.0 - i * 0.1,
                reason=f"dry candidate #{i}",
            ))
        return out


def build_sources(cfg) -> List[Any]:
    """按配置构建可用检索源（含合规硬过滤层）。"""
    sources: List[Any] = []
    for name, spec in (cfg["sources"] or {}).items():
        if not spec.get("enabled"):
            continue
        if name == "poly_haven":
            sources.append(PolyHavenSource(spec))
        elif name == "sketchfab":
            sources.append(SketchfabSource(spec))
        elif name == "dry_run":
            sources.append(DryRunSource(spec))
    return sources
