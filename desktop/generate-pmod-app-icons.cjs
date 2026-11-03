const { chromium } = require("F:/Pmod/qwen3.8-27b-ninfer-5070tilaptop-kvmem-local-agent/integrations/dsh/local-web/node_modules/playwright-core");
const path = require("path");
const fs = require("fs");
const { execSync } = require("child_process");

(async () => {
  console.log("1. Launching headless browser to render 1024x1024 icon...");
  const browser = await chromium.launch({
    headless: true,
    executablePath: "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe"
  });
  const page = await browser.newPage({ viewport: { width: 1024, height: 1024 }, deviceScaleFactor: 1 });
  const htmlPath = "file:///" + path.resolve(__dirname, "render-app-icon.html").replace(/\\/g, "/");
  await page.goto(htmlPath);
  await page.waitForTimeout(600);

  const pngOut = path.resolve(__dirname, "app-icon.png");
  await page.screenshot({ path: pngOut, omitBackground: true });
  console.log("2. Rendered 1024x1024 app-icon.png successfully!");
  await browser.close();

  // 3. 运行 tauri icon 自动生成全套多分辨率图标
  console.log("3. Running Tauri icon generator for Windows & cross-platform...");
  execSync("npx tauri icon app-icon.png", {
    cwd: path.resolve(__dirname),
    stdio: "inherit",
    shell: true
  });

  // 4. 将生成的 icon.ico 同步到打包暂存目录（如果存在）
  const generatedIco = path.resolve(__dirname, "src-tauri/icons/icon.ico");
  const stagingIco = path.resolve(__dirname, "../dist/package_staging/icon.ico");
  if (fs.existsSync(path.dirname(stagingIco))) {
    fs.copyFileSync(generatedIco, stagingIco);
    console.log(`Copied icon.ico to ${stagingIco}`);
  }

  // 5. 复制一份到 desktop/public 作为 Web favicon
  const publicDir = path.resolve(__dirname, "public");
  fs.copyFileSync(path.resolve(__dirname, "src-tauri/icons/icon.png"), path.join(publicDir, "favicon.png"));
  fs.copyFileSync(path.resolve(__dirname, "src-tauri/icons/icon.png"), path.join(publicDir, "tauri.svg"));
  fs.copyFileSync(path.resolve(__dirname, "src-tauri/icons/icon.png"), path.join(publicDir, "vite.svg"));

  console.log("SUCCESS! All Pmod icons generated & synchronized!");
})();

