import { sveltekit } from '@sveltejs/kit/vite';
import { SvelteKitPWA } from '@vite-pwa/sveltekit';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  plugins: [
    sveltekit(),
    SvelteKitPWA({
      registerType: 'autoUpdate',
      strategies: 'generateSW',
      injectRegister: false,
      // SPA on adapter-static: precache the fallback page (revision from version.json) so the app shell starts offline.
      kit: { adapterFallback: 'index.html', spa: true, includeVersionFile: true },
      manifest: {
        name: 'Rosalie',
        short_name: 'Rosalie',
        description: 'Feed, diaper and pump log',
        start_url: '/',
        scope: '/',
        display: 'standalone',
        orientation: 'portrait',
        background_color: '#1b2030',
        theme_color: '#1b2030',
        icons: [
          { src: '/icons/icon-192.png', sizes: '192x192', type: 'image/png' },
          { src: '/icons/icon-512.png', sizes: '512x512', type: 'image/png' },
          { src: '/icons/icon-512.png', sizes: '512x512', type: 'image/png', purpose: 'maskable' }
        ]
      },
      workbox: {
        globPatterns: ['**/*.{js,css,html,png,svg,webmanifest}'],
        navigateFallback: '/index.html',
        // Never cache Supabase traffic; the app is only useful live.
        navigateFallbackDenylist: [/^\/rest\//, /^\/auth\//],
        // _app/env.js is written by the adapter after the worker is generated; its content is fixed per build.
        additionalManifestEntries: [{ url: '_app/env.js', revision: String(Date.now()) }],
        runtimeCaching: [
          {
            urlPattern: /^https:\/\/fonts\.(googleapis|gstatic)\.com\//,
            handler: 'StaleWhileRevalidate',
            options: { cacheName: 'fonts', expiration: { maxEntries: 20, maxAgeSeconds: 60 * 60 * 24 * 365 } }
          }
        ]
      },
      devOptions: { enabled: false }
    })
  ],
  test: {
    include: ['src/**/*.test.ts', 'scripts/**/*.test.mjs'],
    environment: 'node'
  }
});
