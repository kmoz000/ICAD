const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");
const { installToolchain } = require("./toolchain");

let client;
let toolchain = { compiler: "icad", viewer: "icad-viewer" };
let output;

function configuration() {
  return vscode.workspace.getConfiguration("icad");
}

function localBinary(name) {
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!folder) return undefined;
  if (process.platform === "darwin" && name === "icad-viewer") {
    const appBinary = path.join(folder, "build", "bin", "icad-viewer.app", "Contents", "MacOS", "icad-viewer");
    return fs.existsSync(appBinary) ? appBinary : undefined;
  }
  const suffix = process.platform === "win32" ? ".exe" : "";
  const candidate = path.join(folder, "build", "bin", `${name}${suffix}`);
  return fs.existsSync(candidate) ? candidate : undefined;
}

async function resolveToolchain(context, force = false) {
  const configuredCompiler = configuration().get("executablePath", "");
  const configuredViewer = configuration().get("viewerPath", "");
  if (!force) {
    const compiler = configuredCompiler || localBinary("icad");
    const viewer = configuredViewer || localBinary("icad-viewer");
    if (compiler && viewer) return { compiler, viewer };
  }
  if (!configuration().get("toolchain.autoDownload", true) && !force) {
    return {
      compiler: configuredCompiler || localBinary("icad") || "icad",
      viewer: configuredViewer || localBinary("icad-viewer") || "icad-viewer"
    };
  }
  const manifest = context.extension.packageJSON;
  const releaseBaseUrl = configuration().get(
    "toolchain.releaseBaseUrl", "https://github.com/kmoz000/ICAD/releases/download");
  return vscode.window.withProgress({
    location: vscode.ProgressLocation.Notification,
    title: `Installing ICAD ${manifest.version} compiler and viewer`,
    cancellable: false
  }, () => installToolchain({
    storageRoot: path.join(context.globalStorageUri.fsPath, "toolchain"),
    version: manifest.version,
    releaseBaseUrl,
    force
  }));
}

function activeDesign() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "icad") {
    vscode.window.showErrorMessage("Open an .icad design first.");
    return undefined;
  }
  return editor.document;
}

function run(binary, args) {
  return new Promise((resolve, reject) => {
    const process = spawn(binary, args, { shell: false, windowsHide: true });
    let stdout = "";
    let stderr = "";
    process.stdout.on("data", data => { stdout += data; });
    process.stderr.on("data", data => { stderr += data; });
    process.on("error", reject);
    process.on("close", code => code === 0 ? resolve(stdout) : reject(new Error(stderr || stdout)));
  });
}

async function checkDocument(document, notify = true) {
  if (!document || document.languageId !== "icad") return;
  try {
    const result = await run(toolchain.compiler, ["check", document.uri.fsPath]);
    output.appendLine(`[check] ${document.uri.fsPath}: ${result.trim() || "passed"}`);
    if (notify) vscode.window.showInformationMessage("ICAD check passed.");
  } catch (error) {
    output.appendLine(`[check] ${document.uri.fsPath}: ${error.message.trim()}`);
    if (notify) vscode.window.showErrorMessage(`ICAD check failed: ${error.message.trim()}`);
    else vscode.window.setStatusBarMessage("$(error) ICAD check failed — see Problems", 5000);
  }
}

async function checkDesign() {
  const document = activeDesign();
  if (!document) return;
  await document.save();
  await checkDocument(document, true);
}

async function createDesignFromPrompt() {
  if (!configuration().get("agentic.enabled", true)) {
    vscode.window.showInformationMessage("Enable ICAD › Agentic: Enabled in Settings first.");
    return;
  }
  const prompt = await vscode.window.showInputBox({
    title: "Create an ICAD design",
    prompt: "Describe the parametric part, assembly, mechanism, or bridge",
    placeHolder: "Create a detailed articulated industrial robotic arm with a gripper and animated joints",
    ignoreFocusOut: true
  });
  if (!prompt) return;
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  const defaultUri = folder ? vscode.Uri.file(path.join(folder, "agent_design.icad")) : undefined;
  const destination = await vscode.window.showSaveDialog({
    title: "Save generated ICAD source",
    defaultUri,
    filters: { "ICAD design": ["icad"] }
  });
  if (!destination) return;
  const stem = path.basename(destination.fsPath, ".icad");
  const outputDirectory = path.join(folder || path.dirname(destination.fsPath), "build", "icad", stem);
  try {
    await run(toolchain.compiler,
      ["agent-create", prompt, "--source-out", destination.fsPath, "--output-dir", outputDirectory]);
    const document = await vscode.workspace.openTextDocument(destination);
    await vscode.window.showTextDocument(document);
    vscode.window.showInformationMessage(`ICAD created ${stem} and its complete artifact package.`);
  } catch (error) {
    vscode.window.showErrorMessage(`ICAD agent creation failed: ${error.message.trim()}`);
  }
}

