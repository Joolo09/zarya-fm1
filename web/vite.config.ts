import { resolve } from 'node:path';
import { defineConfig } from 'vite';

// Two pages: the project site (index.html) and the editor (editor/index.html).
// A relative base lets the same build work on GitHub Pages and from any folder.
export default defineConfig({
  base: './',
  build: { target: 'es2022', assetsInlineLimit: 0, rollupOptions: { input: { site: resolve(__dirname, 'index.html'), editor: resolve(__dirname, 'editor/index.html') } } },
  server: { port: 5173, strictPort: false },
});
