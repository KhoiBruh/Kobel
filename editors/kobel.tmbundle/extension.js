const vscode = require('vscode');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

/**
 * Resolves the path to the Kobel compiler executable.
 */
function resolveCompilerPath() {
    const config = vscode.workspace.getConfiguration('kobel');
    const configured = (config.get('compilerPath') || '').trim();
    const workspaceFolders = vscode.workspace.workspaceFolders || [];

    if (configured) {
        if (path.isAbsolute(configured) && fs.existsSync(configured)) {
            return configured;
        }
        for (const folder of workspaceFolders) {
            const candidate = path.join(folder.uri.fsPath, configured);
            if (fs.existsSync(candidate)) {
                return candidate;
            }
        }
        if (fs.existsSync(configured)) {
            return configured;
        }
    }

    const candidates = process.platform === 'win32'
        ? [
            path.join('build', 'kobel.exe'),
            path.join('build', 'seed', 'kobel_seed.exe'),
            'kobel_v1.exe',
            'kobel.exe'
        ]
        : [
            path.join('build', 'kobel'),
            path.join('build', 'seed', 'kobel_seed'),
            'kobel_v1',
            'kobel'
        ];

    for (const folder of workspaceFolders) {
        for (const rel of candidates) {
            const full = path.join(folder.uri.fsPath, rel);
            if (fs.existsSync(full)) {
                return full;
            }
        }
    }

    return 'kobel';
}

function toVscodePosition(pos) {
    return new vscode.Position(
        Math.max(0, Number(pos?.line || 0)),
        Math.max(0, Number(pos?.character || 0))
    );
}

function toVscodeRange(r) {
    if (!r || !r.start || !r.end) {
        return new vscode.Range(0, 0, 0, 0);
    }
    return new vscode.Range(toVscodePosition(r.start), toVscodePosition(r.end));
}

function mapSymbolKind(kind) {
    switch (kind) {
        case 2: return vscode.SymbolKind.Module;
        case 5: return vscode.SymbolKind.Class;
        case 6: return vscode.SymbolKind.Method;
        case 7: return vscode.SymbolKind.Property;
        case 8: return vscode.SymbolKind.Field;
        case 9: return vscode.SymbolKind.Constructor;
        case 10: return vscode.SymbolKind.Enum;
        case 11: return vscode.SymbolKind.Interface;
        case 12: return vscode.SymbolKind.Function;
        case 13: return vscode.SymbolKind.Variable;
        case 14: return vscode.SymbolKind.Constant;
        case 22: return vscode.SymbolKind.EnumMember;
        case 23: return vscode.SymbolKind.Struct;
        case 25: return vscode.SymbolKind.Operator;
        default: return vscode.SymbolKind.Variable;
    }
}

function mapCompletionKind(kind) {
    switch (kind) {
        case 2: return vscode.CompletionItemKind.Method;
        case 3: return vscode.CompletionItemKind.Function;
        case 4: return vscode.CompletionItemKind.Constructor;
        case 5: return vscode.CompletionItemKind.Field;
        case 6: return vscode.CompletionItemKind.Variable;
        case 7: return vscode.CompletionItemKind.Class;
        case 8: return vscode.CompletionItemKind.Interface;
        case 9: return vscode.CompletionItemKind.Module;
        case 10: return vscode.CompletionItemKind.Property;
        case 13: return vscode.CompletionItemKind.Enum;
        case 14: return vscode.CompletionItemKind.Keyword;
        case 15: return vscode.CompletionItemKind.Snippet;
        case 20: return vscode.CompletionItemKind.EnumMember;
        case 21: return vscode.CompletionItemKind.Constant;
        case 22: return vscode.CompletionItemKind.Struct;
        case 24: return vscode.CompletionItemKind.Operator;
        default: return vscode.CompletionItemKind.Text;
    }
}

function mapDocumentSymbol(s) {
    const fullRange = toVscodeRange(s.range);
    const selRange = s.selectionRange ? toVscodeRange(s.selectionRange) : fullRange;
    const sym = new vscode.DocumentSymbol(
        s.name || '<unnamed>',
        s.detail || '',
        mapSymbolKind(s.kind),
        fullRange,
        selRange
    );
    if (Array.isArray(s.children) && s.children.length > 0) {
        sym.children = s.children.map(mapDocumentSymbol);
    }
    return sym;
}

