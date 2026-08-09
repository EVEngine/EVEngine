// @ts-check
'use strict';

/**
 * VS Code debugger extension for EVEngine.
 *
 * Architecture (same pattern as microsoft/vscode-mock-debug):
 *  - package.json contributes type `eve` + breakpoints for `.nut`
 *  - DebugConfigurationProvider fills / validates launch.json
 *  - DebugAdapterDescriptorFactory launches `eve run --debug --dap-port`
 *    and returns DebugAdapterServer so VS Code speaks DAP to the engine
 *
 * Standard VS Code debug keys then work against the session:
 *  F5 continue · F10 step over · F11 step into · F8 step frame · F6 pause
 */

const vscode = require('vscode');
const { spawn } = require('child_process');
const fs = require('fs');
const net = require('net');
const path = require('path');

/** @type {import('child_process').ChildProcess | null} */
let launched = null;

/** @type {vscode.OutputChannel | null} */
let output = null;

function getOutput() {
  if (!output) {
    output = vscode.window.createOutputChannel('EVEngine Debug');
  }
  return output;
}

/**
 * @param {string} host
 * @param {number} port
 * @param {number} timeoutMs
 * @returns {Promise<void>}
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

  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return configured || 'eve';
}

/**
 * Pick a free local TCP port.
 * @returns {Promise<number>}
 */
function findFreePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.listen(0, '127.0.0.1', () => {
      const addr = srv.address();
      if (!addr || typeof addr === 'string') {
        srv.close();
        reject(new Error('Failed to allocate DAP port'));
        return;
      }
      const port = addr.port;
      srv.close(() => resolve(port));
    });
    srv.on('error', reject);
  });
}

/**
 * Kill a previously launched eve process if still alive.
 */
function killLaunched() {
  if (!launched || launched.killed) {
    launched = null;
    return;
  }
  try {
    if (process.platform === 'win32') {
      spawn('taskkill', ['/pid', String(launched.pid), '/T', '/F']);
    } else {
      launched.kill('SIGTERM');
      const child = launched;
      setTimeout(() => {
        if (child && !child.killed) child.kill('SIGKILL');
      }, 1500);
    }
  } catch (_) {
    /* ignore */
  }
  launched = null;
}

/**
 * @param {vscode.ExtensionContext} context
 */
function activate(context) {
  context.subscriptions.push(getOutput());

  const provider = new EveConfigurationProvider();
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider('eve', provider)
  );
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(
      'eve',
      {
        provideDebugConfigurations() {
          return [
            {
              type: 'eve',
              request: 'launch',
              name: 'EVEngine: Debug game',
              program: '${workspaceFolder}',
              port: 0,
            },
            {
              type: 'eve',
              request: 'attach',
              name: 'EVEngine: Attach',
              host: '127.0.0.1',
              port: 4711,
            },
          ];
        },
      },
      vscode.DebugConfigurationProviderTriggerKind.Dynamic
    )
  );

  const factory = new EveDebugAdapterDescriptorFactory();
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('eve', factory)
  );
  context.subscriptions.push(factory);

  // Optional DAP wire log (enable with `"trace": true` in launch.json).
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterTrackerFactory('eve', {
      createDebugAdapterTracker(session) {
        if (!session.configuration.trace) return {};
        const ch = getOutput();
        return {
          onWillReceiveMessage: (m) => ch.appendLine(`→ ${JSON.stringify(m)}`),
          onDidSendMessage: (m) => ch.appendLine(`← ${JSON.stringify(m)}`),
        };
      },
    })
  );

  context.subscriptions.push(
    vscode.commands.registerCommand('eve-debug.start', () => {
      const folder = vscode.workspace.workspaceFolders?.[0];
      return vscode.debug.startDebugging(folder, {
        type: 'eve',
        request: 'launch',
        name: 'EVEngine: Debug game',
        program: folder ? folder.uri.fsPath : '${workspaceFolder}',
        port: 0,
      });
    })
  );

  // Editor title / command palette: debug the folder containing the active .nut
  context.subscriptions.push(
    vscode.commands.registerCommand('eve-debug.debugEditorContents', (resource) => {
      let target = resource;
      if (!target && vscode.window.activeTextEditor) {
        target = vscode.window.activeTextEditor.document.uri;
      }
      if (!target) {
        return vscode.window.showInformationMessage('Open a .nut file or game folder first.');
      }
      const folder = path.dirname(target.fsPath);
      const ws = vscode.workspace.getWorkspaceFolder(target);
      return vscode.debug.startDebugging(ws, {
        type: 'eve',
        request: 'launch',
        name: 'EVEngine: Debug game',
        program: folder,
        port: 0,
      });
    })
  );

  // Continue / step helpers for the command palette; F10/F11 use VS Code defaults.
  context.subscriptions.push(
    vscode.commands.registerCommand('eve-debug.continue', () =>
      vscode.commands.executeCommand('workbench.action.debug.continue')
    ),
    vscode.commands.registerCommand('eve-debug.stepOver', () =>
      vscode.commands.executeCommand('workbench.action.debug.stepOver')
    ),
    vscode.commands.registerCommand('eve-debug.stepInto', () =>
      vscode.commands.executeCommand('workbench.action.debug.stepInto')
    ),
    vscode.commands.registerCommand('eve-debug.stepOut', () =>
      vscode.commands.executeCommand('workbench.action.debug.stepOut')
    ),
    vscode.commands.registerCommand('eve-debug.stepFrame', () => {
      const session = vscode.debug.activeDebugSession;
      if (session && session.type === 'eve') {
        return session.customRequest('stepFrame', { threadId: 1 });
      }
      return undefined;
    }),
    vscode.commands.registerCommand('eve-debug.pause', () =>
      vscode.commands.executeCommand('workbench.action.debug.pause')
    )
  );

  context.subscriptions.push(
    vscode.debug.onDidTerminateDebugSession((session) => {
      if (session.type === 'eve') killLaunched();
    })
  );
}

