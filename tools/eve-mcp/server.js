#!/usr/bin/env node
/**
 * stdio MCP ↔ TCP bridge for EVEngine, with optional lifecycle spawn.
 *
 * Cursor / Claude Desktop speak MCP over stdio (newline-delimited JSON-RPC).
 * The engine embeds the same JSON-RPC on TCP when launched with:
 *   eve run --debug --mcp-port=7529 .
 *
 * Config example (Cursor mcp.json):
 * {
 *   "mcpServers": {
 *     "evengine": {
 *       "command": "node",
 *       "args": ["tools/eve-mcp/server.js"],
 *       "env": {
 *         "EVE_MCP_HOST": "127.0.0.1",
 *         "EVE_MCP_PORT": "7529",
 *         "EVE_MCP_AUTO_START": "1",
 *         "EVE_MCP_EVE_BIN": "build/linux-debug/src/engine/eve",
 *         "EVE_MCP_GAME": "examples/basic"
 *       }
 *     }
 *   }
 * }
 *
 * Env filters forwarded to the engine process when auto-starting:
 *   EVE_MCP_TOOLSET, EVE_MCP_READONLY, EVE_MCP_EXCLUDE
 */

'use strict';

const net = require('net');
const path = require('path');
const readline = require('readline');
const { spawn } = require('child_process');

const HOST = process.env.EVE_MCP_HOST || '127.0.0.1';
const PORT = Number(process.env.EVE_MCP_PORT || '7529');
const CONNECT_TIMEOUT_MS = Number(process.env.EVE_MCP_CONNECT_TIMEOUT_MS || '5000');
const AUTO_START = /^(1|true|yes|on)$/i.test(String(process.env.EVE_MCP_AUTO_START || ''));
const EVE_BIN = process.env.EVE_MCP_EVE_BIN || process.env.EVE_BIN || 'eve';
const GAME = process.env.EVE_MCP_GAME || '.';
const SPAWN_WAIT_MS = Number(process.env.EVE_MCP_SPAWN_WAIT_MS || '60000');
const EXTRA_ARGS = (process.env.EVE_MCP_EVE_ARGS || '')
  .split(/\s+/)
  .map((s) => s.trim())
  .filter(Boolean);

let child = null;
let shuttingDown = false;

function log(...args) {
  process.stderr.write(args.map(String).join(' ') + '\n');
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function connectOnce() {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host: HOST, port: PORT });
    const timer = setTimeout(() => {
      sock.destroy();
      reject(new Error(`timeout connecting to ${HOST}:${PORT}`));
    }, CONNECT_TIMEOUT_MS);
    sock.once('connect', () => {
      clearTimeout(timer);
      sock.setEncoding('utf8');
      resolve(sock);
    });
    sock.once('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

async function connectWithRetry(deadlineMs) {
  const deadline = Date.now() + deadlineMs;
  let lastErr = null;
  while (Date.now() < deadline) {
    try {
      return await connectOnce();
    } catch (err) {
      lastErr = err;
      await sleep(200);
    }
  }
  throw lastErr || new Error(`cannot connect to ${HOST}:${PORT}`);
}

function stopChild() {
  if (!child || child.killed) return;
  try {
    child.kill('SIGTERM');
  } catch (_) {
    /* ignore */
  }
  const proc = child;
  setTimeout(() => {
    try {
      if (!proc.killed) proc.kill('SIGKILL');
    } catch (_) {
      /* ignore */
    }
  }, 2000).unref?.();
}

function startEngine() {
  const args = ['run', '--debug', `--mcp-port=${PORT}`, ...EXTRA_ARGS, GAME];
  log('eve-mcp: auto-starting', EVE_BIN, args.join(' '));
  const env = { ...process.env };
  // Keep tool filters consistent between bridge docs and the engine process.
  child = spawn(EVE_BIN, args, {
    env,
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  });
  child.stdout.on('data', (chunk) => {
    String(chunk)
      .split(/\r?\n/)
      .filter(Boolean)
      .forEach((line) => log('eve:', line));
  });
  child.stderr.on('data', (chunk) => {
    String(chunk)
      .split(/\r?\n/)
      .filter(Boolean)
      .forEach((line) => log('eve:', line));
  });
  child.on('exit', (code, signal) => {
    log('eve-mcp: engine exited', code != null ? `code=${code}` : `signal=${signal}`);
    child = null;
    if (!shuttingDown) process.exit(code || 1);
  });
  child.on('error', (err) => {
    log('eve-mcp: failed to spawn engine:', err.message);
    if (!shuttingDown) process.exit(1);
  });
}

function bridge(sock) {
  let engineBuf = '';
  sock.on('data', (chunk) => {
    engineBuf += chunk;
    let nl;
    while ((nl = engineBuf.indexOf('\n')) >= 0) {
      const line = engineBuf.slice(0, nl);
      engineBuf = engineBuf.slice(nl + 1);
      if (!line.trim()) continue;
      process.stdout.write(line + '\n');
    }
  });
  sock.on('error', (err) => log('eve-mcp: socket error', err.message));
  sock.on('close', () => {
    log('eve-mcp: engine disconnected');
    shutdown(0);
  });

  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  rl.on('line', (line) => {
    if (!line.trim()) return;
    sock.write(line + '\n');
  });
  rl.on('close', () => {
    sock.end();
    shutdown(0);
  });

  log(`eve-mcp: bridged stdio ↔ ${HOST}:${PORT}` + (child ? ' (auto-started)' : ''));
}

function shutdown(code) {
  if (shuttingDown) return;
  shuttingDown = true;
  stopChild();
  process.exit(code);
}

process.on('SIGINT', () => shutdown(0));
process.on('SIGTERM', () => shutdown(0));

async function main() {
  let sock;
  try {
    sock = await connectOnce();
  } catch (err) {
    if (!AUTO_START) {
      log(
        'eve-mcp: cannot reach EVEngine MCP at',
        `${HOST}:${PORT}:`,
        err.message,
        '\nStart the game with: eve run --debug --mcp-port=' + PORT,
        GAME,
        '\nor set EVE_MCP_AUTO_START=1 with EVE_MCP_EVE_BIN / EVE_MCP_GAME'
      );
      process.exit(1);
    }
    startEngine();
    try {
      sock = await connectWithRetry(SPAWN_WAIT_MS);
    } catch (spawnErr) {
      log(
        'eve-mcp: engine did not open MCP on',
        `${HOST}:${PORT}:`,
        spawnErr.message,
        '\nbinary:',
        path.resolve(EVE_BIN),
        'game:',
        GAME
      );
      shutdown(1);
      return;
    }
  }

  bridge(sock);
}

main().catch((err) => {
  log('eve-mcp: fatal', err);
  shutdown(1);
});
