const vscode = require('vscode');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

/**
 * Resolves the path to the Kobel compiler executable (`kobel_v1.exe` or `kobel`).
 */
function resolveCompilerPath() {
    const config = vscode.workspace.getConfiguration('kobel');
    let compilerPath = config.get('compilerPath');

    if (compilerPath && fs.existsSync(compilerPath)) {
        return compilerPath;
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        for (const folder of workspaceFolders) {
            const candidate1 = path.join(folder.uri.fsPath, 'kobel_v1.exe');
            if (fs.existsSync(candidate1)) return candidate1;

            const candidate2 = path.join(folder.uri.fsPath, 'kobel.exe');
            if (fs.existsSync(candidate2)) return candidate2;
        }
    }

    return 'kobel';
}

/**
 * Lightweight, zero-dependency Language Server Protocol Client for Kobel.
 */
class KobelLanguageClient {
    constructor(compilerPath) {
        this.compilerPath = compilerPath;
        this.nextId = 1;
        this.pendingRequests = new Map();
        this.diagnosticsCollection = vscode.languages.createDiagnosticCollection('kobel');
        this.buffer = Buffer.alloc(0);
        this.process = null;
        this.isReady = false;
        this.start();
    }

    start() {
        const workspaceFolders = vscode.workspace.workspaceFolders;
        const cwd = (workspaceFolders && workspaceFolders.length > 0)
            ? workspaceFolders[0].uri.fsPath
            : undefined;

        try {
            this.process = spawn(this.compilerPath, ['lsp'], {
                cwd: cwd,
                stdio: ['pipe', 'pipe', 'pipe']
            });
        } catch (err) {
            console.error('[Kobel LSP] Failed to spawn compiler process:', err);
            vscode.window.showErrorMessage(`Failed to start Kobel Language Server: ${err.message}`);
            return;
        }

        this.process.stdout.on('data', chunk => this.handleData(chunk));
        this.process.stderr.on('data', data => {
            console.error('[Kobel LSP stderr]:', data.toString());
        });

        this.process.on('error', err => {
            console.error('[Kobel LSP] Process error:', err);
        });

        this.process.on('exit', code => {
            console.log(`[Kobel LSP] Process exited with code ${code}`);
            this.process = null;
            this.isReady = false;
        });

        // Initialize handshake
        this.sendRequest('initialize', {
            processId: process.pid,
            rootUri: (workspaceFolders && workspaceFolders.length > 0) ? workspaceFolders[0].uri.toString() : null,
            rootPath: cwd || null,
            workspaceFolders: workspaceFolders ? workspaceFolders.map(f => ({ uri: f.uri.toString(), name: f.name })) : null,
            capabilities: {
                textDocument: {
                    hover: { contentFormat: ['markdown', 'plaintext'] },
                    definition: { dynamicRegistration: true },
                    formatting: { dynamicRegistration: true },
                    documentSymbol: { dynamicRegistration: true },
                    completion: { dynamicRegistration: true }
                }
            }
        }).then(result => {
            this.isReady = true;
            this.sendNotification('initialized', {});
            console.log('[Kobel LSP] Server initialized successfully:', result.serverInfo);

            // Sync currently open Kobel documents
            vscode.workspace.textDocuments.forEach(doc => {
                if (doc.languageId === 'kobel') {
                    this.notifyDidOpen(doc);
                }
            });
        }).catch(err => {
            console.error('[Kobel LSP] Initialization failed:', err);
        });
    }

