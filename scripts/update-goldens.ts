import { chromium } from "playwright";
import { readdir } from "fs/promises";
import { join, basename, extname } from "path";
import { createServer, type IncomingMessage, type ServerResponse } from "http";
import { readFileSync, existsSync } from "fs";

const ROOT = join(import.meta.dir, "..");
const TEST_DATA = join(ROOT, "test_data");
const CASES_DIR = join(TEST_DATA, "cases");
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

async function main() {
  const filterName = process.argv[2];

  const files = (await readdir(CASES_DIR))
    .filter((f) => f.endsWith(".md"))
    .filter((f) => !filterName || f.includes(filterName))
    .sort();

  if (files.length === 0) {
    console.error("No matching cases found");
    process.exit(1);
  }

  console.log(`=== Updating ${files.length} golden PNG(s) ===\n`);

  const server = await startServer();
  const browser = await chromium.launch();
  const context = await browser.newContext({
    viewport: { width: 800, height: 600 },
    deviceScaleFactor: 1,
  });

  for (const file of files) {
    const name = basename(file, ".md");
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
  }

  await browser.close();
  server.close();

  console.log(`\nDone. Review the updated goldens, then commit:`);
  console.log(`  git add test_data/cases/*_chrome.png`);
  console.log(`  git commit -m "test: update golden Chrome PNGs"`);
}

main();
