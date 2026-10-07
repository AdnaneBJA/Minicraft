# Minicraft in the browser: design

## Goal
One link (for a resume) that opens the game in a browser. Visitors type a username, and the game connects to the
single hosted server and shows its lobby list, where they create a world or join someone else's. Single-player
also works in the browser, and saves persist between visits.

**What was decided in the conversation:**
- **The browser is the product.** The desktop build only stays where it costs nothing (development and debugging).
- **One hosted server.** No address field on the web; people create and join lobbies there.
- **Free hosting:**
  - the page on GitHub Pages;
  - the server on an Oracle Cloud Always Free VM.
- **Approach A:** replace ENet with WebSockets and keep the C++ server.

**Assumptions (not stated by the user):**
- Players are mostly in one region. Lockstep has no client-side prediction, so input latency is a full round trip;
  the VM's region should be near the expected players.
- The user creates the external accounts (Oracle, DuckDNS) and runs the documented one-time setup on the VM. All
  code, configuration and automation is in the repo.

**Success criteria:**
1. Pushing to `main` builds the WASM client and publishes it to `https://adnanebja.github.io/Minicraft/`.
2. Two browser tabs (or two machines) can enter names, create/join the same world on the hosted server, see
   each other, and play without a desync message.
3. Single-player: New World, save, reload the page, Load World works.
4. The server comes back by itself after a crash or a VM reboot.
5. Existing `game_core_tests` still pass; new tests cover the WebSocket server/client path.

## Architecture

```
Browser (GitHub Pages)                      Oracle VM (Docker Compose)
┌───────────────────────────┐   wss://      ┌──────────────┐  ws://   ┌──────────────────┐
│ Minicraft.wasm            │ ───────────▶  │ Caddy        │ ───────▶ │ minicraft-server │
│ SDL3 + ImGui + game-core  │  binary msgs  │ TLS (Let's   │  :7777   │ lobbies, 60 Hz   │
│ NetworkClient (Emscripten │ ◀───────────  │ Encrypt)     │ ◀─────── │ relay, chat,     │
│ WebSocket API)            │               │ :443         │          │ desync check     │
└───────────────────────────┘               └──────────────┘          └──────────────────┘
```

The protocol (`net-common/protocol.*`) does not change: one encoded message is one WebSocket binary message.
WebSockets run over TCP, so they are reliable and ordered, which is exactly what lockstep needs (the same guarantee
the single ENet channel gave).

## Components

### 1. Transport: `net-common/ws_*`
A small interface hides the platform:

```cpp
// One connection to the server, as the client sees it.
class ClientSocket {
    virtual bool open(const std::string& url) = 0;   // "ws://host:port" or "wss://host"
    virtual void close() = 0;
    virtual void send(std::span<const std::uint8_t> bytes) = 0;
    virtual std::vector<SocketEvent> poll() = 0;     // Opened / Message(bytes) / Closed, in order
};
```

There are two implementations:
- **`ws_client_emscripten.cpp`:** `emscripten/websocket.h`, compiled only for the web. Its callbacks run on the
  browser's main thread and append to the event list.
- **`ws_client_native.cpp`:** IXWebSocket's `ix::WebSocket`, for the desktop dev build and the tests. Its callbacks
  run on IXWebSocket's thread, so they push into a mutex-guarded queue that `poll()` drains.

`NetworkClient` keeps its public interface; only its internals change from `ENetHost*`/`ENetPeer*` to a
`ClientSocket`. `connect(address, name)` takes a URL. A bare `host[:port]` becomes `ws://host:port`, so the desktop
connect screen keeps working with `localhost`.

**Connect timeout:** 5 s. If no `Opened` event arrives by then, the client reports the connection as lost, as ENet
did.

### 2. Server (`server/`)
- **Socket layer:** `ix::WebSocketServer` on `0.0.0.0:<port>` (default 7777) replaces `ENetHost`. IXWebSocket
  serves each connection on its own thread, so its callbacks only push `{connectionId, Connected | Message |
  Disconnected}` events into a mutex-guarded queue.
