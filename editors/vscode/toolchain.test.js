const assert = require("assert");
const { parseChecksum, platformAsset } = require("./toolchain");

assert.strictEqual(platformAsset("darwin", "arm64"), "icad-macos-arm64");
assert.strictEqual(platformAsset("darwin", "x64"), "icad-macos-x86_64");
assert.strictEqual(platformAsset("linux", "x64"), "icad-linux-x86_64");
assert.strictEqual(platformAsset("win32", "x64"), "icad-windows-x86_64");
assert.throws(() => platformAsset("linux", "arm64"), /no released toolchain/);
const checksum = "a".repeat(64);
assert.strictEqual(parseChecksum(`${checksum}  icad.zip\n`), checksum);
assert.throws(() => parseChecksum("not-a-checksum"), /malformed/);
