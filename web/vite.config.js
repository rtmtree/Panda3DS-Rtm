import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],

  server: {
    // Required for SharedArrayBuffer (used if WASM threads are enabled)
    headers: {
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Opener-Policy': 'same-origin',
    },
  },

  // Vite must not try to process panda3ds.js — it lives in public/ as-is
  optimizeDeps: {
    exclude: [],
  },
});
