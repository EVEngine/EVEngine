// @ts-check
'use strict';

const vscode = require('vscode');
const fs = require('fs');
const { LspClient } = require('./lspClient');
const { findProjectRoot, resolveEvePath } = require('./evePath');

/**
 * @param {any} value
 * @returns {vscode.Position}
 */
function asPosition(value) {
  return new vscode.Position(
    value && typeof value.line === 'number' ? value.line : 0,
    value && typeof value.character === 'number' ? value.character : 0
  );
}

/**
 * @param {any} value
 * @returns {vscode.Range}
 */
function asRange(value) {
  return new vscode.Range(asPosition(value && value.start), asPosition(value && value.end));
}

/**
 * @param {vscode.Position} position
 */
function toPosition(position) {
  return { line: position.line, character: position.character };
}

/**
 * @param {vscode.Range} range
 */
function toRange(range) {
  return { start: toPosition(range.start), end: toPosition(range.end) };
}

/**
 * @param {vscode.ExtensionContext} context
 */
function activateLanguageService(context) {
  const output = vscode.window.createOutputChannel('EVEngine Language Server');
  const diagnostics = vscode.languages.createDiagnosticCollection('evescript');
  /** @type {Map<string, LspClient>} */
  const sessions = new Map();
  let missingEveWarned = false;

  context.subscriptions.push(output, diagnostics, {
    dispose() {
      return stopAll();
    },
  });

  const selector = { language: 'nut', scheme: 'file' };

  /**
   * @param {string} root
   * @returns {Promise<LspClient | null>}
   */
  async function ensureSession(root) {
    const existing = sessions.get(root);
    if (existing) return existing;

    const cfg = vscode.workspace.getConfiguration('eve');
    if (cfg.get('languageServer.enabled') === false) return null;

    const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const evePath = resolveEvePath(cfg.get('eve.executable') || 'eve', workspaceRoot);
    if (!evePath || (evePath !== 'eve' && fs.existsSync(evePath) === false)) {
      if (!missingEveWarned) {
        missingEveWarned = true;
        void vscode.window.showWarningMessage(
          `EVEngine language server: eve executable not found (${evePath}). Set eve.executable.`
        );
      }
      return null;
    }

    const trace = cfg.get('languageServer.trace') === 'messages';
    const client = new LspClient(evePath, ['language-server', '--root', root], {
      cwd: root,
      onStderr: (text) => output.append(text),
      onLog: trace ? (line) => output.appendLine(line) : undefined,
    });
    client.on('textDocument/publishDiagnostics', (params) => {
      if (!params || !params.uri) return;
      const items = Array.isArray(params.diagnostics) ? params.diagnostics : [];
      diagnostics.set(
        vscode.Uri.parse(params.uri),
        items.map((item) => {
          const diagnostic = new vscode.Diagnostic(
            asRange(item.range),
            String(item.message || ''),
            item.severity === 2 ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
          );
          diagnostic.source = item.source || 'evescript';
          if (item.code) diagnostic.code = item.code;
          return diagnostic;
        })
      );
    });
    client.child.on('exit', (code, signal) => {
      sessions.delete(root);
      output.appendLine(`[lsp] language-server exited code=${code} signal=${signal} root=${root}`);
    });

    try {
      await client.request('initialize', {
        processId: process.pid,
        rootUri: vscode.Uri.file(root).toString(),
        capabilities: {
          textDocument: {
            synchronization: { change: 2 },
            completion: { completionItem: { snippetSupport: false } },
            hover: { contentFormat: ['markdown', 'plaintext'] },
            publishDiagnostics: {},
          },
        },
      });
      client.notify('initialized', {});
    } catch (err) {
      output.appendLine(`[lsp] initialize failed: ${err.message}`);
      await client.shutdown();
      return null;
    }

    sessions.set(root, client);
    for (const document of vscode.workspace.textDocuments) {
      if (document.languageId === 'nut' && document.uri.scheme === 'file') {
        const docRoot = findProjectRoot(document.uri.fsPath, workspaceRoot);
        if (docRoot === root) openDocument(client, document);
      }
    }
    return client;
  }

  /**
   * @param {LspClient} client
   * @param {vscode.TextDocument} document
   */
  function openDocument(client, document) {
    client.notify('textDocument/didOpen', {
      textDocument: {
        uri: document.uri.toString(),
        languageId: 'nut',
        version: document.version,
        text: document.getText(),
      },
    });
  }

  /**
   * @param {vscode.TextDocument} document
   */
  async function clientFor(document) {
    if (document.languageId !== 'nut' || document.uri.scheme !== 'file') return null;
    const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const root = findProjectRoot(document.uri.fsPath, workspaceRoot);
    return ensureSession(root);
  }

  async function stopAll() {
    const running = [...sessions.values()];
    sessions.clear();
    diagnostics.clear();
    await Promise.all(running.map((client) => client.shutdown()));
  }

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => {
      void clientFor(document).then((client) => {
        if (client) openDocument(client, document);
      });
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      const document = event.document;
      if (document.languageId !== 'nut' || document.uri.scheme !== 'file') return;
      void clientFor(document).then((client) => {
        if (!client) return;
        const changes = event.contentChanges.map((change) => ({
          range: toRange(change.range),
          text: change.text,
        }));
        client.notify('textDocument/didChange', {
          textDocument: { uri: document.uri.toString(), version: document.version },
          contentChanges: changes,
        });
      });
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (document.uri.scheme !== 'file') return;
      const workspaceRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
      const root = findProjectRoot(document.uri.fsPath, workspaceRoot);
      const client = sessions.get(root);
      if (!client) return;
      client.notify('textDocument/didClose', {
        textDocument: { uri: document.uri.toString() },
      });
      diagnostics.delete(document.uri);
    })
  );

  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      selector,
      {
        async provideCompletionItems(document, position) {
          const client = await clientFor(document);
          if (!client) return;
          const result = await client.request('textDocument/completion', {
            textDocument: { uri: document.uri.toString() },
            position: toPosition(position),
          });
          const items = Array.isArray(result) ? result : result && result.items;
          if (!Array.isArray(items)) return;
          return items.map((item) => {
            const completion = new vscode.CompletionItem(
              String(item.label || ''),
              typeof item.kind === 'number' ? item.kind : vscode.CompletionItemKind.Text
            );
            if (item.detail) completion.detail = item.detail;
            if (item.insertText) completion.insertText = item.insertText;
            return completion;
          });
        },
      },
      '.',
      ':'
    ),
    vscode.languages.registerHoverProvider(selector, {
      async provideHover(document, position) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/hover', {
          textDocument: { uri: document.uri.toString() },
          position: toPosition(position),
        });
        if (!result || !result.contents) return;
        const value =
          typeof result.contents === 'string'
            ? result.contents
            : result.contents.value || (Array.isArray(result.contents) ? result.contents.join('\n') : '');
        if (!value) return;
        return new vscode.Hover(new vscode.MarkdownString(value));
      },
    }),
    vscode.languages.registerDefinitionProvider(selector, {
      async provideDefinition(document, position) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/definition', {
          textDocument: { uri: document.uri.toString() },
          position: toPosition(position),
        });
        if (!result) return;
        const locations = Array.isArray(result) ? result : [result];
        return locations
          .filter((item) => item && item.uri)
          .map((item) => new vscode.Location(vscode.Uri.parse(item.uri), asRange(item.range)));
      },
    }),
    vscode.languages.registerReferenceProvider(selector, {
      async provideReferences(document, position, options) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/references', {
          textDocument: { uri: document.uri.toString() },
          position: toPosition(position),
          context: { includeDeclaration: !!(options && options.includeDeclaration) },
        });
        if (!Array.isArray(result)) return;
        return result.map((item) => new vscode.Location(vscode.Uri.parse(item.uri), asRange(item.range)));
      },
    }),
    vscode.languages.registerRenameProvider(selector, {
      async provideRenameEdits(document, position, newName) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/rename', {
          textDocument: { uri: document.uri.toString() },
          position: toPosition(position),
          newName,
        });
        if (!result || !result.changes) return;
        const edit = new vscode.WorkspaceEdit();
        for (const [uri, edits] of Object.entries(result.changes)) {
          const resource = vscode.Uri.parse(uri);
          for (const item of edits) {
            edit.replace(resource, asRange(item.range), item.newText || '');
          }
        }
        return edit;
      },
    }),
    vscode.languages.registerDocumentSymbolProvider(selector, {
      async provideDocumentSymbols(document) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/documentSymbol', {
          textDocument: { uri: document.uri.toString() },
        });
        if (!Array.isArray(result)) return;
        return result.map((item) => {
          const range = asRange(item.range || (item.location && item.location.range));
          const symbol = new vscode.DocumentSymbol(
            String(item.name || ''),
            String(item.detail || ''),
            typeof item.kind === 'number' ? item.kind : vscode.SymbolKind.Variable,
            range,
            asRange(item.selectionRange || item.range)
          );
          return symbol;
        });
      },
    }),
    vscode.languages.registerSignatureHelpProvider(
      selector,
      {
        async provideSignatureHelp(document, position) {
          const client = await clientFor(document);
          if (!client) return;
          const result = await client.request('textDocument/signatureHelp', {
            textDocument: { uri: document.uri.toString() },
            position: toPosition(position),
          });
          if (!result || !Array.isArray(result.signatures) || result.signatures.length === 0) return;
          const help = new vscode.SignatureHelp();
          help.activeSignature = result.activeSignature || 0;
          help.activeParameter = result.activeParameter || 0;
          help.signatures = result.signatures.map((sig) => {
            const info = new vscode.SignatureInformation(String(sig.label || ''), sig.documentation || '');
            info.parameters = (sig.parameters || []).map(
              (parameter) => new vscode.ParameterInformation(parameter.label || '')
            );
            return info;
          });
          return help;
        },
      },
      '(',
      ','
    ),
    vscode.languages.registerDocumentFormattingEditProvider(selector, {
      async provideDocumentFormattingEdits(document, options) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/formatting', {
          textDocument: { uri: document.uri.toString() },
          options: {
            tabSize: options.tabSize,
            insertSpaces: options.insertSpaces,
          },
        });
        if (!Array.isArray(result)) return;
        return result.map((item) => vscode.TextEdit.replace(asRange(item.range), item.newText || ''));
      },
    }),
    vscode.languages.registerDocumentRangeFormattingEditProvider(selector, {
      async provideDocumentRangeFormattingEdits(document, range, options) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/rangeFormatting', {
          textDocument: { uri: document.uri.toString() },
          range: toRange(range),
          options: {
            tabSize: options.tabSize,
            insertSpaces: options.insertSpaces,
          },
        });
        if (!Array.isArray(result)) return;
        return result.map((item) => vscode.TextEdit.replace(asRange(item.range), item.newText || ''));
      },
    }),
    vscode.languages.registerFoldingRangeProvider(selector, {
      async provideFoldingRanges(document) {
        const client = await clientFor(document);
        if (!client) return;
        const result = await client.request('textDocument/foldingRange', {
          textDocument: { uri: document.uri.toString() },
        });
        if (!Array.isArray(result)) return;
        return result.map((item) => {
          const kind =
            item.kind === 'comment'
              ? vscode.FoldingRangeKind.Comment
              : item.kind === 'region'
                ? vscode.FoldingRangeKind.Region
                : undefined;
          return new vscode.FoldingRange(item.startLine || 0, item.endLine || 0, kind);
        });
      },
    }),
    vscode.languages.registerDocumentSemanticTokensProvider(
      selector,
      {
        async provideDocumentSemanticTokens(document) {
          const client = await clientFor(document);
          if (!client) return;
          const result = await client.request('textDocument/semanticTokens/full', {
            textDocument: { uri: document.uri.toString() },
          });
          const data = result && Array.isArray(result.data) ? result.data : [];
          return new vscode.SemanticTokens(Uint32Array.from(data));
        },
      },
      new vscode.SemanticTokensLegend(
        ['namespace', 'class', 'function', 'method', 'variable', 'parameter', 'property', 'keyword'],
        ['declaration', 'readonly', 'defaultLibrary']
      )
    )
  );

  for (const document of vscode.workspace.textDocuments) {
    void clientFor(document);
  }
}

module.exports = { activateLanguageService };
