# VNR VJIET Lost & Found Portal

A redesigned GitHub Pages interface for the existing Lost & Found project. The browser calls the C `lf_similarity` function compiled to WebAssembly using Emscripten.

## Files
- `index.html` — responsive dashboard and existing report/browse/matches/admin interface
- `lf_match.js`, `lf_match.wasm` — existing Emscripten build of `web_bridge.c`
- `web_bridge.c` — C text similarity function exposed to the browser
- `lost_and_found.c` — original terminal-based C management system
- `lost_and_found_backup.c` — untouched backup copy

## Run locally
GitHub Pages serves the files over HTTP. For local testing, open Terminal in this folder and run:

```bash
python3 -m http.server 8000
```

Then open `http://localhost:8000`. Do not open `index.html` directly with `file://`, because the browser may block loading the WebAssembly file.

## Publish with GitHub Pages
1. Create or open your GitHub repository.
2. Upload all files in this folder to the repository root.
3. In **Settings → Pages**, choose **Deploy from a branch**, select `main` and `/ (root)`, then Save.
4. Wait for GitHub Pages to publish the site.

## Important limitations inherited from the current prototype
- Reports are stored in the visitor's browser `localStorage`; visitors do not share one central list.
- The admin login and password are client-side demo logic, not secure authentication. Do not use real private data or deploy this as a production admin system without a server-side backend and database.
- SMS is simulated unless a separately secured SMS service is configured. Never put SMS provider secrets in browser code.
- The original C terminal program uses local files for persistence and remains a separate executable; the browser build currently uses the C similarity function from `web_bridge.c`.