function mapLocation(loc) {
    if (!loc || !loc.uri || !loc.range) return null;
    return new vscode.Location(vscode.Uri.parse(loc.uri), toVscodeRange(loc.range));
}

function mapWorkspaceEdit(lspEdit) {
    const wsEdit = new vscode.WorkspaceEdit();
    if (!lspEdit || !lspEdit.changes || typeof lspEdit.changes !== 'object') {
        return wsEdit;
    }
    for (const [uriStr, edits] of Object.entries(lspEdit.changes)) {
        if (!Array.isArray(edits)) continue;
        const targetUri = vscode.Uri.parse(uriStr);
        const textEdits = edits.map(e => new vscode.TextEdit(toVscodeRange(e.range), e.newText || ''));
        wsEdit.set(targetUri, textEdits);
    }
    return wsEdit;
}

function mapCodeActionKind(kindStr) {
    switch (kindStr) {
        case 'quickfix': return vscode.CodeActionKind.QuickFix;
        case 'refactor': return vscode.CodeActionKind.Refactor;
        case 'refactor.rewrite': return vscode.CodeActionKind.RefactorRewrite;
        case 'source.organizeImports': return vscode.CodeActionKind.SourceOrganizeImports;
        default: return vscode.CodeActionKind.Empty;
    }
}

/**
 * Lightweight, zero-dependency Language Server Protocol Client for Kobel.
 */
class KobelLanguageClient {
    constructor(compilerPath, outputChannel) {
        this.compilerPath = compilerPath;
        this.output = outputChannel;
        this.nextId = 1;
        this.pendingRequests = new Map();
        this.pendingSyncTimers = new Map();
        this.diagnosticsCollection = vscode.languages.createDiagnosticCollection('kobel');
        this.buffer = Buffer.alloc(0);
        this.process = null;
        this.isReady = false;
        this.disposed = false;
        this.start();
    }

    log(msg) {
        this.output.appendLine(msg);
    }

    start() {
        if (this.disposed) return;

        const workspaceFolders = vscode.workspace.workspaceFolders;
        const cwd = (workspaceFolders && workspaceFolders.length > 0)
            ? workspaceFolders[0].uri.fsPath
            : undefined;

        this.log(`[Kobel LSP] Starting: "${this.compilerPath}" lsp (cwd=${cwd || '<none>'})`);

        try {
            this.process = spawn(this.compilerPath, ['lsp'], {
                cwd: cwd,
                stdio: ['pipe', 'pipe', 'pipe']
            });
        } catch (err) {
            this.log(`[Kobel LSP] Failed to spawn: ${err.message}`);
            vscode.window.showErrorMessage(`Failed to start Kobel Language Server: ${err.message}`);
            return;
        }

        const proc = this.process;
        proc.stdout.on('data', chunk => {
            if (this.process === proc) this.handleData(chunk);
        });
        proc.stderr.on('data', data => {
            this.log(`[Kobel LSP stderr] ${data.toString().trimEnd()}`);
        });

        proc.on('error', err => {
            this.log(`[Kobel LSP] Process error: ${err.message}`);
            vscode.window.showErrorMessage(`Kobel Language Server error (${this.compilerPath}): ${err.message}`);
        });

        proc.on('exit', code => {
            this.log(`[Kobel LSP] Process exited with code ${code}`);
            if (this.process === proc) {
                this.process = null;
                this.isReady = false;
                this.rejectAllPending(new Error(`Kobel LSP exited with code ${code}`));
            }
        });

        this.sendRequest('initialize', {
            processId: process.pid,
            rootUri: (workspaceFolders && workspaceFolders.length > 0) ? workspaceFolders[0].uri.toString() : null,
            rootPath: cwd || null,
            workspaceFolders: workspaceFolders
                ? workspaceFolders.map(f => ({ uri: f.uri.toString(), name: f.name }))
                : null,
            capabilities: {
                workspace: {
                    symbol: { dynamicRegistration: true },
                    workspaceEdit: { documentChanges: false }
                },
                textDocument: {
                    hover: { contentFormat: ['markdown', 'plaintext'] },
                    definition: { dynamicRegistration: true },
                    references: { dynamicRegistration: true },
                    documentHighlight: { dynamicRegistration: true },
                    formatting: { dynamicRegistration: true },
                    documentSymbol: { hierarchicalDocumentSymbolSupport: true },
                    completion: { completionItem: { snippetSupport: false } },
                    signatureHelp: { signatureInformation: { parameterInformation: { labelOffsetSupport: true } } },
                    rename: { prepareSupport: true },
                    codeAction: { codeActionLiteralSupport: { codeActionKind: { valueSet: ['quickfix', 'refactor', 'refactor.rewrite', 'source.organizeImports'] } } }
                }
            }
        }).then(result => {
            this.isReady = true;
            this.sendNotification('initialized', {});
            const info = result?.serverInfo ? `${result.serverInfo.name} ${result.serverInfo.version}` : 'ok';
            this.log(`[Kobel LSP] Server initialized (${info})`);

            vscode.workspace.textDocuments.forEach(doc => {
                if (doc.languageId === 'kobel') {
                    this.notifyDidOpen(doc);
                }
            });
        }).catch(err => {
            this.log(`[Kobel LSP] Initialization failed: ${err.message}`);
        });
    }