async function buildDesign(showNotice = true) {
  const document = activeDesign();
  if (!document) return undefined;
  await document.save();
  const folder = vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath || path.dirname(document.uri.fsPath);
  const stem = path.basename(document.uri.fsPath, ".icad");
  const outputDirectory = path.join(folder, "build", "icad", stem);
  try {
    await run(toolchain.compiler, ["build", document.uri.fsPath, "--output-dir", outputDirectory]);
    if (showNotice) vscode.window.showInformationMessage(`ICAD built ${stem} into ${outputDirectory}`);
    return outputDirectory;
  } catch (error) {
    vscode.window.showErrorMessage(`ICAD build failed: ${error.message.trim()}`);
    return undefined;
  }
}

async function openViewer() {
  const document = activeDesign();
  if (!document) return;
  await document.save();
  const initialView = configuration().get("viewer.initialView", "isometric");
  const process = spawn(toolchain.viewer, ["--view", initialView, document.uri.fsPath], {
    detached: true, stdio: "ignore", shell: false, windowsHide: true
  });
  process.on("error", async error => {
    output.appendLine(`[viewer] ${error.message}`);
    await buildDesign(false);
    vscode.window.showErrorMessage(`ICAD native viewer failed to start: ${error.message}`);
  });
  process.unref();
}

async function configureWorkspaceMcp(context, openAfter = true) {
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!folder) {
    vscode.window.showErrorMessage("Open a workspace before configuring ICAD MCP.");
    return;
  }
  const vscodeDirectory = path.join(folder, ".vscode");
  const target = path.join(vscodeDirectory, "mcp.json");
  await fs.promises.mkdir(vscodeDirectory, { recursive: true });
  let document = {};
  try { document = JSON.parse(await fs.promises.readFile(target, "utf8")); }
  catch (error) { if (error.code !== "ENOENT") throw new Error(`Cannot parse ${target}: ${error.message}`); }
  if (!document.servers || typeof document.servers !== "object" || Array.isArray(document.servers)) {
    document.servers = {};
  }
  document.servers.icad = {
    type: "stdio",
    command: toolchain.compiler,
    args: ["mcp", "--workspace", "${workspaceFolder}"]
  };
  await fs.promises.writeFile(target, `${JSON.stringify(document, null, 2)}\n`, "utf8");
  await context.globalState.update("mcpManaged", true);
  output.appendLine(`[mcp] Configured ${target}`);
  if (openAfter) {
    const editor = await vscode.workspace.openTextDocument(target);
    await vscode.window.showTextDocument(editor);
    vscode.window.showInformationMessage("ICAD MCP added. Review and trust it before starting.");
  }
}

async function removeManagedWorkspaceMcp(context) {
  if (!context.globalState.get("mcpManaged", false)) return;
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!folder) return;
  const target = path.join(folder, ".vscode", "mcp.json");
  let document;
  try { document = JSON.parse(await fs.promises.readFile(target, "utf8")); }
  catch (error) {
    if (error.code === "ENOENT") {
      await context.globalState.update("mcpManaged", false);
      return;
    }
    throw new Error(`Cannot parse ${target}: ${error.message}`);
  }
  if (document.servers && typeof document.servers === "object" && !Array.isArray(document.servers)) {
    delete document.servers.icad;
    await fs.promises.writeFile(target, `${JSON.stringify(document, null, 2)}\n`, "utf8");
  }
  await context.globalState.update("mcpManaged", false);
  output.appendLine(`[mcp] Removed managed ICAD server from ${target}`);
}

async function installCodexPlugin(context, notify = true) {
  const marketplace = path.join(context.extensionPath, "codex-marketplace");
  try {
    const marketplaces = await run("codex", ["plugin", "marketplace", "list"]);
    if (!marketplaces.split(/\r?\n/).some(line => /^icad\s/.test(line))) {
      await run("codex", ["plugin", "marketplace", "add", marketplace, "--json"]);
    }
    const plugins = await run("codex", ["plugin", "list"]);
    if (!plugins.includes("icad-agentic-cad@icad")) {
      await run("codex", ["plugin", "add", "icad-agentic-cad@icad", "--json"]);
    }
    try {
      await run("codex", ["mcp", "get", "icad"]);
    } catch {
      const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath || process.cwd();
      await run("codex", ["mcp", "add", "icad", "--", toolchain.compiler,
        "mcp", "--workspace", folder]);
    }
    if (notify) vscode.window.showInformationMessage(
      "ICAD Codex plugin installed. Start a new Codex conversation to use it.");
  } catch (error) {
    output.appendLine(`[codex] ${error.message.trim()}`);
    if (notify) vscode.window.showErrorMessage(
      `Could not install the ICAD Codex plugin: ${error.message.trim()}`);
  }
}