- **Game logic:** `Server::run()` keeps its single-threaded loop. It drains the queue, ticks the lobbies at 60 Hz,
  and waits on a condition variable until the next tick or event, so all lobby logic stays single-threaded and
  unchanged.
- **Identity:** clients are keyed by a connection id (`std::map<int, Client>`, holding a
  `std::shared_ptr<ix::WebSocket>`) instead of `ENetPeer*`. `sendMessage` becomes `sendBinary` on that socket.
- **Messages:** no per-message limit is set by us. A late joiner's `Joined` can be several MB after hours of play
  (ADR 0001: about 600 KB per player-hour); WebSockets carry that fine. `perMessageDeflate` is enabled, because tick
  history compresses well.
- **Split for testing:** `Server` moves into a `server_lib` static library so the tests can link it.
- **Shutdown:** `Server::stop()` (used by the tests, and by SIGTERM in Docker) ends `run()` and closes the sockets.
- **Removed:** ENet entirely: `enet_util.h`, the FetchContent entry, and the `EnetLibrary` member in the client.

### 3. Web client (`client/`, Emscripten only via `#ifdef __EMSCRIPTEN__`)
- **Main loop:** `Game::run()` becomes `Game::frame()`, which does one iteration. Native keeps
  `while (running_) frame();`. The web uses `emscripten_set_main_loop_arg(frame, game, 0, false)`, with the
  `Game` on the heap so it outlives `main`.
- **Assets:** `--preload-file assets@/assets` bundles them into `Minicraft.data`. `SDL_GetBasePath()` returns `/`
  in the browser, so the existing path code works.
- **Saves:**
  - At start-up, an IDBFS filesystem is mounted at `/persist` and loaded with `FS.syncfs(true)`. The title screen
    opens once that finishes; until then a "Loading..." frame shows.
  - `WorldSaves` uses `/persist/saves`.
  - After every save or delete, `FS.syncfs(false)` writes it to IndexedDB.
- **Multiplayer menu:** on the web, the Connect screen shows only the name field. The address is the build-time
  constant `MINICRAFT_SERVER_URL` (a CMake cache variable, for example `wss://minicraft.duckdns.org`). The last name
  is kept in `/persist/player_name.txt`, so returning visitors don't retype it.
- **Title screen:** "Quit" is hidden on the web, since a tab can't quit.
- **Canvas:**
  - The canvas fills the browser window. The existing "largest whole scale that fits" logic handles any size.
  - SDL keeps keyboard focus on the canvas, so Space, Tab and the arrow keys don't scroll the page or move focus.
- **Shell page (`web/shell.html`):**
  - a dark page with the title, a download progress bar, and a "Click to play" overlay. The click satisfies the
    browser's audio-autoplay rule and focuses the canvas;
  - a one-line footer linking the GitHub repo.
- **Threads:** none (no pthreads), so no COOP/COEP headers are needed. GitHub Pages can't set those anyway.
- **Build:** Release (`-O2`) with `-sALLOW_MEMORY_GROWTH=1`. ImGui (the F3 debug panel) stays in.

### 4. Build configuration (CMake)
- **`MINICRAFT_BUILD_CLIENT` (default ON):** OFF skips SDL3, ImGui and the client. The server Docker image builds
  with OFF.
- **`MINICRAFT_BUILD_TESTS` (default ON for native, OFF for Emscripten).**
- **`MINICRAFT_SERVER_URL`:** the web client's server.
- **IXWebSocket:** fetched with FetchContent, without TLS (`USE_TLS=OFF`), because Caddy handles TLS. It is not
  built for Emscripten.
- **Under Emscripten:** the client links `-sWEBSOCKET` APIs (`-lwebsocket.js`), the preload flags, and
  `--shell-file`. The output is `Minicraft.html` (renamed to `index.html` on deploy).
- **If IXWebSocket won't build with MinGW:** desktop multiplayer is compiled out (`MINICRAFT_NATIVE_NET=OFF`) and
  noted in the README. The web build doesn't depend on it, and the server and tests build on Linux in CI and Docker.

