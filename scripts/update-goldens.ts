import { chromium } from "playwright";
import { readdir } from "fs/promises";
import { join, basename, extname } from "path";
import { createServer, type IncomingMessage, type ServerResponse } from "http";
import { readFileSync, existsSync, copyFileSync } from "fs";
import { spawnSync } from "child_process";

const ROOT = join(import.meta.dir, "..");
const TEST_DATA = join(ROOT, "test_data");
const CASES_DIR = join(TEST_DATA, "cases");
const GRAMMAR_SAMPLES_DIR = join(TEST_DATA, "grammar_samples");
const COLORIZER_SMOKES_DIR = join(TEST_DATA, "colorizer_smokes");
const TOOL = join(ROOT, "build", "Release", "screenshot_tool.exe");
const LIGHT_SPOT_CHECK = "sample.py"; // the one light-theme spot-check sample
const PORT = 9123;

const MIME: Record<string, string> = {
  ".html": "text/html",
  ".md": "text/markdown; charset=utf-8",
  ".js": "application/javascript",
  ".css": "text/css",
  ".png": "image/png",
};

function startServer(): Promise<ReturnType<typeof createServer>> {
  return new Promise((resolve) => {
    const server = createServer(
      (req: IncomingMessage, res: ServerResponse) => {
        const url = new URL(req.url!, `http://localhost:${PORT}`);
        const filePath = join(
          TEST_DATA,
          decodeURIComponent(url.pathname.slice(1))
        );
        if (!filePath.startsWith(TEST_DATA)) {
          res.writeHead(403);
          res.end("Forbidden");
          return;
        }
        if (!existsSync(filePath)) {
          res.writeHead(404);
          res.end("Not found");
          return;
        }
        const ext = extname(filePath);
        res.writeHead(200, {
          "Content-Type": MIME[ext] || "application/octet-stream",
        });
        res.end(readFileSync(filePath));
      }
    );
    server.listen(PORT, () => resolve(server));
  });
}

function runDumpTokens(sample: string, dark: boolean): void {
  const args = [
    join(GRAMMAR_SAMPLES_DIR, sample),
    "--colorizer",
    "--dump-tokens",
    ...(dark ? ["--dark"] : []),
  ];
  const result = spawnSync(TOOL, args, { stdio: "inherit" });
  if (result.status !== 0) {
    throw new Error(
      `screenshot_tool --dump-tokens failed for ${sample} (theme=${dark ? "dark" : "light"})`
    );
  }
}

function copyTokenGolden(sample: string, theme: "dark" | "light"): void {
  const ours = join(GRAMMAR_SAMPLES_DIR, `${sample}_tokens.${theme}.json`);
  const golden = join(GRAMMAR_SAMPLES_DIR, `${sample}_tokens.${theme}.golden.json`);
  copyFileSync(ours, golden);
  console.log(`  OK    ${sample} (${theme}) -> ${basename(golden)}`);
}

