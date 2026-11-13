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

function isSupportedKobelDoc(doc) {
    if (!doc || doc.languageId !== 'kobel' || !doc.uri) return false;
    const scheme = doc.uri.scheme;
    return scheme === 'file' || scheme === 'untitled';
}

function sameUriString(a, b) {
    return a && b && a.toString() === b.toString();
}

function hashText(text) {
    let h = 2166136261;
    for (let i = 0; i < text.length; i++) {
        h ^= text.charCodeAt(i);
        h = Math.imul(h, 16777619);
    }
    return `${text.length}:${h >>> 0}`;
}

function getWorkspaceRootPath() {
    const folders = vscode.workspace && vscode.workspace.workspaceFolders;
    return (folders && folders.length > 0 && folders[0].uri) ? folders[0].uri.fsPath : process.cwd();
}

function normalizeFsPath(rawPath, rootDir) {
    if (!rawPath) return '';
    const resolved = path.isAbsolute(rawPath)
        ? path.normalize(rawPath)
        : path.resolve(rootDir || getWorkspaceRootPath(), rawPath);
    return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

const ANALYSIS_METHOD_WS = {
    'textDocument/hover': false,
    'textDocument/definition': false,
    'textDocument/documentHighlight': false,
    'textDocument/codeAction': false,
    'textDocument/signatureHelp': false,
    'textDocument/completion': false,
    'textDocument/references': true,
    'textDocument/prepareRename': true,
    'textDocument/rename': true
};
const MAX_SERVER_CACHE = 4;

/**
 * Lightweight, zero-dependency Language Server Protocol Client for Kobel.
 *
 * Manages worker lifetime cleanly so long-lived editor sessions stay tiny:
 * - Launches on demand and automatically sleeps back to 0 MB after `idleSleepMs`
 *   even while tabs stay open (VS Code retains published diagnostics).
 * - Skips re-analyzing unchanged tabs whose dependencies are clean (`stale=false`),
 *   while automatically invalidating dependents when a dependency file changes.
 * - Tracks the server's 4-slot LRU cache and recycles the worker before a cache
 *   miss when the worker analysis budget is full.
 * - On Windows, runs a shadow copy inside build/ so `kobel build` never hits EBUSY.
 */
class KobelLanguageClient {
    constructor(compilerPath, outputChannel, options = {}) {
        this.compilerPath = compilerPath;
        this.output = outputChannel;
        this.idleShutdownMs = typeof options.idleShutdownMs === 'number' ? options.idleShutdownMs : 1000;
        this.idleSleepMs = typeof options.idleSleepMs === 'number' ? options.idleSleepMs : 4000;
        this.recycleThreshold = typeof options.recycleThreshold === 'number' ? options.recycleThreshold : 3;
        this.nextId = 1;
        this.pendingRequests = new Map();
        this.pendingSyncTimers = new Map();
        this.openDocs = new Map();
        this.syncedUris = new Set();
        this.serverCache = [];
        this.docStates = new Map();
        this.pendingAnalyzedHashes = new Map();
        this.analysisOpsCount = 0;
        this.diagnosticsCollection = vscode.languages.createDiagnosticCollection('kobel');
        this.buffer = Buffer.alloc(0);
        this.process = null;
        this.isReady = false;
        this.startPromise = null;
        this.stopPromise = null;
        this.idleTimer = null;
        this.pendingRecycle = false;
        this.recyclePromise = null;
        this.tempHostBinary = null;
        this.disposed = false;

        this.collectInitialOpenDocs();
        if (this.openDocs.size > 0) {
            this.syncVisibleOnStartup();
        }
    }

    log(msg) {
        this.output.appendLine(msg);
    }

    isWorkerActive() {
        return Boolean(this.process && this.isReady);
    }

    getOrCreateDocState(uriStr) {
        let st = this.docStates.get(uriStr);
        if (!st) {
            st = {
                analyzed: false,
                contentHash: '',
                stale: false,
                moduleName: '',
                deps: new Set(),
                docSymbolsHash: '',
                docSymbols: null
            };
            this.docStates.set(uriStr, st);
        }
        return st;
    }

    needsAnalysis(doc) {
        if (!isSupportedKobelDoc(doc)) return false;
        const key = doc.uri.toString();
        const st = this.docStates.get(key);
        if (!st || !st.analyzed || st.stale) return true;
        return st.contentHash !== hashText(doc.getText());
    }

    markPathAndDependentsStale(changedUriStr, changedFsPath, explicitModule) {
        const rootDir = getWorkspaceRootPath();
        const startNorm = changedFsPath
            ? normalizeFsPath(changedFsPath, rootDir)
            : (changedUriStr ? normalizeFsPath(vscode.Uri.parse(changedUriStr).fsPath, rootDir) : '');
        if (!startNorm && !changedUriStr) return;

        const queue = [{ uri: changedUriStr || '', normPath: startNorm, mod: explicitModule || '' }];
        const visited = new Set();

        while (queue.length > 0) {
            const cur = queue.shift();
            const visitKey = `${cur.uri}|${cur.normPath}|${cur.mod}`;
            if (visited.has(visitKey)) continue;
            visited.add(visitKey);

            let curMod = cur.mod;
            if (!curMod && cur.uri) {
                const cst = this.docStates.get(cur.uri);
                if (cst && cst.moduleName) curMod = cst.moduleName;
            }

            for (const [otherUri, st] of this.docStates.entries()) {
                if (otherUri === cur.uri) continue;
                const otherNorm = normalizeFsPath(vscode.Uri.parse(otherUri).fsPath, rootDir);
                const matchesSelf = cur.normPath && otherNorm === cur.normPath;
                if (matchesSelf) {
                    if (!st.stale) {
                        st.stale = true;
                        queue.push({ uri: otherUri, normPath: otherNorm, mod: st.moduleName });
                    }
                    continue;
                }

                const dependsOnPath = Boolean(cur.normPath && st.deps.has(cur.normPath));
                const samePackageMod = Boolean(curMod && st.moduleName && st.moduleName === curMod);
                if ((dependsOnPath || samePackageMod) && !st.stale) {
                    st.stale = true;
                    queue.push({ uri: otherUri, normPath: otherNorm, mod: st.moduleName });
                }
            }
        }
    }

    syncVisibleOnStartup() {
        const activeDoc = vscode.window?.activeTextEditor?.document;
        if (isSupportedKobelDoc(activeDoc) && this.needsAnalysis(activeDoc)) {
            this.ensureStarted(activeDoc.uri.toString()).catch(() => {});
            return;
        }
        const visibleEditors = (vscode.window && Array.isArray(vscode.window.visibleTextEditors))
            ? vscode.window.visibleTextEditors
            : [];
        for (const ed of visibleEditors) {
            if (ed && ed.document && isSupportedKobelDoc(ed.document) && this.needsAnalysis(ed.document)) {
                this.ensureStarted(ed.document.uri.toString()).catch(() => {});
                return;
            }
        }
        // Fallback for headless tests where visibleTextEditors is empty.
        const first = this.openDocs.values().next().value;
        if (first && this.needsAnalysis(first)) {
            this.ensureStarted(first.uri.toString()).catch(() => {});
        }
    }

    cacheHas(uri, version, needWs) {
        const idx = this.serverCache.findIndex(
            e => e.uri === uri && e.version === version && (!needWs || e.isWs)
        );
        if (idx === -1) return false;
        if (idx + 1 < this.serverCache.length) {
            const [item] = this.serverCache.splice(idx, 1);
            this.serverCache.push(item);
        }
        return true;
    }

    cacheRecord(uri, version, isWs) {
        this.serverCache = this.serverCache.filter(e => e.uri !== uri);
        while (this.serverCache.length >= MAX_SERVER_CACHE) {
            this.serverCache.shift();
        }
        this.serverCache.push({ uri, version, isWs: Boolean(isWs) });
    }

    cacheRemoveUri(uri) {
        this.serverCache = this.serverCache.filter(e => e.uri !== uri);
    }

    cacheClear() {
        this.serverCache = [];
    }

    collectInitialOpenDocs() {
        for (const doc of vscode.workspace.textDocuments || []) {
            if (isSupportedKobelDoc(doc)) {
                this.openDocs.set(doc.uri.toString(), doc);
            }
        }
    }

    prepareLaunchPath() {
        const target = this.compilerPath;
        if (process.platform !== 'win32' || !target) return target;
        if (!/(^|[\\/])build[\\/][^\\/]+\.exe$/i.test(target) || !fs.existsSync(target)) {
            return target;
        }

        const dir = path.dirname(target);
        const sharedHost = path.join(dir, 'kobel_lsp_host.exe');
        try {
            const srcStat = fs.statSync(target);
            let needCopy = true;
            if (fs.existsSync(sharedHost)) {
                const dstStat = fs.statSync(sharedHost);
                if (dstStat.size === srcStat.size && Math.abs(dstStat.mtimeMs - srcStat.mtimeMs) < 5) {
                    needCopy = false;
                }
            }
            if (needCopy) {
                fs.copyFileSync(target, sharedHost);
            }
            return sharedHost;
        } catch (err) {
            // Fall back to a per-PID shadow copy if another window holds kobel_lsp_host.exe.
            const pidHost = path.join(dir, `kobel_lsp_host_${process.pid}.exe`);
            try {
                fs.copyFileSync(target, pidHost);
                this.tempHostBinary = pidHost;
                return pidHost;
            } catch (pidErr) {
                this.log(`[Kobel LSP] Shadow copy skipped (${err.code || err.message}); launching direct binary`);
                return target;
            }
        }
    }

    cleanupTempHostBinary() {
        if (!this.tempHostBinary) return;
        try {
            if (fs.existsSync(this.tempHostBinary)) {
                fs.unlinkSync(this.tempHostBinary);
            }
        } catch (_) {}
        this.tempHostBinary = null;
    }

    cancelIdleTimer() {
        if (this.idleTimer) {
            clearTimeout(this.idleTimer);
            this.idleTimer = null;
        }
    }

    scheduleIdleSleep() {
        this.cancelIdleTimer();
        if (this.disposed || !this.process) return;
        const hasOpenDocs = this.openDocs.size > 0;
        const delayMs = hasOpenDocs ? this.idleSleepMs : this.idleShutdownMs;
        if (delayMs <= 0 || !Number.isFinite(delayMs)) return;

        this.idleTimer = setTimeout(() => {
            this.idleTimer = null;
            if (!this.disposed && this.pendingRequests.size === 0 && this.pendingSyncTimers.size === 0 && this.process) {
                const mode = this.openDocs.size === 0 ? 'no files open' : 'idle sleep';
                this.log(`[Kobel LSP] Stopping worker (${mode}) to return heap memory to OS (diagnostics kept in IDE)`);
                this.stopProcess(false).catch(() => {});
            }
        }, delayMs);
    }

    ensureStarted(priorityUri, skipRecycleWait = false) {
        if (this.disposed) {
            return Promise.reject(new Error('Client disposed'));
        }
        if (!skipRecycleWait && this.recyclePromise) {
            return this.recyclePromise.then(() => this.ensureStarted(priorityUri));
        }
        if (this.isReady && this.process) {
            return Promise.resolve();
        }
        if (this.startPromise) {
            return this.startPromise;
        }
        this.startPromise = this.startInternal(priorityUri).finally(() => {
            this.startPromise = null;
        });
        return this.startPromise;
    }

    async startInternal(priorityUri) {
        if (this.stopPromise) {
            await this.stopPromise;
        }
        if (this.disposed) throw new Error('Client disposed');
        if (this.isReady && this.process) return;

        const workspaceFolders = vscode.workspace.workspaceFolders;
        const cwd = (workspaceFolders && workspaceFolders.length > 0)
            ? workspaceFolders[0].uri.fsPath
            : undefined;
        const launchPath = this.prepareLaunchPath();

        this.buffer = Buffer.alloc(0);
        this.syncedUris.clear();
        this.pendingAnalyzedHashes.clear();
        this.cacheClear();
        this.analysisOpsCount = 0;
        this.pendingRecycle = false;

        this.log(`[Kobel LSP] Starting: "${launchPath}" lsp (cwd=${cwd || '<none>'})`);

        try {
            this.process = spawn(launchPath, ['lsp'], {
                cwd: cwd,
                stdio: ['pipe', 'pipe', 'pipe']
            });
        } catch (err) {
            this.log(`[Kobel LSP] Failed to spawn: ${err.message}`);
            vscode.window.showErrorMessage(`Failed to start Kobel Language Server: ${err.message}`);
            throw err;
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
            vscode.window.showErrorMessage(`Kobel Language Server error (${launchPath}): ${err.message}`);
        });

        proc.on('exit', code => {
            this.log(`[Kobel LSP] Process exited with code ${code}`);
            if (this.process === proc) {
                this.process = null;
                this.isReady = false;
                this.syncedUris.clear();
                this.rejectAllPending(new Error(`Kobel LSP exited with code ${code}`));
            }
        });

        try {
            const result = await this.sendRequestRaw('initialize', {
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
            });
            if (this.process !== proc || this.disposed) return;
            this.isReady = true;
            this.sendNotification('initialized', {});
            const info = result?.serverInfo ? `${result.serverInfo.name} ${result.serverInfo.version}` : 'ok';
            this.log(`[Kobel LSP] Server initialized (${info})`);

            this.syncPriorityOpenDocs(priorityUri);
        } catch (err) {
            this.log(`[Kobel LSP] Initialization failed: ${err.message}`);
            throw err;
        }
    }

    syncPriorityOpenDocs(priorityUri) {
        if (priorityUri && this.openDocs.has(priorityUri)) {
            const targetDoc = this.openDocs.get(priorityUri);
            if (targetDoc && !this.syncedUris.has(priorityUri)) {
                this.sendDidOpenImmediate(targetDoc);
                return;
            }
        }
        const visibleUris = new Set();
        const visibleEditors = (vscode.window && Array.isArray(vscode.window.visibleTextEditors))
            ? vscode.window.visibleTextEditors
            : [];
        for (const ed of visibleEditors) {
            if (ed && ed.document && isSupportedKobelDoc(ed.document)) {
                visibleUris.add(ed.document.uri.toString());
            }
        }
        const activeDoc = vscode.window?.activeTextEditor?.document;
        if (isSupportedKobelDoc(activeDoc)) {
            visibleUris.add(activeDoc.uri.toString());
        }

        // Only analyze visible tabs that actually need analysis (first open, changed, or stale).
        let syncedAny = false;
        for (const key of visibleUris) {
            const doc = this.openDocs.get(key);
            if (doc && !this.syncedUris.has(key) && this.needsAnalysis(doc)) {
                this.sendDidOpenImmediate(doc);
                syncedAny = true;
            }
        }
        if (!syncedAny && this.openDocs.size > 0 && visibleUris.size === 0) {
            for (const doc of this.openDocs.values()) {
                if (!this.syncedUris.has(doc.uri.toString()) && this.needsAnalysis(doc)) {
                    this.sendDidOpenImmediate(doc);
                    break;
                }
            }
        }
    }

    stopProcess(clearDiagnostics) {
        if (this.stopPromise) return this.stopPromise;
        this.cancelIdleTimer();
        this.clearSyncTimers();
        this.rejectAllPending(new Error('Kobel LSP stopping'));
        if (clearDiagnostics) {
            this.diagnosticsCollection.clear();
        }
        this.buffer = Buffer.alloc(0);
        this.isReady = false;
        this.syncedUris.clear();
        this.cacheClear();
        this.analysisOpsCount = 0;
        this.pendingRecycle = false;

        const proc = this.process;
        if (!proc) {
            this.cleanupTempHostBinary();
            return Promise.resolve();
        }
        this.process = null;

        this.stopPromise = new Promise(resolve => {
            let done = false;
            const finish = () => {
                if (done) return;
                done = true;
                clearTimeout(forceTimer);
                this.cleanupTempHostBinary();
                this.stopPromise = null;
                resolve();
            };

            proc.once('exit', finish);
            try {
                if (proc.stdin && proc.stdin.writable) {
                    const id = this.nextId++;
                    const shutdownMsg = JSON.stringify({ jsonrpc: '2.0', id, method: 'shutdown', params: null });
                    const len = Buffer.byteLength(shutdownMsg, 'utf8');
                    proc.stdin.write(`Content-Length: ${len}\r\n\r\n${shutdownMsg}`);
                    const exitMsg = JSON.stringify({ jsonrpc: '2.0', method: 'exit', params: null });
                    const exitLen = Buffer.byteLength(exitMsg, 'utf8');
                    proc.stdin.write(`Content-Length: ${exitLen}\r\n\r\n${exitMsg}`);
                }
            } catch (_) {}

            const forceTimer = setTimeout(() => {
                try { proc.kill(); } catch (_) {}
                finish();
            }, 350);
        });
        return this.stopPromise;
    }

    recycle(reason, priorityUri) {
        if (this.disposed) return Promise.resolve();
        if (this.recyclePromise) return this.recyclePromise;
        this.log(`[Kobel LSP] Recycling worker (${reason}) to keep memory bounded`);
        this.recyclePromise = (async () => {
            await this.stopProcess(false);
            if (!this.disposed && this.openDocs.size > 0) {
                await this.ensureStarted(priorityUri, true).catch(() => {});
            }
        })().finally(() => {
            this.recyclePromise = null;
        });
        return this.recyclePromise;
    }

    async restart(newCompilerPath) {
        if (newCompilerPath) {
            this.compilerPath = newCompilerPath;
        }
        await this.stopProcess(true);
        if (!this.disposed && this.openDocs.size > 0) {
            await this.ensureStarted().catch(() => {});
        }
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
            const key = uri.toString();
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

            const st = this.getOrCreateDocState(key);
            const submittedHash = this.pendingAnalyzedHashes.get(key);
            if (submittedHash) {
                st.contentHash = submittedHash;
                this.pendingAnalyzedHashes.delete(key);
            } else if (this.openDocs.has(key)) {
                st.contentHash = hashText(this.openDocs.get(key).getText());
            }
            st.analyzed = true;
            st.stale = false;
            this.maybeTriggerDeferredMaintenance();
            return;
        }

        if (msg.method === 'kobel/fileDependencies') {
            const params = msg.params || {};
            if (!params.uri) return;
            const uri = vscode.Uri.parse(params.uri);
            const key = uri.toString();
            const st = this.getOrCreateDocState(key);
            st.moduleName = params.module || '';
            const rootDir = getWorkspaceRootPath();
            const nextDeps = new Set();
            for (const dep of (params.dependencies || [])) {
                if (typeof dep === 'string' && dep.length > 0) {
                    nextDeps.add(normalizeFsPath(dep, rootDir));
                }
            }
            st.deps = nextDeps;
            this.maybeTriggerDeferredMaintenance();
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
            this.maybeTriggerDeferredMaintenance();
        }
    }

    maybeTriggerDeferredMaintenance() {
        if (this.disposed || this.pendingRequests.size > 0) return;
        if (this.pendingRecycle && this.process) {
            this.pendingRecycle = false;
            this.recycle('deferred threshold').catch(() => {});
            return;
        }
        if (this.process && this.pendingSyncTimers.size === 0) {
            this.scheduleIdleSleep();
        }
    }

    sendRaw(payload) {
        if (!this.process || !this.process.stdin.writable) return false;
        const len = Buffer.byteLength(payload, 'utf8');
        this.process.stdin.write(`Content-Length: ${len}\r\n\r\n${payload}`);
        return true;
    }

    sendRequestRaw(method, params) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`LSP request '${method}' timed out`));
                    this.maybeTriggerDeferredMaintenance();
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

    async sendRequest(method, params) {
        if (this.recyclePromise) {
            await this.recyclePromise;
        }

        const docUri = params && params.textDocument && params.textDocument.uri;
        const hasAnalysisFlag = Object.prototype.hasOwnProperty.call(ANALYSIS_METHOD_WS, method);
        const needWs = hasAnalysisFlag ? ANALYSIS_METHOD_WS[method] : false;

        let targetDoc = null;
        if (docUri) {
            targetDoc = this.openDocs.get(docUri)
                || (vscode.workspace.textDocuments || []).find(d => isSupportedKobelDoc(d) && sameUriString(d.uri, docUri));
            if (targetDoc) {
                this.openDocs.set(docUri, targetDoc);
            }
        }

        const pendingTimerChange = docUri ? this.pendingSyncTimers.has(docUri) : false;
        const isWorkspaceSymMiss = method === 'workspace/symbol' && this.serverCache.length === 0;
        const isDocMiss = Boolean(
            hasAnalysisFlag && targetDoc && (
                !this.syncedUris.has(docUri)
                || pendingTimerChange
                || !this.cacheHas(docUri, targetDoc.version, needWs)
            )
        );

        // Recycle before performing an uncached analysis if the current worker is at capacity.
        if (this.process && this.isReady && (isDocMiss || isWorkspaceSymMiss)
            && this.analysisOpsCount >= this.recycleThreshold && this.pendingRequests.size === 0) {
            const label = docUri ? path.basename(targetDoc?.uri?.fsPath || docUri) : method;
            await this.recycle(`before ${method} on ${label}`, docUri);
        }

        await this.ensureStarted(docUri);

        if (docUri && targetDoc) {
            if (!this.syncedUris.has(docUri)) {
                this.sendDidOpenImmediate(targetDoc);
            } else {
                this.flushDidChange(targetDoc);
            }
            if (hasAnalysisFlag && !this.cacheHas(docUri, targetDoc.version, needWs)) {
                this.analysisOpsCount += needWs ? 2 : 1;
                this.cacheRecord(docUri, targetDoc.version, needWs);
                if (this.analysisOpsCount >= this.recycleThreshold && needWs) {
                    this.pendingRecycle = true;
                }
            }
        } else if (method === 'workspace/symbol' && this.serverCache.length === 0) {
            this.analysisOpsCount += 2;
            if (this.analysisOpsCount >= this.recycleThreshold) {
                this.pendingRecycle = true;
            }
        }

        return this.sendRequestRaw(method, params);
    }

    sendNotification(method, params) {
        this.sendRaw(JSON.stringify({
            jsonrpc: '2.0',
            method,
            params
        }));
    }

    sendDidOpenImmediate(doc) {
        const key = doc.uri.toString();
        const text = doc.getText();
        const alreadyCached = this.cacheHas(key, doc.version, false);
        this.cancelIdleTimer();
        this.syncedUris.add(key);
        this.pendingAnalyzedHashes.set(key, hashText(text));
        if (!alreadyCached) {
            this.analysisOpsCount += 1;
            this.cacheRecord(key, doc.version, false);
        }
        this.sendNotification('textDocument/didOpen', {
            textDocument: {
                uri: key,
                languageId: 'kobel',
                version: doc.version,
                text
            }
        });
    }

    async notifyDidOpen(doc) {
        if (!isSupportedKobelDoc(doc) || this.disposed) return;
        const key = doc.uri.toString();
        this.openDocs.set(key, doc);
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
        }

        // Skip analysis if IDE already holds up-to-date diagnostics and no dependency changed.
        if (!this.needsAnalysis(doc)) {
            return;
        }

        this.cancelIdleTimer();
        if (this.recyclePromise) {
            await this.recyclePromise;
        }

        const st = this.docStates.get(key);
        const forceRefresh = Boolean(st && st.stale);

        // Recycle before analyzing another uncached/stale file if the worker budget is full.
        const isMiss = forceRefresh || !this.syncedUris.has(key) || !this.cacheHas(key, doc.version, false);
        if (this.process && this.isReady && isMiss
            && this.analysisOpsCount >= this.recycleThreshold && this.pendingRequests.size === 0) {
            await this.recycle(`opening ${path.basename(doc.uri.fsPath || key)} after ${this.analysisOpsCount} analyses`, key);
        }

        try {
            await this.ensureStarted(key);
        } catch (_) {
            return;
        }
        if (this.disposed || !this.openDocs.has(key)) return;
        const currentHash = hashText(doc.getText());
        const alreadySentThisVersion = this.syncedUris.has(key)
            && this.pendingAnalyzedHashes.get(key) === currentHash;
        if (!this.syncedUris.has(key) || (forceRefresh && !alreadySentThisVersion)) {
            this.sendDidOpenImmediate(doc);
        } else if (this.needsAnalysis(doc) && !alreadySentThisVersion) {
            await this.notifyDidChange(doc);
        }
    }

    async notifyActiveEditorChanged(editor) {
        if (!editor || !isSupportedKobelDoc(editor.document) || this.disposed) return;
        const doc = editor.document;
        const key = doc.uri.toString();
        this.openDocs.set(key, doc);

        // Stay idle when re-opening a clean, unchanged tab: IDE already keeps previous diagnostics.
        if (!this.needsAnalysis(doc)) {
            return;
        }
        await this.notifyDidOpen(doc);
    }

    scheduleDidChange(doc) {
        if (!isSupportedKobelDoc(doc) || this.disposed) return;
        const key = doc.uri.toString();
        this.openDocs.set(key, doc);
        this.cancelIdleTimer();
        this.markPathAndDependentsStale(key, doc.uri.fsPath);
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
        }
        const timer = setTimeout(() => {
            this.pendingSyncTimers.delete(key);
            this.notifyDidChange(doc);
        }, 180);
        this.pendingSyncTimers.set(key, timer);
    }

    flushDidChange(doc) {
        if (!isSupportedKobelDoc(doc)) return;
        const key = doc.uri.toString();
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
            return this.notifyDidChange(doc);
        }
        return Promise.resolve();
    }

    async notifyDidChange(doc) {
        if (!isSupportedKobelDoc(doc) || this.disposed) return;
        const key = doc.uri.toString();
        this.openDocs.set(key, doc);
        this.cancelIdleTimer();
        this.markPathAndDependentsStale(key, doc.uri.fsPath);

        if (!this.process || !this.isReady) {
            try {
                await this.ensureStarted(key);
            } catch (_) {
                return;
            }
            if (this.disposed || !this.openDocs.has(key)) return;
            if (!this.syncedUris.has(key)) {
                this.sendDidOpenImmediate(doc);
            }
            return;
        }

        if (!this.syncedUris.has(key)) {
            this.sendDidOpenImmediate(doc);
            return;
        }
        if (this.analysisOpsCount >= this.recycleThreshold && this.pendingRequests.size === 0) {
            // Recycling sends the latest text via didOpen in a fresh worker with zero redundant pass.
            await this.recycle(`editing ${path.basename(doc.uri.fsPath || key)}`, key);
            return;
        }
        const text = doc.getText();
        this.pendingAnalyzedHashes.set(key, hashText(text));
        this.analysisOpsCount += 1;
        this.cacheClear();
        this.cacheRecord(key, doc.version, false);
        this.sendNotification('textDocument/didChange', {
            textDocument: {
                uri: key,
                version: doc.version
            },
            contentChanges: [{ text }]
        });
    }

    notifyDidSave(doc) {
        if (!isSupportedKobelDoc(doc) || this.disposed) return;
        const key = doc.uri.toString();
        this.markPathAndDependentsStale(key, doc.uri.fsPath);
        if (this.pendingSyncTimers.has(key) || this.needsAnalysis(doc)) {
            this.flushDidChange(doc);
        }
    }

    notifyExternalFileChange(uri) {
        if (!uri || this.disposed) return;
        const uriStr = uri.toString();
        const st = this.docStates.get(uriStr);
        if (st) {
            const doc = this.openDocs.get(uriStr);
            if (!doc || st.contentHash !== hashText(doc.getText())) {
                st.stale = true;
            }
        }
        this.markPathAndDependentsStale(uriStr, uri.fsPath);
    }

    notifyDidClose(doc) {
        if (!doc || !doc.uri) return;
        const key = doc.uri.toString();
        this.openDocs.delete(key);
        this.syncedUris.delete(key);
        this.docStates.delete(key);
        this.pendingAnalyzedHashes.delete(key);
        this.cacheRemoveUri(key);
        if (this.pendingSyncTimers.has(key)) {
            clearTimeout(this.pendingSyncTimers.get(key));
            this.pendingSyncTimers.delete(key);
        }
        if (this.process && this.isReady) {
            this.sendNotification('textDocument/didClose', {
                textDocument: { uri: key }
            });
        }
        this.diagnosticsCollection.delete(doc.uri);

        if (this.openDocs.size === 0) {
            this.scheduleIdleSleep();
            return;
        }

        if (this.process && this.analysisOpsCount >= this.recycleThreshold) {
            if (this.pendingRequests.size === 0) {
                this.recycle(`closed ${path.basename(doc.uri.fsPath || key)}`).catch(() => {});
            } else {
                this.pendingRecycle = true;
            }
        }
    }

    dispose() {
        this.disposed = true;
        this.cancelIdleTimer();
        this.clearSyncTimers();
        this.rejectAllPending(new Error('Client disposed'));
        if (this.process) {
            const proc = this.process;
            this.sendRaw(JSON.stringify({ jsonrpc: '2.0', id: this.nextId++, method: 'shutdown', params: null }));
            this.sendNotification('exit', null);
            setTimeout(() => {
                try { proc.kill(); } catch (_) {}
                this.cleanupTempHostBinary();
            }, 300);
            this.process = null;
        } else {
            this.cleanupTempHostBinary();
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
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.kb');
    context.subscriptions.push(
        outputChannel,
        watcher,
        watcher.onDidChange(uri => client.notifyExternalFileChange(uri)),
        watcher.onDidCreate(uri => client.notifyExternalFileChange(uri)),
        watcher.onDidDelete(uri => client.notifyExternalFileChange(uri)),
        vscode.workspace.onDidOpenTextDocument(doc => {
            if (isSupportedKobelDoc(doc)) client.notifyDidOpen(doc);
        }),
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor) client.notifyActiveEditorChanged(editor);
        }),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (isSupportedKobelDoc(event.document)) client.scheduleDidChange(event.document);
        }),
        vscode.workspace.onDidSaveTextDocument(doc => {
            if (isSupportedKobelDoc(doc)) client.notifyDidSave(doc);
        }),
        vscode.workspace.onDidCloseTextDocument(doc => {
            if (isSupportedKobelDoc(doc)) client.notifyDidClose(doc);
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
                        // Do not wake a sleeping worker just for automatic cursor-move lightbulb checks
                        // on an already-analyzed clean document.
                        const autoTrigger = vscode.CodeActionTriggerKind ? vscode.CodeActionTriggerKind.Automatic : 2;
                        if (!client.isWorkerActive() && !client.needsAnalysis(document)
                            && (actionContext.triggerKind === undefined || actionContext.triggerKind === autoTrigger)) {
                            return [];
                        }
                        await client.flushDidChange(document);
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
                    if (!client.isWorkerActive() && !client.needsAnalysis(document)) {
                        return [];
                    }
                    await client.flushDidChange(document);
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
                    const key = document.uri.toString();
                    const textHash = hashText(document.getText());
                    const st = client.getOrCreateDocState(key);
                    if (st.docSymbols && st.docSymbolsHash === textHash) {
                        return st.docSymbols;
                    }
                    await client.flushDidChange(document);
                    const symbols = await client.sendRequest('textDocument/documentSymbol', {
                        textDocument: { uri: key }
                    });
                    if (Array.isArray(symbols)) {
                        const mapped = symbols.map(mapDocumentSymbol);
                        st.docSymbolsHash = textHash;
                        st.docSymbols = mapped;
                        return mapped;
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
    deactivate,
    KobelLanguageClient
};
