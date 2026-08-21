const vscode = require("vscode");
const { LanguageClient, TransportKind } = require("vscode-languageclient/node");
const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");

let client;

function configuredBinary(setting, fallbackName) {
  const configured = vscode.workspace.getConfiguration("icad").get(setting, "");
  if (configured) return configured;
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (folder) {
    const candidate = path.join(folder, "build", "bin", fallbackName);
    if (fs.existsSync(candidate)) return candidate;
  }
  return fallbackName;
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
    const process = spawn(binary, args, { shell: false });
    let stdout = "";
    let stderr = "";
    process.stdout.on("data", data => { stdout += data; });
    process.stderr.on("data", data => { stderr += data; });
    process.on("error", reject);
    process.on("close", code => code === 0 ? resolve(stdout) : reject(new Error(stderr || stdout)));
  });
}

async function checkDesign() {
  const document = activeDesign();
  if (!document) return;
  await document.save();
  try {
    const output = await run(configuredBinary("executablePath", "icad"), ["check", document.uri.fsPath]);
    vscode.window.showInformationMessage(output.trim() || "ICAD check passed.");
  } catch (error) {
    vscode.window.showErrorMessage(`ICAD check failed: ${error.message.trim()}`);
  }
}

async function createDesignFromPrompt() {
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
    await run(configuredBinary("executablePath", "icad"),
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
    await run(configuredBinary("executablePath", "icad"),
      ["build", document.uri.fsPath, "--output-dir", outputDirectory]);
    if (showNotice) vscode.window.showInformationMessage(`ICAD built ${stem} into ${outputDirectory}`);
    return path.join(outputDirectory, `${stem}.html`);
  } catch (error) {
    vscode.window.showErrorMessage(`ICAD build failed: ${error.message.trim()}`);
    return undefined;
  }
}

async function openViewer() {
  const html = await buildDesign(false);
  if (!html) return;
  const viewer = configuredBinary("viewerPath", "icad-viewer");
  const process = spawn(viewer, [html], { detached: true, stdio: "ignore", shell: false });
  process.on("error", async () => {
    await vscode.env.openExternal(vscode.Uri.file(html));
  });
  process.unref();
}

async function activate(context) {
  const executable = configuredBinary("executablePath", "icad");
  const serverOptions = { command: executable, args: ["lsp"], transport: TransportKind.stdio };
  const clientOptions = {
    documentSelector: [{ scheme: "file", language: "icad" }],
    synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher("**/*.icad") }
  };
  client = new LanguageClient("icadLanguageServer", "ICAD Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client.start());
  context.subscriptions.push(vscode.commands.registerCommand("icad.check", checkDesign));
  context.subscriptions.push(vscode.commands.registerCommand("icad.agentCreate", createDesignFromPrompt));
  context.subscriptions.push(vscode.commands.registerCommand("icad.build", () => buildDesign(true)));
  context.subscriptions.push(vscode.commands.registerCommand("icad.openViewer", openViewer));
}

async function deactivate() {
  if (client) await client.stop();
}

module.exports = { activate, deactivate };
