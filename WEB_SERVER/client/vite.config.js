import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      // HTTP API
      '/login': {
        target: 'http://localhost:5000',
        changeOrigin: true
      },
      // Socket.IO (WebSocket + polling)
      '/socket.io': {
        target: 'http://localhost:5000',
        ws: true,
        changeOrigin: true
      }
    }
  }
});