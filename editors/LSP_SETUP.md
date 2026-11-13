# Kobel Language Server Protocol (LSP) Setup

Kobel includes a self-hosted Language Server Protocol (LSP) server built into the compiler binary:
```bash
kobel lsp
```

## Features Supported
- **Real-time Diagnostics (`textDocument/publishDiagnostics`)**: Instant syntax & semantic error feedback with exact line and column ranges.
- **Hover Information (`textDocument/hover`)**: Unmangled type signatures, struct/enum definitions, modules, and Kobel 2.0 keyword documentation.
- **Go to Definition (`textDocument/definition`)**: Cross-file jump to symbol definition (F12 or Ctrl+Click), including module imports and enum variants.
- **Find All References (`textDocument/references`)**: Dependency-aware cross-file reference search (Shift+F12).
- **Global Semantic Rename (`textDocument/prepareRename`, `textDocument/rename`)**: Dependency- and scope-safe symbol rename across files (F2).
- **Code Actions (`textDocument/codeAction`)**: Quick fixes for missing `use` imports, unused import removal (`source.organizeImports`), unmutated `var` -> `val` rewrites, and missing trait method stubs (`Ctrl+.`).
- **Signature Help (`textDocument/signatureHelp`)**: Active parameter highlighting inside function/method calls.
- **Document Highlights (`textDocument/documentHighlight`)**: Read and write occurrence highlighting within a file.
- **Document & Workspace Symbols (`textDocument/documentSymbol`, `workspace/symbol`)**: Hierarchical file outline, breadcrumbs, and workspace-wide symbol search (`Ctrl+T`).
- **Smart Formatting (`textDocument/formatting`)**: Formats using Kobel Smart Tabs (tabs for indentation, spaces for alignment) and K&R brace conventions.
- **Autocompletion (`textDocument/completion`)**: Keywords, built-in types, member/method completion (including generic containers like `HashMap`/`List`), and workspace symbols.

---

## 1. Visual Studio Code / VSCodium

The extension in `editors/kobel.tmbundle` connects to `kobel lsp` out of the box with zero external dependencies and auto-detects `build/kobel.exe` in the workspace.

### Installation / Linking
Link the bundle folder to your VS Code extensions folder:
```powershell
cmd /c mklink /J "%USERPROFILE%\.vscode\extensions\kobel-lang.kobel-0.2.0" "C:\Users\Admin\CLionProjects\Kobel\editors\kobel.tmbundle"
```
Or set `kobel.compilerPath` in `.vscode/settings.json`:
```json
{
  "kobel.compilerPath": "C:\\Users\\Admin\\CLionProjects\\Kobel\\build\\kobel.exe",
  "[kobel]": {
    "editor.formatOnSave": true,
    "editor.defaultFormatter": "kobel-lang.kobel"
  }
}
```

---

## 2. JetBrains IDEs (CLion, IntelliJ IDEA, RustRover, Fleet)

### Method A: Native LSP (CLion 2024.1+, IntelliJ IDEA 2024.1+)
1. Open **Settings / Preferences** (`Ctrl+Alt+S`).
2. Navigate to **Languages & Frameworks** -> **Language Servers**.
3. Click **+** (Add) and configure:
   - **Name**: `Kobel Language Server`
   - **File types**: Add pattern `*.kb`
   - **Server executable**: `C:\Users\Admin\CLionProjects\Kobel\build\kobel.exe` (or `kobel`)
   - **Server arguments**: `lsp`
4. Click **OK**.

### Method B: LSP4IntelliJ Plugin (Any JetBrains IDE)
1. Install plugin: **LSP4IntelliJ** or **Language Server Protocol support**.
2. Under Settings -> **Language Servers**:
   - Extension: `kb`
   - Command: `kobel_v1.exe lsp`

---

## 3. Neovim (nvim-lspconfig)

Add to your `init.lua`:
```lua
local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.kobel then
  configs.kobel = {
    default_config = {
      cmd = { 'kobel', 'lsp' },
      filetypes = { 'kobel' },
      root_dir = lspconfig.util.root_pattern('.git', 'src'),
      settings = {},
    },
  }
end

lspconfig.kobel.setup{}
```

---

## 4. Helix Editor

In `~/.config/helix/languages.toml` (or `%APPDATA%\helix\languages.toml` on Windows):
```toml
[[language]]
name = "kobel"
scope = "source.kobel"
injection-regex = "kobel"
file-types = ["kb"]
roots = [".git"]
language-servers = ["kobel-lsp"]

[language-server.kobel-lsp]
command = "kobel"
args = ["lsp"]
```
