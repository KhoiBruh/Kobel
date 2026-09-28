const vscode = require('vscode');
const { execFile } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

/**
 * Formats Kobel source code using `kobel fmt`.
 */
function formatKobelDocument(document) {
    return new Promise((resolve, reject) => {
        const text = document.getText();
        if (!text || text.trim().length === 0) {
            return resolve([]);
        }

        const config = vscode.workspace.getConfiguration('kobel');
        let compilerPath = config.get('compilerPath');
        
        // If not explicitly configured, look for kobel_v1.exe in workspace or 'kobel' in PATH
        if (!compilerPath) {
            const workspaceFolders = vscode.workspace.workspaceFolders;
            if (workspaceFolders && workspaceFolders.length > 0) {
                const localExe = path.join(workspaceFolders[0].uri.fsPath, 'kobel_v1.exe');
                if (fs.existsSync(localExe)) {
                    compilerPath = localExe;
                }
            }
        }
        if (!compilerPath) {
            compilerPath = 'kobel';
        }

        // Write document content to a temporary file
        const tmpDir = os.tmpdir();
        const tmpFile = path.join(tmpDir, `kobel_fmt_${Date.now()}_${Math.random().toString(36).substring(7)}.kb`);

        fs.writeFile(tmpFile, text, 'utf8', err => {
            if (err) {
                console.error('Failed to create temporary file for formatting:', err);
                return resolve([]);
            }

            // Run: <compilerPath> fmt <tmpFile>
            execFile(compilerPath, ['fmt', tmpFile], { encoding: 'utf8' }, (execErr, stdout, stderr) => {
                // Clean up temp file
                fs.unlink(tmpFile, () => {});

                if (execErr) {
                    console.error('kobel fmt execution failed:', execErr, stderr);
                    vscode.window.showWarningMessage(
                        `Kobel format failed: ${stderr || execErr.message}`
                    );
                    return resolve([]);
                }

                if (stdout && stdout.length > 0) {
                    const fullRange = new vscode.Range(
                        document.positionAt(0),
                        document.positionAt(text.length)
                    );
                    resolve([vscode.TextEdit.replace(fullRange, stdout)]);
                } else {
                    resolve([]);
                }
            });
        });
    });
}

function activate(context) {
    // 1. Register Document Formatting Provider (for Shift+Alt+F & Format on Save)
    const formatProvider = vscode.languages.registerDocumentFormattingEditProvider('kobel', {
        provideDocumentFormattingEdits(document) {
            return formatKobelDocument(document);
        }
    });

    // 2. Register explicit Command 'kobel.format'
    const formatCommand = vscode.commands.registerCommand('kobel.format', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor || editor.document.languageId !== 'kobel') return;

        const edits = await formatKobelDocument(editor.document);
        if (edits && edits.length > 0) {
            const workEdits = new vscode.WorkspaceEdit();
            workEdits.set(editor.document.uri, edits);
            await vscode.workspace.applyEdit(workEdits);
        }
    });

    context.subscriptions.push(formatProvider, formatCommand);
}

function deactivate() {}

module.exports = {
    activate,
    deactivate
};