    restart(newCompilerPath) {
        if (newCompilerPath) {
            this.compilerPath = newCompilerPath;
        }
        this.clearSyncTimers();
        this.rejectAllPending(new Error('Kobel LSP restarting'));
        this.diagnosticsCollection.clear();
        this.buffer = Buffer.alloc(0);
        this.isReady = false;

        if (this.process) {
            const old = this.process;
            this.process = null;
            try { old.kill(); } catch (_) {}
        }
        this.start();
    }

    clearSyncTimers() {
        for (const timer of this.pendingSyncTimers.values()) {
            clearTimeout(timer);
        }
        this.pendingSyncTimers.clear();
    }

    rejectAllPending(err) {
        for (const entry of this.pendingRequests.values()) {
            clearTimeout(entry.timer);
            entry.reject(err);
        }
        this.pendingRequests.clear();
    }

    handleData(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (true) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) break;

            const headerStr = this.buffer.slice(0, headerEnd).toString('utf8');
            const match = headerStr.match(/Content-Length:\s*(\d+)/i);
            if (!match) {
                this.buffer = this.buffer.slice(headerEnd + 4);
                continue;
            }

            const len = parseInt(match[1], 10);
            const totalMsgLen = headerEnd + 4 + len;
            if (this.buffer.length < totalMsgLen) break;

            const bodyStr = this.buffer.slice(headerEnd + 4, totalMsgLen).toString('utf8');
            this.buffer = this.buffer.slice(totalMsgLen);

