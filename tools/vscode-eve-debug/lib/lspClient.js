// @ts-check
'use strict';

const { spawn } = require('child_process');

/**
 * Minimal LSP JSON-RPC client over stdio (Content-Length framing).
 * No vscode-languageclient dependency.
 */
class LspClient {
  /**
   * @param {string} command
   * @param {string[]} args
   * @param {{ cwd?: string, env?: NodeJS.ProcessEnv, onStderr?: (text: string) => void, onLog?: (line: string) => void }} [options]
   */
  constructor(command, args, options = {}) {
    this.child = spawn(command, args, {
      cwd: options.cwd,
      env: options.env || process.env,
      stdio: ['pipe', 'pipe', 'pipe'],
      windowsHide: true,
    });
    this.buf = Buffer.alloc(0);
    this.nextId = 1;
    /** @type {Map<number, { resolve: (value: any) => void, reject: (err: Error) => void }>} */
    this.pending = new Map();
    /** @type {Map<string, (params: any) => void>} */
    this.handlers = new Map();
    this.onLog = options.onLog;
    this.child.stdout.on('data', (chunk) => this._onData(chunk));
    if (options.onStderr) {
      this.child.stderr.on('data', (chunk) => options.onStderr(chunk.toString()));
    }
    this.exit = new Promise((resolve) => {
      this.child.on('exit', (code, signal) => resolve({ code, signal }));
    });
    this.child.on('error', (err) => {
      for (const pending of this.pending.values()) pending.reject(err);
      this.pending.clear();
    });
  }

  /**
   * @param {string} method
   * @param {(params: any) => void} handler
   */
  on(method, handler) {
    this.handlers.set(method, handler);
  }

  /**
   * @param {string} method
   * @param {any} [params]
   */
  notify(method, params) {
    const message = { jsonrpc: '2.0', method };
    if (params !== undefined) message.params = params;
    this._send(message);
  }

  /**
   * @param {string} method
   * @param {any} [params]
   * @param {number} [timeoutMs]
   */
  request(method, params, timeoutMs = 15000) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`LSP ${method} timed out after ${timeoutMs}ms`));
      }, timeoutMs);
      this.pending.set(id, {
        resolve: (value) => {
          clearTimeout(timer);
          resolve(value);
        },
        reject: (err) => {
          clearTimeout(timer);
          reject(err);
        },
      });
      const message = { jsonrpc: '2.0', id, method };
      if (params !== undefined) message.params = params;
      this._send(message);
    });
  }

  async shutdown() {
    try {
      await this.request('shutdown', undefined, 3000);
    } catch (_) {
      /* process may already be gone */
    }
    this.notify('exit');
    if (this.child.stdin && !this.child.stdin.destroyed) {
      try {
        this.child.stdin.end();
      } catch (_) {
        /* ignore */
      }
    }
    const child = this.child;
    await Promise.race([
      this.exit,
      new Promise((resolve) => setTimeout(resolve, 1500)),
    ]);
    if (child.exitCode === null && !child.killed) {
      try {
        child.kill();
      } catch (_) {
        /* ignore */
      }
    }
  }

  /**
   * @param {object} message
   */
  _send(message) {
    if (this.onLog) this.onLog('→ ' + JSON.stringify(message));
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'utf8');
    if (!this.child.stdin || this.child.stdin.destroyed) return;
    this.child.stdin.write(Buffer.concat([header, body]));
  }

  /**
   * @param {Buffer} chunk
   */
  _onData(chunk) {
    this.buf = Buffer.concat([this.buf, chunk]);
    while (true) {
      const msg = this._tryRead();
      if (!msg) break;
      this._dispatch(msg);
    }
  }

  _tryRead() {
    const headerEnd = this.buf.indexOf('\r\n\r\n');
    if (headerEnd < 0) return null;
    const header = this.buf.slice(0, headerEnd).toString('utf8');
    const match = /Content-Length:\s*(\d+)/i.exec(header);
    if (!match) {
      this.buf = this.buf.slice(headerEnd + 4);
      return null;
    }
    const length = Number(match[1]);
    const start = headerEnd + 4;
    if (this.buf.length < start + length) return null;
    const json = this.buf.slice(start, start + length).toString('utf8');
    this.buf = this.buf.slice(start + length);
    return JSON.parse(json);
  }

  /**
   * @param {any} msg
   */
  _dispatch(msg) {
    if (this.onLog) this.onLog('← ' + JSON.stringify(msg));
    if (msg && msg.id !== undefined && msg.method === undefined) {
      const pending = this.pending.get(msg.id);
      if (!pending) return;
      this.pending.delete(msg.id);
      if (msg.error) {
        pending.reject(new Error(msg.error.message || 'LSP error'));
      } else {
        pending.resolve(msg.result);
      }
      return;
    }
    if (msg && msg.method) {
      const handler = this.handlers.get(msg.method);
      if (handler) handler(msg.params);
    }
  }
}

module.exports = { LspClient };