    handleData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (true) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) break;

            const headerStr = this.buffer.slice(0, headerEnd).toString('utf8');
            const match = headerStr.match(/Content-Length:\s*(\d+)/i);
            if (!match) break;

            const len = parseInt(match[1], 10);
            const totalMsgLen = headerEnd + 4 + len;
            if (this.buffer.length < totalMsgLen) break;

            const bodyStr = this.buffer.slice(headerEnd + 4, totalMsgLen).toString('utf8');
            this.buffer = this.buffer.slice(totalMsgLen);

            try {
                const msg = JSON.parse(bodyStr);
                this.handleMessage(msg);
            } catch (err) {
                console.error('[Kobel LSP] JSON parse error:', err);
            }
        }
    }

    handleMessage(msg) {
        // Notification: publishDiagnostics
        if (msg.method === 'textDocument/publishDiagnostics') {
            const params = msg.params;
            const uri = vscode.Uri.parse(params.uri);
            const diags = (params.diagnostics || []).map(d => {
                const range = new vscode.Range(
                    d.range.start.line, d.range.start.character,
                    d.range.end.line, d.range.end.character
                );
                const sev = d.severity === 1 ? vscode.DiagnosticSeverity.Error :
                            d.severity === 2 ? vscode.DiagnosticSeverity.Warning :
                            vscode.DiagnosticSeverity.Information;
                return new vscode.Diagnostic(range, d.message, sev);
            });
            this.diagnosticsCollection.set(uri, diags);
            return;
        }

        // Response to request
        if (msg.id !== undefined && this.pendingRequests.has(msg.id)) {
            const { resolve, reject } = this.pendingRequests.get(msg.id);
            this.pendingRequests.delete(msg.id);
            if (msg.error) {
                reject(new Error(msg.error.message));
            } else {
                resolve(msg.result);
            }
        }
    }

    sendRaw(payload) {
        if (!this.process || !this.process.stdin.writable) return;
        const len = Buffer.byteLength(payload, 'utf8');
        this.process.stdin.write(`Content-Length: ${len}\r\n\r\n${payload}`);
    }

    sendRequest(method, params) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            this.pendingRequests.set(id, { resolve, reject });
            this.sendRaw(JSON.stringify({
                jsonrpc: '2.0',
                id,
                method,
                params
            }));
        });
    }

    sendNotification(method, params) {
        this.sendRaw(JSON.stringify({
            jsonrpc: '2.0',
            method,
            params
        }));
    }

    notifyDidOpen(doc) {
        this.sendNotification('textDocument/didOpen', {
            textDocument: {
                uri: doc.uri.toString(),
                languageId: 'kobel',
                version: doc.version,
                text: doc.getText()
            }
        });
    }

    notifyDidChange(doc) {
        this.sendNotification('textDocument/didChange', {
            textDocument: {
                uri: doc.uri.toString(),
                version: doc.version
            },
            contentChanges: [{ text: doc.getText() }]
        });
    }

    notifyDidClose(doc) {
        this.sendNotification('textDocument/didClose', {
            textDocument: {
                uri: doc.uri.toString()
            }
        });
        this.diagnosticsCollection.delete(doc.uri);
    }

    dispose() {
        if (this.process) {
            this.sendRequest('shutdown', null).finally(() => {
                this.sendNotification('exit', null);
                if (this.process) this.process.kill();
            });
        }
        this.diagnosticsCollection.dispose();
    }
}

/**
 * VS Code Extension Activation
 */