### 5. Deployment
**Web (`.github/workflows/web.yml`):**
- runs on push to `main` and on manual dispatch;
- sets up emsdk (pinned version), runs `emcmake cmake -DMINICRAFT_SERVER_URL=${{ vars.MINICRAFT_SERVER_URL }}`
  and builds;
- uploads `Minicraft.{html→index.html,js,wasm,data}` as a Pages artifact and deploys with
  `actions/deploy-pages`.

**Tests (`.github/workflows/tests.yml`):** on push and PR, an Ubuntu GCC build of `game_core_tests` and
`server_tests`, then `ctest`.

**Server (`deploy/`):**
- `server/Dockerfile`: a multi-stage build from Debian with `MINICRAFT_BUILD_CLIENT=OFF`, then a slim runtime
  image. It builds natively on the ARM VM.
- `deploy/docker-compose.yml`:
  - `server`, with `restart: unless-stopped`; port 7777 is internal only;
  - `caddy`, with ports 80/443, a volume for its certificates, and `restart: unless-stopped`.
- `deploy/Caddyfile`: `{$DOMAIN} { reverse_proxy server:7777 }`. Caddy proxies WebSocket upgrades automatically.
- `deploy/README.md` gives the one-time steps for the user:
  1. Create the Oracle account and an Ubuntu ARM VM (Always Free shape), in a region near the players.
  2. Open TCP 80/443 in the VCN security list **and** in the VM's iptables. Oracle's Ubuntu images block them
     by default.
  3. Point a DuckDNS subdomain at the VM's IP.
  4. Install Docker, clone the repo, and run `DOMAIN=<name>.duckdns.org docker compose up -d --build`.
  5. Set the GitHub repo variable `MINICRAFT_SERVER_URL=wss://<name>.duckdns.org`, enable Pages (source: GitHub
     Actions), and re-run the web workflow.
  6. Upgrade the account to Pay-As-You-Go (it stays free within Always Free limits). Oracle may reclaim
     Always Free instances that sit idle, and a quiet game server looks idle.
  7. Update by running `git pull && docker compose up -d --build`.

## Error handling
- **Server unreachable or not answering:** after 5 s the menu shows "Connection lost" and goes back to the name
  screen (the existing `takeConnectionLost()` path).
- **Connection drops mid-game:** handled the same way as with ENet today: back to the connect screen.
- **IndexedDB unavailable (private mode):** saves still work for the session (in memory), and a sync failure is
  logged, not fatal.
- **`MINICRAFT_SERVER_URL` empty in the web build:** the Multiplayer entry shows "No server configured".
- **Bad messages:** the server already ignores malformed bytes (bounds-checked decoding).

## Testing
- **Unit:** the existing `game_core_tests` (41).
- **New `server_tests` (GoogleTest, native):** they start `Server` on a free port in a background thread, then
  connect two native `NetworkClient`s over WebSocket and check that:
  - both receive `Welcome` and the lobby list;
  - A creates a lobby and B joins it;
  - both receive the same ticks containing both players' inputs;
  - chat reaches both clients;
  - closing B makes A's lobby report "left";
  - a late joiner gets the history.
- **Web build:** compiled in a local `emscripten/emsdk` Docker container (the same pinned version as CI).
- **Browser smoke test:** serve the build locally, run the server from Docker, and drive two headless Chromium tabs
  with Playwright: load, click to play, enter names, create and join the lobby, take screenshots, and check there's
  no "Desync" chat line. Single-player save → reload → load is checked the same way.
- **Live check after deploy:** open the Pages URL in two browsers.

## Out of scope
- Client-side prediction (latency hiding).
- Desktop players on the hosted server.
- Accounts, authentication, persistence of multiplayer worlds, and moderation beyond the existing name and chat
  validation.
- Mobile/touch controls. The game needs a keyboard; the shell page says so.

## Documentation
- **README:** a "Play in your browser" link at the top, plus updated architecture, multiplayer and build sections
  (WebSocket instead of ENet, the web build commands, a link to `deploy/README.md`).
- **ADR 0002 "WebSocket transport for the browser":** supersedes the transport part of ADR 0001. ADR 0001 gets
  one line pointing to it.