async function activate(context) {
  output = vscode.window.createOutputChannel("ICAD");
  context.subscriptions.push(output);
  context.subscriptions.push(vscode.commands.registerCommand("icad.check", checkDesign));
  context.subscriptions.push(vscode.commands.registerCommand("icad.agentCreate", createDesignFromPrompt));
  context.subscriptions.push(vscode.commands.registerCommand("icad.build", () => buildDesign(true)));
  context.subscriptions.push(vscode.commands.registerCommand("icad.openViewer", openViewer));
  context.subscriptions.push(vscode.commands.registerCommand("icad.openSettings", () =>
    vscode.commands.executeCommand("workbench.action.openSettings", "@ext:icad.icad-agentic-cad")));
  context.subscriptions.push(vscode.commands.registerCommand("icad.configureMcp", () =>
    configureWorkspaceMcp(context, true)));
  context.subscriptions.push(vscode.commands.registerCommand("icad.installCodexPlugin", () =>
    installCodexPlugin(context, true)));
  context.subscriptions.push(vscode.commands.registerCommand("icad.installToolchain", async () => {
    try {
      toolchain = await resolveToolchain(context, true);
      vscode.window.showInformationMessage(`ICAD ${context.extension.packageJSON.version} toolchain installed.`);
    } catch (error) {
      vscode.window.showErrorMessage(`ICAD toolchain installation failed: ${error.message}`);
    }
  }));

  try {
    toolchain = await resolveToolchain(context, false);
    output.appendLine(`[toolchain] compiler=${toolchain.compiler}`);
    output.appendLine(`[toolchain] viewer=${toolchain.viewer}`);
  } catch (error) {
    output.appendLine(`[toolchain] ${error.message}`);
    vscode.window.showWarningMessage(
      `ICAD automatic toolchain installation failed. Configure ICAD executable paths or retry from Settings. ${error.message}`);
  }

  if (configuration().get("lsp.enabled", true)) {
    const serverOptions = { command: toolchain.compiler, args: ["lsp"], transport: TransportKind.stdio };
    const clientOptions = {
      documentSelector: [{ scheme: "file", language: "icad" }],
      synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher("**/*.icad") }
    };
    client = new LanguageClient("icadLanguageServer", "ICAD Language Server", serverOptions, clientOptions);
    context.subscriptions.push(client.start());
  }

  context.subscriptions.push(vscode.workspace.onWillSaveTextDocument(event => {
    if (event.document.languageId !== "icad" || !configuration().get("formatOnSave", true) || !client) return;
    event.waitUntil(vscode.commands.executeCommand(
      "vscode.executeFormatDocumentProvider", event.document.uri).then(edits => edits || []));
  }));
  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => {
    if (document.languageId === "icad" && configuration().get("checkOnSave", true)) {
      void checkDocument(document, false);
    }
  }));
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(async event => {
    if (event.affectsConfiguration("icad.lsp.enabled")) {
      vscode.window.showInformationMessage("Reload VS Code to apply the ICAD LSP setting.", "Reload")
        .then(choice => { if (choice === "Reload") vscode.commands.executeCommand("workbench.action.reloadWindow"); });
    }
    if (event.affectsConfiguration("icad.mcp.enabled")) {
      try {
        if (configuration().get("mcp.enabled", false)) await configureWorkspaceMcp(context, false);
        else await removeManagedWorkspaceMcp(context);
      } catch (error) {
        output.appendLine(`[mcp] ${error.message}`);
      }
    }
    if (event.affectsConfiguration("icad.codexPlugin.installOnEnable") &&
        configuration().get("codexPlugin.installOnEnable", false)) {
      await installCodexPlugin(context, false);
    }
  }));

  if (configuration().get("mcp.enabled", false)) {
    try { await configureWorkspaceMcp(context, false); }
    catch (error) { output.appendLine(`[mcp] ${error.message}`); }
  }
  if (configuration().get("codexPlugin.installOnEnable", false)) {
    await installCodexPlugin(context, false);
  }
}

async function deactivate() {
  if (client) await client.stop();
}

module.exports = { activate, deactivate };