            try {
                const msg = JSON.parse(bodyStr);
                this.handleMessage(msg);
            } catch (err) {
                this.log(`[Kobel LSP] JSON parse error: ${err.message}`);
            }
        }
    }

    handleMessage(msg) {
        if (msg.method === 'textDocument/publishDiagnostics') {
            const params = msg.params || {};
            if (!params.uri) return;
            const uri = vscode.Uri.parse(params.uri);
            const diags = (params.diagnostics || []).map(d => {
                const range = toVscodeRange(d.range);
                const sev = d.severity === 1 ? vscode.DiagnosticSeverity.Error
                    : d.severity === 2 ? vscode.DiagnosticSeverity.Warning
                    : d.severity === 3 ? vscode.DiagnosticSeverity.Information
                    : vscode.DiagnosticSeverity.Hint;
                const diag = new vscode.Diagnostic(range, d.message || '', sev);
                diag.source = d.source || 'kobel';
                return diag;
            });
            this.diagnosticsCollection.set(uri, diags);
            return;
        }

        if (msg.id !== undefined && this.pendingRequests.has(msg.id)) {
            const entry = this.pendingRequests.get(msg.id);
            this.pendingRequests.delete(msg.id);
            clearTimeout(entry.timer);
            if (msg.error) {
                entry.reject(new Error(msg.error.message || 'LSP request failed'));
            } else {
                entry.resolve(msg.result);
            }
        }
    }

    sendRaw(payload) {
        if (!this.process || !this.process.stdin.writable) return false;
        const len = Buffer.byteLength(payload, 'utf8');
        this.process.stdin.write(`Content-Length: ${len}\r\n\r\n${payload}`);
        return true;
    }

    sendRequest(method, params) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`LSP request '${method}' timed out`));
                }
            }, 15000);

            this.pendingRequests.set(id, { resolve, reject, timer });
            const ok = this.sendRaw(JSON.stringify({
                jsonrpc: '2.0',
                id,
                method,
                params
            }));
            if (!ok) {
                this.pendingRequests.delete(id);
                clearTimeout(timer);
                reject(new Error('Kobel LSP process is not running'));
            }
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
        const key = doc.uri.toString();
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
        }
        this.sendNotification('textDocument/didOpen', {
            textDocument: {
                uri: key,
                languageId: 'kobel',
                version: doc.version,
                text: doc.getText()
            }
        });
    }

    scheduleDidChange(doc) {
        const key = doc.uri.toString();
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
        }
        const timer = setTimeout(() => {
            this.pendingSyncTimers.delete(key);
            this.notifyDidChange(doc);
        }, 120);
        this.pendingSyncTimers.set(key, timer);
    }

    flushDidChange(doc) {
        if (!doc) return;
        const key = doc.uri.toString();
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
            this.notifyDidChange(doc);
        }
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
        const key = doc.uri.toString();
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
        }
        this.sendNotification('textDocument/didClose', {
            textDocument: { uri: key }
        });
        this.diagnosticsCollection.delete(doc.uri);
    }

    dispose() {
        this.disposed = true;
        this.clearSyncTimers();
        this.rejectAllPending(new Error('Client disposed'));
        if (this.process) {
            const proc = this.process;
            this.sendRaw(JSON.stringify({ jsonrpc: '2.0', id: this.nextId++, method: 'shutdown', params: null }));
            this.sendNotification('exit', null);
            setTimeout(() => {
                try { proc.kill(); } catch (_) {}
            }, 300);
            this.process = null;
        }
        this.diagnosticsCollection.dispose();
    }
}

/**
 * VS Code Extension Activation
 */