function activate(context) {
    const compilerPath = resolveCompilerPath();
    console.log(`[Kobel Extension] Activating with compiler: ${compilerPath}`);

    const client = new KobelLanguageClient(compilerPath);

    // 1. Document Synchronization
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'kobel') client.notifyDidOpen(doc);
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.document.languageId === 'kobel') client.notifyDidChange(event.document);
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (doc.languageId === 'kobel') client.notifyDidClose(doc);
        })
    );

    // 2. Hover Provider
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('kobel', {
            async provideHover(document, position) {
                try {
                    const res = await client.sendRequest('textDocument/hover', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (res && res.contents) {
                        return new vscode.Hover(new vscode.MarkdownString(res.contents.value));
                    }
                } catch (e) {
                    console.error('[Kobel LSP] Hover error:', e);
                }
                return null;
            }
        })
    );

    // 3. Definition Provider (F12)
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider('kobel', {
            async provideDefinition(document, position) {
                try {
                    const res = await client.sendRequest('textDocument/definition', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (res && res.uri && res.range) {
                        const targetUri = vscode.Uri.parse(res.uri);
                        const targetRange = new vscode.Range(
                            res.range.start.line, res.range.start.character,
                            res.range.end.line, res.range.end.character
                        );
                        return new vscode.Location(targetUri, targetRange);
                    }
                } catch (e) {
                    console.error('[Kobel LSP] Definition error:', e);
                }
                return null;
            }
        })
    );

    // 4. Formatting Provider (Shift+Alt+F & Format on Save)
    context.subscriptions.push(
        vscode.languages.registerDocumentFormattingEditProvider('kobel', {
            async provideDocumentFormattingEdits(document, options) {
                try {
                    const edits = await client.sendRequest('textDocument/formatting', {
                        textDocument: { uri: document.uri.toString() },
                        options: {
                            tabSize: options.tabSize,
                            insertSpaces: options.insertSpaces
                        }
                    });
                    if (Array.isArray(edits)) {
                        return edits.map(e => {
                            const range = new vscode.Range(
                                e.range.start.line, e.range.start.character,
                                document.lineCount, 0
                            );
                            return vscode.TextEdit.replace(range, e.newText);
                        });
                    }
                } catch (e) {
                    console.error('[Kobel LSP] Formatting error:', e);
                }
                return [];
            }
        })
    );

    // 5. Document Symbol Provider (Outline & Breadcrumbs)
    context.subscriptions.push(
        vscode.languages.registerDocumentSymbolProvider('kobel', {
            async provideDocumentSymbols(document) {
                try {
                    const symbols = await client.sendRequest('textDocument/documentSymbol', {
                        textDocument: { uri: document.uri.toString() }
                    });
                    if (Array.isArray(symbols)) {
                        return symbols.map(s => {
                            const range = new vscode.Range(
                                s.range.start.line, s.range.start.character,
                                s.range.end.line, s.range.end.character
                            );
                            const kind = s.kind === 12 ? vscode.SymbolKind.Function :
                                         s.kind === 23 ? vscode.SymbolKind.Struct :
                                         s.kind === 10 ? vscode.SymbolKind.Enum :
                                         s.kind === 11 ? vscode.SymbolKind.Interface :
                                         s.kind === 5 ? vscode.SymbolKind.Class :
                                         s.kind === 14 ? vscode.SymbolKind.Constant :
                                         vscode.SymbolKind.Variable;
                            return new vscode.DocumentSymbol(s.name, '', kind, range, range);
                        });
                    }
                } catch (e) {
                    console.error('[Kobel LSP] Document symbol error:', e);
                }
                return [];
            }
        })
    );

    // 6. Completion Provider
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('kobel', {
            async provideCompletionItems(document, position) {
                try {
                    const items = await client.sendRequest('textDocument/completion', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (Array.isArray(items)) {
                        return items.map(it => {
                            const item = new vscode.CompletionItem(it.label);
                            item.kind = it.kind === 14 ? vscode.CompletionItemKind.Keyword :
                                        it.kind === 3 ? vscode.CompletionItemKind.Function :
                                        it.kind === 22 ? vscode.CompletionItemKind.Struct :
                                        it.kind === 13 ? vscode.CompletionItemKind.Enum :
                                        it.kind === 7 ? vscode.CompletionItemKind.Class :
                                        it.kind === 21 ? vscode.CompletionItemKind.Constant :
                                        vscode.CompletionItemKind.Variable;
                            if (it.detail) item.detail = it.detail;
                            return item;
                        });
                    }
                } catch (e) {
                    console.error('[Kobel LSP] Completion error:', e);
                }
                return [];
            }
        }, '.', ':')
    );

    // 7. Explicit Command 'kobel.format'
    context.subscriptions.push(
        vscode.commands.registerCommand('kobel.format', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'kobel') return;
            await vscode.commands.executeCommand('editor.action.formatDocument');
        })
    );

    context.subscriptions.push({
        dispose: () => client.dispose()
    });
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
