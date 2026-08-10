#!/usr/bin/env node
/**
 * stdio MCP ↔ TCP bridge for EVEngine.
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
 *       "env": { "EVE_MCP_HOST": "127.0.0.1", "EVE_MCP_PORT": "7529" }
 *     }
 *   }
 * }
 */

'use strict';

const net = require('net');
const readline = require('readline');

const HOST = process.env.EVE_MCP_HOST || '127.0.0.1';
const PORT = Number(process.env.EVE_MCP_PORT || '7529');
const CONNECT_TIMEOUT_MS = Number(process.env.EVE_MCP_CONNECT_TIMEOUT_MS || '5000');

function log(...args) {
  process.stderr.write(args.map(String).join(' ') + '\n');
}

function connect() {
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

async function main() {
  let sock;
  try {
    sock = await connect();
  } catch (err) {
    log(
      'eve-mcp: cannot reach EVEngine MCP at',
      `${HOST}:${PORT}:`,
      err.message,
      '\nStart the game with: eve run --debug --mcp-port=' + PORT,
      '.'
    );
    process.exit(1);
  }

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
    process.exit(0);
  });

  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  rl.on('line', (line) => {
    if (!line.trim()) return;
    sock.write(line + '\n');
  });
  rl.on('close', () => {
    sock.end();
  });

  log(`eve-mcp: bridged stdio ↔ ${HOST}:${PORT}`);
}

main().catch((err) => {
  log('eve-mcp: fatal', err);
  process.exit(1);
});