async function main() {
  const filterName = process.argv[2];

  const files = (await readdir(CASES_DIR))
    .filter((f) => f.endsWith(".md"))
    .filter((f) => !filterName || f.includes(filterName))
    .sort();

  // --- Markdown Chrome reference PNGs ---
  if (files.length > 0) {
    console.log(`=== Updating ${files.length} markdown golden PNG(s) ===\n`);
  }

  // Browser + HTTP server are only needed for Chrome-reference cases.
  // Bring them up lazily so a single .flags case (or a Playwright outage)
  // doesn't gate the whole script.
  let server: ReturnType<typeof createServer> | null = null;
  let browser: Awaited<ReturnType<typeof chromium.launch>> | null = null;
  let context: Awaited<ReturnType<NonNullable<typeof browser>["newContext"]>> | null = null;

  for (const file of files) {
    const name = basename(file, ".md");

    // Self-snapshot path: case has a `.flags` sidecar listing extra
    // screenshot_tool args. Skip Playwright; run the tool, copy output
    // to <name>_golden.png.
    const flagsPath = join(CASES_DIR, `${name}.flags`);
    if (existsSync(flagsPath)) {
      const flags = readFileSync(flagsPath, "utf8").trim().split(/\s+/).filter(Boolean);
      const tool = join(ROOT, "build", "Release", "screenshot_tool.exe");
      const mdPath = join(CASES_DIR, file);
      const result = spawnSync(tool, [mdPath, "--full", ...flags], { stdio: "inherit" });
      if (result.error) {
        console.error(`  FAIL  ${name} (${result.error.message})`);
        continue;
      }
      if (result.status !== 0) {
        console.error(`  FAIL  ${name} (screenshot_tool exited ${result.signal ?? result.status})`);
        continue;
      }
      const ourPng = join(CASES_DIR, `${name}.png`);
      const goldenPng = join(CASES_DIR, `${name}_golden.png`);
      copyFileSync(ourPng, goldenPng);
      console.log(`  OK    ${name} -> ${name}_golden.png`);
      continue;
    }

    try {
      if (!context) {
        server = await startServer();
        browser = await chromium.launch();
        context = await browser.newContext({
          viewport: { width: 800, height: 600 },
          deviceScaleFactor: 1,
        });
      }

      const page = await context.newPage();

      await page.goto(
        `http://localhost:${PORT}/preview.html?file=cases/${file}`
      );
      await page.waitForLoadState("networkidle");
      await page.waitForSelector(".markdown-body", { timeout: 5000 });

      const outPath = join(CASES_DIR, `${name}_chrome.png`);
      await page.screenshot({ path: outPath, fullPage: true });
      console.log(`  OK  ${name}`);
      await page.close();
    } catch (e: any) {
      console.error(`  FAIL  ${name} (Playwright: ${e?.message?.split("\n")[0] ?? e})`);
      // If browser launch failed, skip remaining Playwright cases.
      if (!context) break;
    }
  }

  // --- Colorizer token snapshots ---
  if (existsSync(GRAMMAR_SAMPLES_DIR)) {
    console.log(`\n=== Colorizer token snapshots ===`);
    const samples = (await readdir(GRAMMAR_SAMPLES_DIR))
      .filter((f) => f.startsWith("sample."))
      .filter((f) => !f.includes("_tokens."))
      .filter((f) => !f.endsWith(".png"))
      .filter((f) => !filterName || f.includes(filterName))
      .sort();

    for (const sample of samples) {
      // Always regenerate the dark snapshot.
      try {
        runDumpTokens(sample, /*dark=*/ true);
        copyTokenGolden(sample, "dark");
      } catch (e: any) {
        console.error(`  FAIL  ${sample} dark: ${e?.message ?? e}`);
      }

      // Light-theme spot-check: only the configured sample.
      if (sample === LIGHT_SPOT_CHECK) {
        try {
          runDumpTokens(sample, /*dark=*/ false);
          copyTokenGolden(sample, "light");
        } catch (e: any) {
          console.error(`  FAIL  ${sample} light: ${e?.message ?? e}`);
        }
      }
    }
  }

  // --- Colorizer pixel smokes ---
  if (existsSync(COLORIZER_SMOKES_DIR)) {
    console.log(`\n=== Colorizer pixel smokes ===`);
    const smokes = (await readdir(COLORIZER_SMOKES_DIR))
      .filter((f) => !f.endsWith(".png"))
      .filter((f) => !f.endsWith(".flags"))
      .filter((f) => !filterName || f.includes(filterName))
      .sort();

    for (const smoke of smokes) {
      const flagsPath = join(COLORIZER_SMOKES_DIR, `${smoke}.flags`);
      const flags = existsSync(flagsPath)
        ? readFileSync(flagsPath, "utf8").trim().split(/\s+/).filter(Boolean)
        : ["--full", "--dark"];
      const src = join(COLORIZER_SMOKES_DIR, smoke);
      const result = spawnSync(TOOL, [src, ...flags], { stdio: "inherit" });
      if (result.status !== 0) {
        console.error(`  FAIL  ${smoke}`);
        continue;
      }
      // Tool wrote <smoke>_dark.png in --dark mode (which all smokes use).
      // Copy to <smoke>_golden.png.
      const ours = join(COLORIZER_SMOKES_DIR, `${smoke}_dark.png`);
      const golden = join(COLORIZER_SMOKES_DIR, `${smoke}_golden.png`);
      if (existsSync(ours)) {
        copyFileSync(ours, golden);
        console.log(`  OK    ${smoke} -> ${basename(golden)}`);
      } else {
        console.error(`  FAIL  ${smoke} (no PNG produced; expected ${basename(ours)})`);
      }
    }
  }

  if (browser) await browser.close();
  if (server) server.close();

  console.log(`\nDone. Review the updated goldens, then commit:`);
  console.log(`  git add test_data/cases/*_chrome.png \\`);
  console.log(`          test_data/grammar_samples/*_tokens.*.golden.json \\`);
  console.log(`          test_data/colorizer_smokes/*_golden.png`);
}

main();