function deactivate() {
  killLaunched();
}

class EveConfigurationProvider {
  /**
   * @param {vscode.WorkspaceFolder | undefined} folder
   * @param {vscode.DebugConfiguration} config
   * @returns {vscode.ProviderResult<vscode.DebugConfiguration>}
   */
  resolveDebugConfiguration(folder, config) {
    // Empty launch.json → synthesize a default from the open editor / workspace.
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      const isNut =
        editor &&
        (editor.document.languageId === 'nut' ||
          editor.document.fileName.endsWith('.nut'));
      config.type = 'eve';
      config.name = 'EVEngine: Debug game';
      config.request = 'launch';
      if (isNut) {
        config.program = path.dirname(editor.document.uri.fsPath);
      } else if (folder) {
        config.program = folder.uri.fsPath;
      } else {
        return vscode.window
          .showInformationMessage('Cannot find a game folder to debug')
          .then(() => undefined);
      }
      config.port = 0;
    }

    if (config.request === 'launch' && !config.program) {
      return vscode.window
        .showInformationMessage('launch.json: missing "program" (game directory)')
        .then(() => undefined);
    }

    return config;
  }

  /**
   * @param {vscode.WorkspaceFolder | undefined} folder
   * @param {vscode.DebugConfiguration} config
   * @returns {Promise<vscode.DebugConfiguration>}
   */
  async resolveDebugConfigurationWithSubstitutedVariables(folder, config) {
    const workspaceRoot = folder?.uri.fsPath || vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;

    if (config.request === 'launch') {
      config.evePath = resolveEvePath(config.evePath, workspaceRoot);
      if (!config.evePath || (config.evePath !== 'eve' && !fs.existsSync(config.evePath))) {
        // Still allow bare `eve` on PATH.
        if (config.evePath !== 'eve') {
          void vscode.window.showErrorMessage(
            `eve executable not found: ${config.evePath}. Set "evePath" in launch.json.`
          );
          throw new Error(`eve executable not found: ${config.evePath}`);
        }
      }
      if (config.port === undefined || config.port === null || Number(config.port) === 0) {
        config.port = await findFreePort();
      }
      if (!config.host) config.host = '127.0.0.1';
      if (config.cwd === undefined) config.cwd = config.program;
    } else if (config.request === 'attach') {
      if (!config.host) config.host = '127.0.0.1';
      if (!config.port) config.port = 4711;
    }

    return config;
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
    const ch = getOutput();

    if (session.configuration.request === 'launch') {
      killLaunched();

      const evePath = String(cfg.evePath || 'eve');
      const program = String(cfg.program || '.');
      const cwd = String(cfg.cwd || program);
      const extra = Array.isArray(cfg.args) ? cfg.args.map(String) : [];

      // Chdir into the game folder; pass "." so Run mounts the correct root.
      const args = ['run', '--debug', `--dap-port=${port}`, '.', ...extra];
      ch.appendLine(`[eve-debug] ${evePath} ${args.join(' ')}`);
      ch.appendLine(`[eve-debug] cwd=${cwd}  dap=${host}:${port}`);

      launched = spawn(evePath, args, {
        cwd,
        env: process.env,
        stdio: ['ignore', 'pipe', 'pipe'],
      });

      const sessionId = session.id;
      const mirror = (buf) => {
        const text = buf.toString();
        ch.append(text);
        try {
          vscode.debug.activeDebugConsole?.append?.(text);
        } catch (_) {
          /* Debug Console may not be ready yet */
        }
      };
      launched.stdout?.on('data', mirror);
      launched.stderr?.on('data', mirror);
      launched.on('error', (err) => {
        ch.appendLine(`[eve-debug] failed to spawn: ${err.message}`);
        void vscode.window.showErrorMessage(`Failed to launch eve: ${err.message}`);
      });
      launched.on('exit', (code, signal) => {
        ch.appendLine(`[eve-debug] eve exited code=${code} signal=${signal}`);
        launched = null;
        // End the VS Code session if the game process dies first.
        const active = vscode.debug.activeDebugSession;
        if (active && active.id === sessionId) {
          void vscode.commands.executeCommand('workbench.action.debug.stop');
        }
      });

      try {
        await waitForPort(host, port, 20000);
      } catch (e) {
        killLaunched();
        throw e;
      }
    } else {
      ch.appendLine(`[eve-debug] attach ${host}:${port}`);
      await waitForPort(host, port, 5000);
    }

    return new vscode.DebugAdapterServer(port, host);
  }

  dispose() {
    killLaunched();
  }
}

module.exports = {
  activate,
  deactivate,
};
