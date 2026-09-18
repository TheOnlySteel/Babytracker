import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "tests/e2e",
  fullyParallel: true,
  retries: process.env.CI ? 1 : 0,
  use: {
    baseURL: "http://127.0.0.1:4173",
    trace: "retain-on-failure",
    ...devices["iPhone 13"],
    defaultBrowserType: "chromium",
    launchOptions: process.env.PLAYWRIGHT_CHROME
      ? { executablePath: process.env.PLAYWRIGHT_CHROME }
      : {},
  },
  webServer: {
    command:
      "node node_modules/vite/bin/vite.js dev --host 127.0.0.1 --port 4173",
    url: "http://127.0.0.1:4173",
    reuseExistingServer: !process.env.CI,
    env: {
      PUBLIC_SUPABASE_URL: "https://fixture.supabase.co",
      PUBLIC_SUPABASE_ANON_KEY: "fixture-public-key",
    },
  },
});
