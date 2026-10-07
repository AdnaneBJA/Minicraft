# ADR 0002: WebSockets instead of ENet, so the game can run in a browser

Supersedes the **Transport** part of [ADR 0001](0001-multiplayer-lockstep-over-enet.md). Lockstep, late join by
replay and desync detection are unchanged.

## Context
- The game should be playable from a link: compiled to WebAssembly with Emscripten, served as a web page, with
  multiplayer on one hosted server.
- **Browsers can't send UDP.** ENet runs on UDP, so a browser can't speak it. Tunnelling ENet's UDP through
  Emscripten's socket emulation would need an extra proxy, and would put ENet's own resends on top of a TCP
  connection.
- **A page served over HTTPS may only open `wss://` connections**, so the server needs TLS and a domain name.
- Lockstep needs every message delivered, in order, which ENet's one reliable channel gave.

## Decision
- **Transport:** WebSockets, with one protocol message per binary WebSocket message. The message format
  (`net-common/protocol.*`) didn't change.
- **Client:**
  - `ClientSocket` (`net-common/client_socket.h`) hides the platform. In the browser it's Emscripten's
    WebSocket API (`client_socket_web.cpp`); elsewhere, for the desktop build and the tests, it's IXWebSocket
    (`client_socket_native.cpp`).
  - `NetworkClient` kept its interface.
- **Server:**
  - IXWebSocket's server serves each connection on its own thread. Those threads only queue events, and
    `Server::run()` handles them on one thread with the 60 Hz tick, so the lobby logic stayed single-threaded.
  - Clients are held by `weak_ptr`, so a send to a connection that has just closed does nothing instead of
    crashing.
- **TLS** is not in the game server. Caddy runs in front of it (`deploy/`), gets a Let's Encrypt certificate and
  proxies `wss://` to the server's plain `ws://`.
- **The web build:**
  - one page on GitHub Pages, built by GitHub Actions;
  - the server URL is compiled in (`MINICRAFT_SERVER_URL`), so the multiplayer screen only asks for a name;
  - saves go to the browser's IndexedDB.

## Consequences
- **Good:**
  - Anyone with the link can play, with nothing to install.
  - The protocol, the lobbies and the lockstep code didn't change.
  - The server tests now run the real server and real clients over localhost, and the same tests run on Linux
    in CI and in the Docker build.
- **TCP head-of-line blocking:** a lost packet delays the ones behind it. Lockstep needed every tick in order
  anyway (ENet's reliable channel behaved the same), so nothing is lost, but latency spikes on bad connections
  are a little worse than before.
- **Browser players only on the hosted server.** Desktop builds could connect (the desktop client speaks
  WebSockets too), but a native build and a WASM build may compute floats differently (different `sin`/`cos`
  implementations), and lockstep needs bit-identical simulations. The desync check would report it.
- **No compression:** IXWebSocket is built without zlib. A late joiner downloads the tick history uncompressed
  (about 600 KB per player-hour, ADR 0001).
- **Hosting:** the server needs a machine with a domain name and ports 80/443. `deploy/README.md` describes a
  free setup.
