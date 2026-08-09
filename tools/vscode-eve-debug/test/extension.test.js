// @ts-check
'use strict';

/**
 * Lightweight Node tests for eve-debug extension helpers.
 * Run: node tools/vscode-eve-debug/test/extension.test.js
 *
 * These exercise path resolution / port wait without loading the vscode module.
 */

const assert = require('assert');
const fs = require('fs');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

const repoRoot = path.resolve(__dirname, '../../..');

function resolveEvePath(configured, workspaceRoot) {
  if (configured && configured !== 'eve' && fs.existsSync(configured)) {
    return configured;
  }
  if (!workspaceRoot) return configured || 'eve';

  const candidates = [];
  if (process.platform === 'darwin') {
    candidates.push(
      path.join(workspaceRoot, 'build/macosx-debug/src/engine/eve'),
      path.join(workspaceRoot, 'build/macosx/src/engine/eve')
    );
  } else if (process.platform === 'win32') {
    candidates.push(
      path.join(workspaceRoot, 'build/win32-debug/src/engine/Debug/eve.exe'),
      path.join(workspaceRoot, 'build/win32-debug/src/engine/eve.exe')
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
        setTimeout(tryOnce, 50);
      });
    };
    tryOnce();
  });
}

function findFreePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.listen(0, '127.0.0.1', () => {
      const addr = srv.address();
      if (!addr || typeof addr === 'string') {
        srv.close();
        reject(new Error('no port'));
        return;
      }
      const port = addr.port;
      srv.close(() => resolve(port));
    });
    srv.on('error', reject);
  });
}

/** Minimal DAP client for smoke-testing a live eve process. */
class DapClient {
  constructor(port) {
    this.port = port;
    /** @type {net.Socket | null} */
    this.sock = null;
    this.buf = '';
    this.seq = 1;
    /** @type {object[]} */
    this.events = [];
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.sock = net.connect({ host: '127.0.0.1', port: this.port }, resolve);
      this.sock.on('error', reject);
      this.sock.on('data', (d) => {
        this.buf += d.toString('utf8');
      });
    });
  }

  send(command, args = {}) {
    const msg = JSON.stringify({
      seq: this.seq++,
      type: 'request',
      command,
      arguments: args,
    });
    const frame = `Content-Length: ${Buffer.byteLength(msg, 'utf8')}\r\n\r\n${msg}`;
    this.sock.write(frame);
  }

  async recv(timeoutMs = 3000) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
      const msg = this.tryParse();
      if (msg) {
        if (msg.type === 'event') this.events.push(msg);
        return msg;
      }
      await new Promise((r) => setTimeout(r, 20));
    }
    return null;
  }

  tryParse() {
    const idx = this.buf.indexOf('\r\n\r\n');
    if (idx < 0) return null;
    const header = this.buf.slice(0, idx);
    const m = /Content-Length:\s*(\d+)/i.exec(header);
    if (!m) {
      this.buf = this.buf.slice(idx + 4);
      return null;
    }
    const len = Number(m[1]);
    const start = idx + 4;
    if (this.buf.length < start + len) return null;
    const json = this.buf.slice(start, start + len);
    this.buf = this.buf.slice(start + len);
    return JSON.parse(json);
  }

  async expectResponse(command, timeoutMs = 3000) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
      const msg = await this.recv(100);
      if (msg && msg.type === 'response' && msg.command === command) return msg;
    }
    return null;
  }

  close() {
    try {
      this.sock?.end();
    } catch (_) {}
  }
}

async function testResolveEvePath() {
  const resolved = resolveEvePath('eve', repoRoot);
  assert.ok(resolved, 'resolved path');
  if (resolved !== 'eve') {
    assert.ok(fs.existsSync(resolved), `eve binary exists: ${resolved}`);
    console.log('  resolveEvePath →', resolved);
  } else {
    console.log('  resolveEvePath → eve (on PATH; build tree not found)');
  }
}

async function testWaitForPort() {
  const port = await findFreePort();
  const srv = net.createServer();
  await new Promise((resolve) => srv.listen(port, '127.0.0.1', resolve));
  await waitForPort('127.0.0.1', port, 2000);
  await new Promise((resolve) => srv.close(resolve));
  console.log('  waitForPort ok on', port);
}

