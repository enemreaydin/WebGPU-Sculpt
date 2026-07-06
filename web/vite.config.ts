import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The compiled WASM engine (sdf-engine.js/.wasm/.data) is emitted by the CMake
// build into web/public/engine and served as static files at /engine/*.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
  },
});
