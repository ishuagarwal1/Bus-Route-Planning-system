# BusRoute Website

- Local backend + HTTP server: `src/server.c`, `src/backend.c` (C, raw sockets, Dijkstra routing)
- Local database: `src/db.c` -> text files in `data/`
- Frontend: `public/` (HTML, CSS, JavaScript)
- Vercel backend: Node.js functions in `api/` and `lib/api-handler.js`, with Neon Postgres

## Step 1: one-time free setup
- Windows: install MSYS2 (https://www.msys2.org), open "MSYS2 UCRT64" and run:
      pacman -S mingw-w64-ucrt-x86_64-gcc
  then add C:\msys64\ucrt64\bin to your Windows PATH and restart VS Code.
- Linux: sudo apt install gcc      macOS: xcode-select --install
- Check: gcc --version

## Step 2: run
- Double-click run.bat (Windows) or ./run.sh (Linux/macOS), OR
- In VS Code: File > Open Folder > this folder, press Ctrl+Shift+B.
Then open http://localhost:8080   (stop the server with Ctrl+C)

## Deploy to Vercel
Vercel does not run the project's always-on C socket server, and its filesystem
is not persistent. The Vercel deployment uses the Node.js API function in
`api/` and Neon Postgres for saved bookings, stops, and routes. The existing C
server and text-file database remain available for local development.

1. Push this project to a Git provider and import it into Vercel.
2. Create a Neon Postgres database and copy its connection string.
3. In the Vercel project settings, add these environment variables for the
   Production deployment (and Preview if desired):
   - `DATABASE_URL`: the Neon connection string.
   - `ADMIN_KEY`: a long, private password for the Admin section.
4. Redeploy. The API creates its tables and loads the default stops and routes
   on its first request. No build command is needed; the static site is in
   `public/`.

The local C server still uses its existing local admin key (`admin123`). Do not
use that default as the deployed `ADMIN_KEY`.
The `.gitignore` excludes the local bookings file and compiled executables so
they are not pushed to the deployment repository.

## API
GET /api/stops | /api/routes | /api/plan?from=1&to=10&by=distance|time|fare | /api/bookings
POST /api/bookings {passenger,from,to,by}    DELETE /api/bookings/:id
POST /api/stops, /api/routes (header `x-admin-key`: `ADMIN_KEY` on Vercel;
`admin123` on the local C server)
