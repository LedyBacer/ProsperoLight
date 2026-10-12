# ps5-native-app-boilerplate - worker-owned headed Chromium, isolated context.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later


class ManagedBrowser:
    def __init__(self, channel=None):
        self.playwright = None
        self.browser = None
        self.page = None
        self.channel = channel
        self.version = None
        self.errors = []

    async def open(self, url, display):
        from playwright.async_api import async_playwright
        self.playwright = await async_playwright().start()
        self.browser = await self.playwright.chromium.launch(headless=False, channel=self.channel, args=[
            f"--window-position={display['x']},{display['y']}",
            f"--window-size={display['width']},{display['height']}",
            '--no-first-run'])
        self.version = self.browser.version
        self.page = await self.browser.new_page(no_viewport=True)
        self.page.on('pageerror', lambda error: self.errors.append(str(error)[:512]) if len(self.errors) < 20 else None)
        await self.page.goto(url, wait_until='domcontentloaded', timeout=15000)
        await self.page.bring_to_front()

    async def ready(self):
        await self.page.locator('#ready').click(timeout=30000)

    async def diagnostics(self):
        try:
            observed = await self.page.evaluate("() => ({...snapshot(),socket_state:socket.readyState,prepared:!!simple,ready_disabled:button.disabled,status:status.textContent,pending_ready:pendingReady,preflight_count:preflightFrames.length})")
        except Exception as exc:
            observed = {'reason': type(exc).__name__}
        return {'observed': observed, 'page_errors': self.errors, 'version': self.version}

    async def close(self):
        try:
            if self.browser:
                await self.browser.close()
        finally:
            if self.playwright:
                await self.playwright.stop()
            self.browser = self.playwright = self.page = None
