const crypto = require("crypto");
const fs = require("fs");
const https = require("https");
const path = require("path");
const { spawn } = require("child_process");

function platformAsset(platform = process.platform, architecture = process.arch) {
  const key = `${platform}-${architecture}`;
  const assets = {
    "darwin-arm64": "icad-macos-arm64",
    "darwin-x64": "icad-macos-x86_64",
    "linux-x64": "icad-linux-x86_64",
    "win32-x64": "icad-windows-x86_64"
  };
  if (!assets[key]) throw new Error(`ICAD has no released toolchain for ${key}`);
  return assets[key];
}

function parseChecksum(source) {
  const match = String(source).trim().match(/^([a-fA-F0-9]{64})(?:\s|$)/);
  if (!match) throw new Error("release checksum is malformed");
  return match[1].toLowerCase();
}

function download(url, destination, redirects = 5) {
  return new Promise((resolve, reject) => {
    const request = https.get(url, { headers: { "User-Agent": "icad-vscode" } }, response => {
      if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
        response.resume();
        if (redirects === 0) return reject(new Error("too many release download redirects"));
        return download(new URL(response.headers.location, url).toString(), destination, redirects - 1)
          .then(resolve, reject);
      }
      if (response.statusCode !== 200) {
        response.resume();
        return reject(new Error(`release download failed with HTTP ${response.statusCode}`));
      }
      const output = fs.createWriteStream(destination, { flags: "wx" });
      output.on("error", reject);
      response.on("error", reject);
      output.on("finish", () => output.close(resolve));
      response.pipe(output);
    });
    request.on("error", reject);
  });
}

async function sha256(file) {
  const hash = crypto.createHash("sha256");
  await new Promise((resolve, reject) => {
    const input = fs.createReadStream(file);
    input.on("data", chunk => hash.update(chunk));
    input.on("error", reject);
    input.on("end", resolve);
  });
  return hash.digest("hex");
}

function execute(command, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { shell: false, windowsHide: true });
    let error = "";
    child.stderr.on("data", data => { error += data; });
    child.on("error", reject);
    child.on("close", code => code === 0 ? resolve() : reject(new Error(error || `${command} failed`)));
  });
}

function installedPaths(root) {
  const suffix = process.platform === "win32" ? ".exe" : "";
  return {
    compiler: path.join(root, "stage", "bin", `icad${suffix}`),
    viewer: path.join(root, "stage", "bin", `icad-viewer${suffix}`)
  };
}

async function installToolchain({ storageRoot, version, releaseBaseUrl, force = false }) {
  const asset = platformAsset();
  const installRoot = path.join(storageRoot, `v${version}`, asset);
  const installed = installedPaths(installRoot);
  if (!force && fs.existsSync(installed.compiler) && fs.existsSync(installed.viewer)) return installed;

  await fs.promises.mkdir(path.dirname(installRoot), { recursive: true });
  const staging = `${installRoot}.installing-${process.pid}-${Date.now()}`;
  const archive = path.join(staging, `${asset}.zip`);
  const checksumFile = `${archive}.sha256`;
  await fs.promises.rm(staging, { recursive: true, force: true });
  await fs.promises.mkdir(staging, { recursive: true });
  try {
    const base = releaseBaseUrl.replace(/\/$/, "");
    await download(`${base}/v${version}/${asset}.zip.sha256`, checksumFile);
    await download(`${base}/v${version}/${asset}.zip`, archive);
    const expected = parseChecksum(await fs.promises.readFile(checksumFile, "utf8"));
    const actual = await sha256(archive);
    if (actual !== expected) throw new Error(`ICAD toolchain checksum mismatch for ${asset}`);
    await execute("tar", ["-xf", archive, "-C", staging]);
    const staged = installedPaths(staging);
    if (!fs.existsSync(staged.compiler) || !fs.existsSync(staged.viewer)) {
      throw new Error("release archive does not contain icad and icad-viewer");
    }
    if (process.platform !== "win32") {
      await fs.promises.chmod(staged.compiler, 0o755);
      await fs.promises.chmod(staged.viewer, 0o755);
    }
    await fs.promises.rm(archive, { force: true });
    await fs.promises.rm(checksumFile, { force: true });
    await fs.promises.rm(installRoot, { recursive: true, force: true });
    await fs.promises.rename(staging, installRoot);
    return installedPaths(installRoot);
  } catch (error) {
    await fs.promises.rm(staging, { recursive: true, force: true });
    throw error;
  }
}

module.exports = { installToolchain, installedPaths, parseChecksum, platformAsset, sha256 };
