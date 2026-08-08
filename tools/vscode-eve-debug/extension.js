// @ts-check
'use strict';

const vscode = require('vscode');
const { spawn } = require('child_process');
const net = require('net');
const path = require('path');

/** @type {import('child_process').ChildProcess | null} */
let launched = null;

/**
 * Wait until a TCP port accepts connections.
 * @param {string} host
 * @param {number} port
 * @param {number} timeoutMs
 */
function waitForPort(host, port, timeoutMs) {
  const start = Date.now();
  return new Promise((resolve, reject) => {
    const tryOnce = () => {
      const socket = net.connect({ host, port }, () => {
        socket.end();
        resolve();
      });
      socket.on('error', () => {
        socket.destroy();
        if (Date.now() - start > timeoutMs) {
          reject(new Error(`Timed out waiting for DAP on ${host}:${port}`));
          return;
        }
        setTimeout(tryOnce, 100);
      });
    };
    tryOnce();
  });
}

/**
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
  const factory = new EveDebugAdapterDescriptorFactory();
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('eve', factory)
  );
  context.subscriptions.push(
    vscode.commands.registerCommand('eve-debug.start', () => {
      return vscode.debug.startDebugging(undefined, {
        type: 'eve',
        request: 'launch',
        name: 'EVEngine: Debug game',
        program: '${workspaceFolder}',
        port: 4711,
      });
    })
  );
}

function deactivate() {
  if (launched && !launched.killed) {
    launched.kill();
    launched = null;
  }
}

class EveDebugAdapterDescriptorFactory {
  /**
   * @param {vscode.DebugSession} session
   * @returns {Promise<vscode.DebugAdapterDescriptor>}
   */
  async createDebugAdapterDescriptor(session) {
    const cfg = session.configuration;
    const port = Number(cfg.port || 4711);
    const host = String(cfg.host || '127.0.0.1');

    if (session.configuration.request === 'launch') {
      if (launched && !launched.killed) {
        launched.kill();
        launched = null;
      }
      const evePath = String(cfg.evePath || 'eve');
      const program = String(cfg.program || '.');
      const extra = Array.isArray(cfg.args) ? cfg.args.map(String) : [];
      // Chdir into the game folder; pass "." so Run mounts the correct root.
      const args = ['run', '--debug', `--dap-port=${port}`, '.', ...extra];
      launched = spawn(evePath, args, {
        cwd: program,
        env: process.env,
        stdio: 'ignore',
      });
      launched.on('exit', () => {
        launched = null;
      });
      await waitForPort(host, port, 15000);
    } else {
      await waitForPort(host, port, 5000);
    }

    return new vscode.DebugAdapterServer(port, host);
  }
}

module.exports = {
  activate,
  deactivate,
};