async function testLiveEveDap() {
  const eve = resolveEvePath('eve', repoRoot);
  if (eve === 'eve' || !fs.existsSync(eve)) {
    console.log('  SKIP live eve DAP (binary not found)');
    return;
  }

  const port = await findFreePort();
  const game = path.join(repoRoot, 'examples/basic');
  const child = spawn(eve, ['run', '--debug', `--dap-port=${port}`, '.'], {
    cwd: game,
    env: {
      ...process.env,
      // Allow headless CI / sandbox hosts without a real display.
      SDL_VIDEODRIVER: process.env.SDL_VIDEODRIVER || 'dummy',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  let stderr = '';
  child.stderr.on('data', (d) => {
    stderr += d.toString();
  });
  child.stdout.on('data', (d) => {
    stderr += d.toString();
  });

  const noDisplay = () =>
    /did not add any displays|No available video device|SDL.*video|initialize SDL video|Vulkan support is either not configured|video driver \(dummy\)|Window Create Failed/i.test(
      stderr
    );

  const waitForPortOrExit = async (timeoutMs) => {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
      if (child.exitCode !== null) {
        if (noDisplay()) return 'no-display';
        throw new Error(`eve exited early code=${child.exitCode}\n${stderr}`);
      }
      try {
        await waitForPort('127.0.0.1', port, 200);
        return 'ready';
      } catch (_) {
        /* retry */
      }
    }
    if (noDisplay()) return 'no-display';
    throw new Error(`Timed out waiting for DAP on 127.0.0.1:${port}\n${stderr}`);
  };

  try {
    const ready = await waitForPortOrExit(20000);
    if (ready === 'no-display') {
      console.log(
        '  SKIP live eve DAP (no display / SDL video unavailable in this environment)'
      );
      return;
    }
    if (child.exitCode !== null) {
      if (noDisplay()) {
        console.log(
          '  SKIP live eve DAP (no display / SDL video unavailable in this environment)'
        );
        return;
      }
      throw new Error(`eve exited early code=${child.exitCode}\n${stderr}`);
    }

    // Connect promptly: eve waits for configurationDone before loading scripts.
    const client = new DapClient(port);
    await client.connect();

    client.send('initialize', {
      clientID: 'eve-node-test',
      adapterID: 'eve',
      linesStartAt1: true,
      columnsStartAt1: true,
      pathFormat: 'path',
    });
    const init = await client.expectResponse('initialize', 10000);
    assert.ok(init && init.success, `initialize success\nstderr:\n${stderr}`);
    assert.ok(init.body.supportsConfigurationDoneRequest, 'supportsConfigurationDoneRequest');
    assert.ok(init.body.supportsTerminateRequest, 'supportsTerminateRequest');

    // Drain initialized / process events
    await client.recv(500);
    await client.recv(500);

    client.send('launch', {
      program: game,
      cwd: game,
      stopOnEntry: true,
    });
    const launch = await client.expectResponse('launch', 5000);
    assert.ok(launch && launch.success, 'launch success');

    const mainNut = path.join(game, 'main.nut');
    client.send('setBreakpoints', {
      source: { path: mainNut, name: 'main.nut' },
      breakpoints: [{ line: 80 }],
    });
    const bps = await client.expectResponse('setBreakpoints', 5000);
    assert.ok(bps && bps.success, 'setBreakpoints success');
    assert.ok(bps.body.breakpoints.length >= 1, 'breakpoint verified');
    const bpPath = bps.body.breakpoints[0].source && bps.body.breakpoints[0].source.path;
    assert.ok(
      !bpPath || bpPath === mainNut || bpPath.endsWith('main.nut'),
      'breakpoint source path mapped'
    );

    client.send('configurationDone', {});
    const cfg = await client.expectResponse('configurationDone', 5000);
    assert.ok(cfg && cfg.success, 'configurationDone success');

    // stopOnEntry arms stepInto; stopped arrives on the first script line after start.
    let stopped = client.events.find((e) => e.event === 'stopped');
    const t0 = Date.now();
    while (!stopped && Date.now() - t0 < 15000) {
      if (child.exitCode !== null) {
        if (noDisplay()) {
          console.log(
            '  SKIP live eve DAP (no display / SDL video unavailable in this environment)'
          );
          return;
        }
        throw new Error(`eve exited before stopOnEntry code=${child.exitCode}\n${stderr}`);
      }
      const msg = await client.recv(200);
      if (msg && msg.type === 'event' && msg.event === 'stopped') stopped = msg;
    }
    assert.ok(stopped, `stopped on entry\nstderr:\n${stderr}`);

    // Stack path should resolve to the game file, not a bare basename.
    client.send('stackTrace', { threadId: 1, startFrame: 0, levels: 8 });
    const stack = await client.expectResponse('stackTrace', 5000);
    assert.ok(stack && stack.success, 'stackTrace success');
    const frame0 = stack.body.stackFrames && stack.body.stackFrames[0];
    assert.ok(frame0, 'stack frame');
    assert.ok(frame0.source && frame0.source.path, 'stack source path');
    assert.ok(
      frame0.source.path.endsWith('.nut'),
      `stack path looks like a script: ${frame0.source.path}`
    );

    // F5 continue — may die during window create on headless/dummy SDL hosts.
    client.send('continue', { threadId: 1 });
    const cont = await client.expectResponse('continue', 5000);
    assert.ok(cont && cont.success, 'continue success');

    const tExit = Date.now();
    while (Date.now() - tExit < 4000) {
      if (noDisplay() || child.exitCode !== null) break;
      await new Promise((r) => setTimeout(r, 50));
    }
    if (noDisplay()) {
      console.log(
        '  live eve DAP stopOnEntry + stackTrace ok; SKIP pause/step (no display)'
      );
      client.close();
      return;
    }
    if (child.exitCode !== null) {
      throw new Error(`eve exited after continue code=${child.exitCode}\n${stderr}`);
    }

    // Pause = break next statement; wait for the subsequent stopped event.
    client.send('pause', { threadId: 1 });
    const pause = await client.expectResponse('pause', 10000);
    if (!pause && noDisplay()) {
      console.log(
        '  live eve DAP stopOnEntry + stackTrace ok; SKIP pause/step (no display)'
      );
      client.close();
      return;
    }
    assert.ok(pause && pause.success, `pause success\nstderr:\n${stderr}`);
    let paused = null;
    const t1 = Date.now();
    while (!paused && Date.now() - t1 < 10000) {
      const msg = await client.recv(200);
      if (msg && msg.type === 'event' && msg.event === 'stopped') paused = msg;
    }
    assert.ok(paused, 'stopped after pause');

    client.send('next', { threadId: 1 });
    const next = await client.expectResponse('next', 5000);
    assert.ok(next && next.success, 'next success');
    assert.strictEqual(next.body.threadId, 1);

    client.send('disconnect', { terminateDebuggee: true });
    const disc = await client.expectResponse('disconnect', 5000);
    assert.ok(disc && disc.success, 'disconnect success');

    client.close();
    console.log('  live eve DAP handshake + continue/step ok');
  } finally {
    try {
      child.kill('SIGTERM');
    } catch (_) {}
    await new Promise((r) => setTimeout(r, 300));
    try {
      child.kill('SIGKILL');
    } catch (_) {}
    if (stderr.includes('Failed to start DAP')) {
      throw new Error('eve failed to start DAP:\n' + stderr);
    }
  }
}

async function testPackageJsonKeybindings() {
  const pkg = JSON.parse(
    fs.readFileSync(path.join(__dirname, '..', 'package.json'), 'utf8')
  );
  assert.strictEqual(pkg.contributes.debuggers[0].type, 'eve');
  const keys = pkg.contributes.keybindings || [];
  const f5 = keys.find((k) => k.key === 'f5');
  const f8 = keys.find((k) => k.key === 'f8');
  assert.ok(f5 && f5.command === 'eve-debug.continue', 'F5 → continue');
  assert.ok(f8 && f8.command === 'eve-debug.stepFrame', 'F8 → stepFrame');
  assert.ok(
    f5.when.includes("debugType == 'eve'") && f5.when.includes('stopped'),
    'F5 when clause'
  );
  const cmds = (pkg.contributes.commands || []).map((c) => c.command);
  assert.ok(cmds.includes('eve-debug.stepOver'), 'stepOver command');
  assert.ok(cmds.includes('eve-debug.stepInto'), 'stepInto command');
  assert.ok(cmds.includes('eve-debug.stepOut'), 'stepOut command');
  console.log('  package.json keybindings ok');
}

async function main() {
  let failed = 0;
  const cases = [
    ['package.json keybindings', testPackageJsonKeybindings],
    ['resolveEvePath', testResolveEvePath],
    ['waitForPort', testWaitForPort],
    ['live eve DAP', testLiveEveDap],
  ];
  for (const [name, fn] of cases) {
    try {
      console.log(`TEST ${name}`);
      await fn();
      console.log(`  PASS`);
    } catch (e) {
      failed++;
      console.error(`  FAIL:`, e);
    }
  }
  if (failed) {
    console.error(`\n${failed} test(s) failed`);
    process.exit(1);
  }
  console.log('\nAll extension tests passed');
}

main();
