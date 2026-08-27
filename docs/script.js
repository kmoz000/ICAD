const commands = {
  unix: "curl -fsSL https://github.com/valorisystems/ICAD/releases/latest/download/install.sh | sh",
  windows: [
    '$installer = Join-Path $env:TEMP "icad-install.ps1"',
    "Invoke-WebRequest https://github.com/valorisystems/ICAD/releases/latest/download/install.ps1 -OutFile $installer",
    "& $installer",
  ].join("\n"),
};

const command = document.querySelector("#install-command");
const tabs = [...document.querySelectorAll(".terminal-tab")];
const copyButton = document.querySelector(".copy-button");

for (const tab of tabs) {
  tab.addEventListener("click", () => {
    for (const candidate of tabs) {
      const selected = candidate === tab;
      candidate.classList.toggle("active", selected);
      candidate.setAttribute("aria-selected", String(selected));
    }
    command.textContent = commands[tab.dataset.platform];
    copyButton.textContent = "Copy";
  });
}

copyButton.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(command.textContent);
  } catch {
    const range = document.createRange();
    range.selectNodeContents(command);
    const selection = window.getSelection();
    selection.removeAllRanges();
    selection.addRange(range);
    document.execCommand("copy");
    selection.removeAllRanges();
  }
  copyButton.textContent = "Copied";
  window.setTimeout(() => { copyButton.textContent = "Copy"; }, 1800);
});

document.querySelector("#year").textContent = new Date().getFullYear();

if ("IntersectionObserver" in window) {
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) {
        entry.target.classList.add("visible");
        observer.unobserve(entry.target);
      }
    }
  }, { threshold: 0.12 });
  document.querySelectorAll(".reveal").forEach((element) => observer.observe(element));
} else {
  document.querySelectorAll(".reveal").forEach((element) => element.classList.add("visible"));
}
