// @ts-check
'use strict';

const fs = require('fs');
const path = require('path');

/**
 * Resolve the eve executable from config or common build trees.
 * @param {string | undefined} configured
 * @param {string | undefined} workspaceRoot
 * @returns {string}
 */
function resolveEvePath(configured, workspaceRoot) {
  if (configured && configured !== 'eve' && fs.existsSync(configured)) {
    return configured;
  }
  if (!workspaceRoot) {
    return configured || 'eve';
  }

  const candidates = [];
  if (process.platform === 'darwin') {
    candidates.push(
      path.join(workspaceRoot, 'build/macosx-debug/src/engine/eve'),
      path.join(workspaceRoot, 'build/macosx/src/engine/eve')
    );
  } else if (process.platform === 'win32') {
    candidates.push(
      path.join(workspaceRoot, 'build/win32-debug/src/engine/Debug/eve.exe'),
      path.join(workspaceRoot, 'build/win32-debug/src/engine/eve.exe'),
      path.join(workspaceRoot, 'build/win32/src/engine/Release/eve.exe'),
      path.join(workspaceRoot, 'build/win32/src/engine/eve.exe')
    );
  } else {
    candidates.push(
      path.join(workspaceRoot, 'build/linux-debug/src/engine/eve'),
      path.join(workspaceRoot, 'build/linux/src/engine/eve')
    );
  }

  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) return candidate;
  }
  return configured || 'eve';
}

/**
 * Walk up from a .nut file (or directory) until `config.nut` is found.
 * @param {string} startPath
 * @param {string | undefined} fallback
 * @returns {string}
 */
function findProjectRoot(startPath, fallback) {
  let dir = startPath;
  try {
    if (!fs.statSync(dir).isDirectory()) dir = path.dirname(dir);
  } catch (_) {
    dir = path.dirname(dir);
  }
  while (true) {
    if (fs.existsSync(path.join(dir, 'config.nut'))) return dir;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return fallback || path.dirname(startPath);
}

module.exports = { resolveEvePath, findProjectRoot };