function activate(context) {
    const outputChannel = vscode.window.createOutputChannel('Kobel Language Server');
    const compilerPath = resolveCompilerPath();
    const client = new KobelLanguageClient(compilerPath, outputChannel);

    // 1. Document Synchronization
    context.subscriptions.push(
        outputChannel,
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (doc.languageId === 'kobel') client.notifyDidOpen(doc);
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.document.languageId === 'kobel') client.scheduleDidChange(event.document);
        }),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (doc.languageId === 'kobel') client.flushDidChange(doc);
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (doc.languageId === 'kobel') client.notifyDidClose(doc);
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('kobel.compilerPath')) {
                client.restart(resolveCompilerPath());
            }
        })
    );

    // 2. Hover Provider
    context.subscriptions.push(
        vscode.languages.registerHoverProvider('kobel', {
            async provideHover(document, position) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/hover', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (res && res.contents && res.contents.value) {
                        const md = new vscode.MarkdownString(res.contents.value);
                        return res.range
                            ? new vscode.Hover(md, toVscodeRange(res.range))
                            : new vscode.Hover(md);
                    }
                } catch (e) {
                    client.log(`[Hover error] ${e.message}`);
                }
                return null;
            }
        })
    );

    // 3. Go to Definition Provider (F12 / Ctrl+Click)
    context.subscriptions.push(
        vscode.languages.registerDefinitionProvider('kobel', {
            async provideDefinition(document, position) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/definition', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (Array.isArray(res)) {
                        return res.map(mapLocation).filter(Boolean);
                    }
                    return mapLocation(res);
                } catch (e) {
                    client.log(`[Definition error] ${e.message}`);
                }
                return null;
            }
        })
    );

    // 4. Find All References Provider (Shift+F12)
    context.subscriptions.push(
        vscode.languages.registerReferenceProvider('kobel', {
            async provideReferences(document, position, contextInfo) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/references', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character },
                        context: { includeDeclaration: Boolean(contextInfo?.includeDeclaration) }
                    });
                    if (Array.isArray(res)) {
                        return res.map(mapLocation).filter(Boolean);
                    }
                } catch (e) {
                    client.log(`[References error] ${e.message}`);
                }
                return [];
            }
        })
    );

    // 5. Global Semantic Rename Provider (F2)
    context.subscriptions.push(
        vscode.languages.registerRenameProvider('kobel', {
            async prepareRename(document, position) {
                client.flushDidChange(document);
                const res = await client.sendRequest('textDocument/prepareRename', {
                    textDocument: { uri: document.uri.toString() },
                    position: { line: position.line, character: position.character }
                });
                if (!res || !res.range) {
                    throw new Error('Symbol cannot be renamed here');
                }
                return {
                    range: toVscodeRange(res.range),
                    placeholder: res.placeholder || document.getText(toVscodeRange(res.range))
                };
            },
            async provideRenameEdits(document, position, newName) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/rename', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character },
                        newName
                    });
                    if (res && res.changes) {
                        return mapWorkspaceEdit(res);
                    }
                } catch (e) {
                    client.log(`[Rename error] ${e.message}`);
                }
                return null;
            }
        })
    );

    // 6. Code Actions Provider (QuickFix / Organize Imports / Rewrite)
    context.subscriptions.push(
        vscode.languages.registerCodeActionsProvider(
            'kobel',
            {
                async provideCodeActions(document, range, actionContext) {
                    try {
                        client.flushDidChange(document);
                        const diags = (actionContext.diagnostics || []).map(d => ({
                            range: {
                                start: { line: d.range.start.line, character: d.range.start.character },
                                end: { line: d.range.end.line, character: d.range.end.character }
                            },
                            message: d.message,
                            severity: d.severity === vscode.DiagnosticSeverity.Error ? 1
                                : d.severity === vscode.DiagnosticSeverity.Warning ? 2 : 3,
                            source: d.source || 'kobel'
                        }));

                        const res = await client.sendRequest('textDocument/codeAction', {
                            textDocument: { uri: document.uri.toString() },
                            range: {
                                start: { line: range.start.line, character: range.start.character },
                                end: { line: range.end.line, character: range.end.character }
                            },
                            context: { diagnostics: diags }
                        });
                        if (Array.isArray(res)) {
                            return res.map(a => {
                                const action = new vscode.CodeAction(a.title || 'Fix', mapCodeActionKind(a.kind));
                                if (a.isPreferred) action.isPreferred = true;
                                if (a.edit) action.edit = mapWorkspaceEdit(a.edit);
                                return action;
                            });
                        }
                    } catch (e) {
                        client.log(`[CodeAction error] ${e.message}`);
                    }
                    return [];
                }
            },
            {
                providedCodeActionKinds: [
                    vscode.CodeActionKind.QuickFix,
                    vscode.CodeActionKind.RefactorRewrite,
                    vscode.CodeActionKind.SourceOrganizeImports
                ]
            }
        )
    );

    // 7. Document Highlight Provider (Read / Write occurrences)
    context.subscriptions.push(
        vscode.languages.registerDocumentHighlightProvider('kobel', {
            async provideDocumentHighlights(document, position) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/documentHighlight', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (Array.isArray(res)) {
                        return res.map(h => {
                            const kind = h.kind === 3 ? vscode.DocumentHighlightKind.Write
                                : h.kind === 2 ? vscode.DocumentHighlightKind.Read
                                : vscode.DocumentHighlightKind.Text;
                            return new vscode.DocumentHighlight(toVscodeRange(h.range), kind);
                        });
                    }
                } catch (e) {
                    client.log(`[Highlight error] ${e.message}`);
                }
                return [];
            }
        })
    );

    // 8. Signature Help Provider
    context.subscriptions.push(
        vscode.languages.registerSignatureHelpProvider('kobel', {
            async provideSignatureHelp(document, position) {
                try {
                    client.flushDidChange(document);
                    const res = await client.sendRequest('textDocument/signatureHelp', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (!res || !Array.isArray(res.signatures) || res.signatures.length === 0) {
                        return null;
                    }
                    const help = new vscode.SignatureHelp();
                    help.activeSignature = Number(res.activeSignature || 0);
                    help.activeParameter = Number(res.activeParameter || 0);
                    help.signatures = res.signatures.map(sig => {
                        const info = new vscode.SignatureInformation(
                            sig.label || '',
                            sig.documentation ? new vscode.MarkdownString(sig.documentation.value || sig.documentation) : undefined
                        );
                        if (Array.isArray(sig.parameters)) {
                            info.parameters = sig.parameters.map(p => new vscode.ParameterInformation(p.label));
                        }
                        return info;
                    });
                    return help;
                } catch (e) {
                    client.log(`[SignatureHelp error] ${e.message}`);
                }
                return null;
            }
        }, '(', ',')
    );

    // 9. Document Formatting Provider (Shift+Alt+F & Format on Save)
    context.subscriptions.push(
        vscode.languages.registerDocumentFormattingEditProvider('kobel', {
            async provideDocumentFormattingEdits(document, options) {
                try {
                    client.flushDidChange(document);
                    const edits = await client.sendRequest('textDocument/formatting', {
                        textDocument: { uri: document.uri.toString() },
                        options: {
                            tabSize: options.tabSize,
                            insertSpaces: options.insertSpaces
                        }
                    });
                    if (Array.isArray(edits)) {
                        return edits.map(e => new vscode.TextEdit(toVscodeRange(e.range), e.newText || ''));
                    }
                } catch (e) {
                    client.log(`[Formatting error] ${e.message}`);
                }
                return [];
            }
        })
    );

    // 10. Hierarchical Document Symbol Provider (Outline & Breadcrumbs)
    context.subscriptions.push(
        vscode.languages.registerDocumentSymbolProvider('kobel', {
            async provideDocumentSymbols(document) {
                try {
                    client.flushDidChange(document);
                    const symbols = await client.sendRequest('textDocument/documentSymbol', {
                        textDocument: { uri: document.uri.toString() }
                    });
                    if (Array.isArray(symbols)) {
                        return symbols.map(mapDocumentSymbol);
                    }
                } catch (e) {
                    client.log(`[DocumentSymbol error] ${e.message}`);
                }
                return [];
            }
        })
    );

    // 11. Workspace Symbol Provider (Ctrl+T)
    context.subscriptions.push(
        vscode.languages.registerWorkspaceSymbolProvider({
            async provideWorkspaceSymbols(query) {
                try {
                    const res = await client.sendRequest('workspace/symbol', { query: query || '' });
                    if (Array.isArray(res)) {
                        return res.map(s => {
                            const loc = mapLocation(s.location);
                            if (!loc) return null;
                            return new vscode.SymbolInformation(
                                s.name || '<unnamed>',
                                mapSymbolKind(s.kind),
                                s.containerName || '',
                                loc
                            );
                        }).filter(Boolean);
                    }
                } catch (e) {
                    client.log(`[WorkspaceSymbol error] ${e.message}`);
                }
                return [];
            }
        })
    );

    // 12. Completion Provider
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('kobel', {
            async provideCompletionItems(document, position) {
                try {
                    client.flushDidChange(document);
                    const items = await client.sendRequest('textDocument/completion', {
                        textDocument: { uri: document.uri.toString() },
                        position: { line: position.line, character: position.character }
                    });
                    if (Array.isArray(items)) {
                        return items.map(it => {
                            const item = new vscode.CompletionItem(it.label, mapCompletionKind(it.kind));
                            if (it.detail) item.detail = it.detail;
                            if (it.documentation) {
                                item.documentation = typeof it.documentation === 'string'
                                    ? it.documentation
                                    : new vscode.MarkdownString(it.documentation.value || '');
                            }
                            if (it.insertText) item.insertText = it.insertText;
                            return item;
                        });
                    }
                } catch (e) {
                    client.log(`[Completion error] ${e.message}`);
                }
                return [];
            }
        }, '.', ':', '(')
    );

    // 13. Explicit Commands
    context.subscriptions.push(
        vscode.commands.registerCommand('kobel.format', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'kobel') return;
            await vscode.commands.executeCommand('editor.action.formatDocument');
        }),
        vscode.commands.registerCommand('kobel.restartServer', () => {
            client.restart(resolveCompilerPath());
            vscode.window.showInformationMessage('Kobel Language Server restarted.');
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
