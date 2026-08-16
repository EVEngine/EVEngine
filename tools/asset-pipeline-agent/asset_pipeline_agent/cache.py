"""资源缓存管理：下载落地 + MD5 完整性校验 + 复用。

合规：缓存保留原始压缩包（含版权信息），MD5 用于避免重复下载与校验损坏。
"""

from __future__ import annotations

import hashlib
import os
import urllib.request
import urllib.error
from typing import Optional


class CacheError(RuntimeError):
    pass


def md5_of(path: str) -> str:
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class AssetCache:
    def __init__(self, cache_dir: str, keep_archives: bool = True):
        self.root = cache_dir
        self.archives = os.path.join(cache_dir, "archives")
        self.keep_archives = keep_archives
        os.makedirs(self.archives, exist_ok=True)

    def _safe_name(self, asset_id: str, ext: str) -> str:
        name = "".join(c for c in asset_id if c.isalnum() or c in "._-") or "asset"
        return f"{name}{ext}"

    def local_path(self, asset_id: str, ext: str) -> str:
        return os.path.join(self.archives, self._safe_name(asset_id, ext))

    def has(self, asset_id: str, ext: str, md5: Optional[str] = None) -> bool:
        path = self.local_path(asset_id, ext)
        if not os.path.isfile(path):
            return False
        if md5 and md5_of(path) != md5:
            return False
        return True

    def download(self, url: str, asset_id: str, ext: str,
                 md5: Optional[str] = None) -> tuple:
        """下载到缓存，返回 (本地路径, md5)。若已缓存且 MD5 匹配则直接复用。"""
        path = self.local_path(asset_id, ext)
        if os.path.isfile(path) and (not md5 or md5_of(path) == md5):
            return path, md5_of(path)
        if url.startswith("dry://"):
            # 仿真源：写一个占位文件以闭合流程。
            with open(path, "wb") as fh:
                fh.write(b"dry-run placeholder archive")
        else:
            req = urllib.request.Request(url, headers={"User-Agent": "EVEngine-asset-pipeline/0.1"})
            try:
                with urllib.request.urlopen(req, timeout=120) as resp, open(path, "wb") as out:
                    while True:
                        chunk = resp.read(1 << 20)
                        if not chunk:
                            break
                        out.write(chunk)
            except (urllib.error.HTTPError, urllib.error.URLError) as e:
                raise CacheError(f"download failed {url}: {e}") from e
        digest = md5_of(path)
        if md5 and digest != md5:
            try:
                os.remove(path)
            except OSError:
                pass
            raise CacheError(f"MD5 mismatch for {url}")
        return path, digest

    def extract_dir(self, asset_id: str) -> str:
        """为该资产准备独立工作目录（用于解压 + Blender 处理）。"""
        d = os.path.join(self.root, "work", self._safe_name(asset_id, ""))
        os.makedirs(d, exist_ok=True)
        return d
