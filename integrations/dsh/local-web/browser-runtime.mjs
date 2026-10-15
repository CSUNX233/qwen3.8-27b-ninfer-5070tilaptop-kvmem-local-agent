import { mkdir } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));
const output = resolve(root, '../output/playwright');
const limit = 12000;
const idleMs = 180000;
const isNavigationContextError = error => /Execution context was destroyed|Cannot find context with specified id/i.test(String(error));

export class BrowserRuntime {
  context = null;
  page = null;
  timer = null;
  generation = 0;
  errors = [];
  gpu = null;

  touch() {
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.close().catch(console.error), idleMs);
    this.timer.unref();
  }

  async ensure() {
    clearTimeout(this.timer);
    if (!this.context) {
      const { chromium } = await import('playwright-core');
      const downloads = resolve(root, '../downloads/browser');
      await mkdir(downloads, { recursive: true });
      this.context = await chromium.launchPersistentContext(resolve(root, 'edge-profile'), {
        channel: 'msedge', headless: true,
        downloadsPath: downloads,
        viewport: { width: 1280, height: 900 },
        args: ['--use-angle=d3d11', '--no-first-run', '--disable-background-networking'],
        timeout: 30000,
      });
      this.context.setDefaultTimeout(10000);
      this.context.setDefaultNavigationTimeout(20000);
      const page = this.context.pages()[0] ?? await this.context.newPage();
      this.attach(page);
      this.context.on('page', next => this.attach(next));
      this.context.on('close', () => { this.context = null; this.page = null; this.gpu = null; });
    }
    if (!this.page || this.page.isClosed()) this.attach(await this.context.newPage());
  }

  attach(page) {
    this.page = page;
    page.on('pageerror', error => { this.errors.push(String(error).slice(0, 1000)); this.errors = this.errors.slice(-10); });
    page.on('console', message => {
      if (message.type() === 'error') { this.errors.push(message.text().slice(0, 1000)); this.errors = this.errors.slice(-10); }
    });
    page.on('framenavigated', frame => { if (frame === page.mainFrame()) this.generation++; });
  }

  requirePage() {
    clearTimeout(this.timer);
    if (!this.page || this.page.isClosed()) throw new Error('Browser is closed or idle. Call browser_open with the URL first.');
  }

  async readPage(read) {
    // Only repeat our own DOM/GPU reads. Model-authored scripts can have side
    // effects and must never be replayed after losing their execution context.
    for (let attempt = 0; attempt < 3; attempt++) {
      this.requirePage();
      const page = this.page;
      try {
        await page.waitForLoadState('domcontentloaded', { timeout: 5000 });
        return await read(page);
      } catch (error) {
        if (!isNavigationContextError(error)) throw error;
        if (attempt === 2) throw new Error('The page kept navigating while being read. Call browser_snapshot after the navigation settles.');
      }
    }
  }

  async open(url) {
    const target = new URL(url);
    if (!['http:', 'https:', 'file:'].includes(target.protocol)) throw new Error('Use an HTTP(S) or local file URL.');
    if (target.username || target.password) throw new Error('Use a URL without embedded credentials.');
    await this.ensure();
    this.errors = [];
    const response = await this.page.goto(target.href, { waitUntil: 'domcontentloaded' });
    // Keep one foreground page; popups replace it when an interaction opens one.
    this.gpu = await this.renderer();
    return { status: response?.status() ?? null, renderer: this.gpu, ...await this.snapshot() };
  }

  async renderer() {
    return this.readPage(page => page.evaluate(() => {
      const canvas = document.createElement('canvas');
      const gl = canvas.getContext('webgl', { powerPreference: 'low-power' });
      if (!gl) return 'WebGL unavailable';
      const extension = gl.getExtension('WEBGL_debug_renderer_info');
      const renderer = extension ? gl.getParameter(extension.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
      gl.getExtension('WEBGL_lose_context')?.loseContext();
      return String(renderer);
    }));
  }

  async snapshot(offset = 0, maxChars = limit) {
    this.requirePage();
    if (!Number.isInteger(offset) || offset < 0) throw new Error('offset must be a nonnegative integer.');
    if (!Number.isInteger(maxChars) || maxChars < 1 || maxChars > 20000) throw new Error('maxChars must be between 1 and 20000.');
    const prefix = 'e' + (++this.generation) + '-';
    const info = await this.readPage(page => page.evaluate(({ prefix, offset, maxChars }) => {
      document.querySelectorAll('[data-dsh-browser-ref]').forEach(element => element.removeAttribute('data-dsh-browser-ref'));
      const interactive = [...document.querySelectorAll('a[href],button,input:not([type="hidden"]),textarea,select,[role="button"],[role="link"],[role="textbox"],[contenteditable="true"],summary')]
        .filter(element => element.getClientRects().length > 0 && getComputedStyle(element).visibility !== 'hidden').slice(0, 90);
      const elements = interactive.map((element, index) => {
        const ref = prefix + index;
        element.setAttribute('data-dsh-browser-ref', ref);
        const type = element.getAttribute('role') || element.tagName.toLowerCase();
        const label = element.getAttribute('aria-label') || element.labels?.[0]?.innerText || element.getAttribute('placeholder') || element.innerText || element.getAttribute('title') || '';
        const value = element.type !== 'password' && 'value' in element ? String(element.value).slice(0, 180) : undefined;
        return { ref, type, label: label.replace(/\s+/g, ' ').trim().slice(0, 160), ...(value !== undefined ? { value } : {}),
          ...(element.tagName === 'A' ? { href: element.href } : {}) };
      });
      const text = document.body?.innerText ?? '';
      return { title: document.title, url: location.href, text: text.slice(offset, offset + maxChars),
        offset, totalChars: text.length, truncated: offset + maxChars < text.length, elements };
    }, { prefix, offset, maxChars }));
    return { ...info, errors: this.errors.slice(-5) };
  }

  async action({ action, ref, text }) {
    this.requirePage();
    const originalPage = this.page;
    let popup;
    if (action === 'back') {
      await this.page.goBack({ waitUntil: 'domcontentloaded' });
    } else if (action === 'reload') {
      await this.page.reload({ waitUntil: 'domcontentloaded' });
    } else if (action === 'scroll') {
      const pixels = Number(text ?? 600);
      if (!Number.isFinite(pixels) || Math.abs(pixels) > 10000) throw new Error('scroll text must be a pixel count from -10000 to 10000.');
      await this.page.evaluate(pixels => window.scrollBy(0, pixels), pixels);
    } else {
      if (!/^e\d+-\d+$/.test(ref ?? '')) throw new Error('Use an element ref from the most recent browser snapshot.');
      const element = this.page.locator('[data-dsh-browser-ref="' + ref + '"]');
      if (await element.count() !== 1) throw new Error('That ref is stale. Call browser_snapshot and use a current ref.');
      if (action === 'click' || action === 'press') popup = originalPage.waitForEvent('popup', { timeout: 750 }).catch(() => null);
      if (action === 'click') await element.click();
      else if (action === 'fill') await element.fill(String(text ?? ''));
      else if (action === 'press') await element.press(String(text ?? 'Enter'));
      else if (action === 'select') await element.selectOption(String(text ?? ''));
      else throw new Error('Unknown browser action.');
    }
    const opened = popup ? await popup : null;
    if (opened) this.page = opened;
    if (this.context.pages().length > 1) {
      this.page = this.context.pages().at(-1);
      if (this.page !== originalPage && this.page.url() === 'about:blank') {
        await this.page.waitForURL(url => url.href !== 'about:blank', { waitUntil: 'domcontentloaded', timeout: 5000 }).catch(() => {});
      }
      for (const page of this.context.pages()) if (page !== this.page) await page.close();
    }
    await this.page.waitForLoadState('domcontentloaded', { timeout: 5000 }).catch(() => {});
    return this.snapshot();
  }

  async evaluate(expression) {
    this.requirePage();
    if (typeof expression !== 'string' || !expression.trim()) throw new Error('A JavaScript expression is required.');
    await this.page.waitForLoadState('domcontentloaded', { timeout: 5000 });
    let timeout;
    let result;
    try {
      result = await Promise.race([
        this.page.evaluate(expression),
        new Promise((_, reject) => {
          timeout = setTimeout(() => reject(new Error('Browser JavaScript exceeded 10 seconds; the browser has been closed. Call browser_open to recover.')), 10000);
        }),
      ]);
    } catch (error) {
      if (String(error).includes('exceeded 10 seconds')) await this.close();
      if (isNavigationContextError(error)) {
        throw new Error('The page navigated or reloaded while this JavaScript was running, so its result was lost. The script may already have acted; it was not retried. Call browser_snapshot, then run a separate browser_evaluate on the new page. To refresh, use browser_action(action="reload"); do not combine location.reload() or a URL change with a timer or Promise that reads the old page.');
      }
      throw error;
    } finally {
      clearTimeout(timeout);
    }
    const encoded = JSON.stringify(result ?? null);
    return { result: encoded.length <= 16000 ? result ?? null : encoded.slice(0, 16000), truncated: encoded.length > 16000 };
  }

  async screenshot() {
    this.requirePage();
    await mkdir(output, { recursive: true });
    const path = resolve(output, 'browser-' + Date.now() + '.png');
    await this.page.screenshot({ path, fullPage: false });
    return { path, url: this.page.url(), note: 'Screenshot saved locally. Use DOM/text tools for this text-only model.' };
  }

  status() {
    return { running: !!this.context, url: this.page && !this.page.isClosed() ? this.page.url() : null,
      renderer: this.gpu, idleCloseSeconds: idleMs / 1000, errors: this.errors.slice(-5) };
  }

  async close() {
    clearTimeout(this.timer);
    const context = this.context;
    this.context = null; this.page = null; this.gpu = null;
    if (context) await context.close();
    return this.status();
  }
}
