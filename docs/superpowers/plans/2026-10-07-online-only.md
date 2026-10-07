# Online only: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opening the game puts you on a name screen and then straight into the server's one shared world. No
single-player, no saves, no lobbies.

**Architecture:**
- **Server:** keeps one `Lobby` (the world). Hello joins it, the last player leaving ends it, and it resets after 3 h.
- **Protocol:** loses the lobby messages.
- **Client:** starts on the Connect screen, and loses the title/world screens and `world_save`.

**Tech Stack:** C++20, IXWebSocket, SDL3, Emscripten, GoogleTest, Playwright (all as already in the repo).

**Spec:** `docs/superpowers/specs/2026-10-07-online-only-design.md`

## Global Constraints
- **game-core is untouched.**
- **The reset period is 3 hours by default** (`60 * 60 * 60 * 3` ticks), with the warning 60 s before. It is a `Server`
  constructor parameter.
- **Error texts are exact:** `"Invalid name"`, `"Name already in use"`, `"The world resets in 1 minute!"`,
  `"<name> joined the game"`, `"<name> left the game"`.
- **Commits** carry no Claude/AI lines. Branch `online-only`.
- **Code style** follows the surrounding code.

## Review Focus
1. **A player disconnects in the same drain as a reset.** No crash, and they're not added to the new world (covered:
   reset test with three players, one leaving right before the reset).
2. **A second Hello after a refused name** must work: the client retries with a new name on the same screen.
   `DuplicateNameIsRejected` then retries with another name on the same connection.
3. **The name screen when the server is down:** "Could not connect", and Enter retries (smoke: nothing extra; native
   test `UnreachableServerReportsLost` exists).
4. **A reset while a player is still catching up** on a long history: the new `Joined` must clear the queued old ticks
   (`NetworkClient` clears `ticks_` on Joined; asserted in `WorldResetsAfterLimit`).
5. **Esc on the Connect screen** must not quit or open anything.

---

### Task 1: One world on the server; protocol and NetworkClient without lobbies

**Files:**
- Modify: `net-common/protocol.h`, `net-common/protocol.cpp`, `server/server.h`, `server/server.cpp`,
  `server/lobby.h` (constructor only), `client/network_client.h`, `client/network_client.cpp`,
  `server/tests/server_test.cpp`
- Modify, to keep compiling: `client/main.cpp` (the `pollNetwork` and lobby actions only)

**Interfaces:**
- **Produces:**
  - `protocol::Joined { std::uint32_t seed; std::vector<TickInput> history; }`
  - `Server(int resetAfterTicks = kDefaultResetAfterTicks)`
  - `NetworkClient`: `connect`, `disconnect`, `poll`, `state`, `playerId`, `takeConnectionLost`, `takeJoined`,
    `ticks`, `ticksWaiting`, `takeChat`, `takeError`, `sendInput`, `sendCommand`, `sendChat`, `sendStateHash`.
    Removed: `takeLobbies`, `createLobby`, `joinLobby`, `leaveLobby`.

- [ ] **Step 1: Rewrite `server_test.cpp` for the one world** (the tests from the spec).
  - The fixture takes an optional `resetAfterTicks`.
  - Helper `join(client, name)` connects and waits for `Joined`.
  - `DuplicateNameIsRejected`: the second "Alice" gets the Error, then the same client calls `connect(address,
    "Alice2")` and joins.
  - `WorldResetsAfterLimit` (resetAfterTicks = 180, i.e. 3 s):
    - A, B and C join; C disconnects at about 2.9 s;
    - A and B get a chat line containing "resets", then a second `Joined` with a different seed;
    - after the next tick, `ticks()` front is tick 1 and holds exactly A and B.
- [ ] **Step 2: Build `server_tests`.** Expected: it fails to compile (no `Joined::seed`-only, no
  `Server(int)`, ...).
- [ ] **Step 3: Protocol.**
  - Remove `CreateLobby`, `JoinLobby`, `LeaveLobby`, `LobbyInfo` and `LobbyList`, and their `MessageType` values.
    Keep the numbering contiguous: client and server ship together.
  - `Joined` is `seed` plus `history`. Update the encode/decode in `protocol.cpp`.
  - Update the header comment ("Each server runs one world...").
- [ ] **Step 4: Server.**
  - Replace `lobbies_` and `nextLobbyId_` with `std::unique_ptr<Lobby> world_`, `int resetAfterTicks_` and
    `bool resetWarned_`.
  - `Lobby`'s constructor becomes `Lobby(std::uint32_t seed)`. Remove `id()` and `name()`.
  - On Hello:
    - check the name is valid, else send "Invalid name";
    - check no connected client already has it, else send "Name already in use";
    - set the name;
    - if `!world_`, create the world with a random seed;
    - send `Joined`, then `addMember`, then tell everyone "<name> joined the game".
  - `disconnect`: if the client was named, `removeMember` and tell everyone "<name> left the game". If
    `world_->empty()`, reset `world_` and `resetWarned_`.
  - `tickLobbies` becomes `tickWorld`:
    - if there's no world, return;
    - send `nextTick()` to every member;
    - when `history().size() == resetAfterTicks_ - 60*60` and the warning hasn't been sent, send the warning chat;
    - when `history().size() >= resetAfterTicks_`, call `resetWorld()`.
  - `resetWorld` creates a new world with a new seed, adds every named client to it, and sends each `Joined`.
  - Messages from a client without a name are still ignored, except Hello (unchanged rule). Chat requires a name.
  - Delete `createLobby`, `joinLobby`, `leaveLobby`, `sendLobbyList` and `lobbyOf`.
- [ ] **Step 5: NetworkClient.**
  - Remove the lobby API and `lobbies_`.
  - `handleMessage` drops `LobbyList`.
  - On `Joined`: `ticks_.clear()`, store it, `inLobby_ = true` (rename to `inWorld_`).
- [ ] **Step 6: `main.cpp`, so it compiles.**
  - `pollNetwork` drops the lobby list handling.
  - On `Joined`: `enterWorld("", true)` (the title "Joining the world...").
  - `Kind::LeaveGame` → `net_.disconnect(); leaveWorld(); menu_.openConnect();`.
  - Remove the `CreateLobby`/`JoinLobby` cases. Task 2 removes the actions themselves.
- [ ] **Step 7: Build all and run ctest.** Expected: all tests pass.
- [ ] **Step 8: Commit.** `feat(server): one shared world per server: join on Hello, unique names, reset when empty or after 3 h`

### Task 2: The client is online only

**Files:**
- Modify: `client/main.cpp`, `client/game_menu.h`, `client/game_menu.cpp`, `client/persist.h`, `CMakeLists.txt`
- Delete: `client/world_save.h`, `client/world_save.cpp`

- [ ] **Step 1: GameMenu.**
  - Screens: `None, Connect, Connecting, Pause, Options, Dead, Won`.
  - Actions: `None, Resume, Respawn, ToggleSound, VolumeDown, VolumeUp, Connect, LeaveGame`.
  - Remove `openTitle`, `setCanQuit`, the world list, the name/seed fields, `nameProblem`, the splash text, and the
    Title/Play/NewWorld/LoadWorld/Lobbies draw and key code.
  - **Pause:** always the online list.
  - **Dead/Won:** second entry "Leave Game".
  - **Connect screen:**
    - the Minicraft logo at the top (the existing logo texture, so `load` stays);
    - a "Name:" row, an address row on desktop only (`fixedServer_` empty), and a "Play" row;
    - a message line;
    - footers "(ENTER to play)". The Escape footer is removed.
  - **Esc on Connect:** ignored. Esc on Connecting: back to Connect, sending `Kind::LeaveGame` so the game
    disconnects.
  - Remove `setOnline`; the menu is always online.
- [ ] **Step 2: `main.cpp`.**
  - Remove `createWorld`, `loadWorld`, `saveWorld`, `saves_`, `worldName_`, `online_`, `localCommands_`, the
    offline tick path in `update`, and `Kind::Quit`.
  - `send(command)` → `net_.sendCommand`.
  - `init()` ends with `menu_.openConnect()`.
  - Esc in game → `menu_.openPause()` (the world keeps going).
  - A server Error while `Connecting`: `net_.disconnect()`, `menu_.openConnect()`, then show the error.
  - `leaveWorld()` resets the view and `inWorld_`.
  - Keep `rememberPlayerName` and its use, and the name prefill, on both platforms. The prefill moves out of the
    `#ifdef`; only `setFixedServer` stays web-only.
  - Remove every `online_` condition: the online branch is the only one.
- [ ] **Step 3:** Delete `world_save.*`. Remove `client/world_save.cpp` from CMake. Update `persist.h`'s comment
  ("the last player name").
- [ ] **Step 4: Build.** Native: `cmake --build build/native` then ctest; the web build via the emsdk container.
  Expected: no errors, and all tests pass.
- [ ] **Step 5: Desktop startup check.** Start the game for 4 s; it stays alive.
- [ ] **Step 6: Commit.** `feat(client): online only: start on the name screen, no single-player or saves`

### Task 3: Smoke test and docs

**Files:**
- Modify: `web/smoke/smoke.mjs`, `README.md`, `docs/adr/0001-multiplayer-lockstep-over-enet.md`,
  `docs/adr/0002-websocket-transport.md`

- [ ] **Step 1: `smoke.mjs`.**
  - Alice and Bob: click, clear the name, type it, Enter, wait 3 s, then screenshots. Both screens show the other's
    name tag (checked by eye).
  - Bob chats.
  - Alice: Esc → Down ×2 → Enter (Leave Game); screenshot of the name screen; Enter → back in the world.
  - A third page types "Alice" while Alice is online; screenshot shows "Name already in use".
  - Fail on page errors. Check the server log for desync.
- [ ] **Step 2: Run** against the server (Docker image or the native `minicraft-server.exe`) with the rebuilt web
  build, and read the screenshots.
- [ ] **Step 3: README.**
  - The intro becomes online only.
  - Remove "Play alone" and the saves text.
  - "Play together" is about the hosted world, its 3 h reset and the local server.
  - Controls: Esc menu.
  - Tests: the new server test list and the count.
  - Status line.
  - **ADRs:** 0001 gets a one-line note on one world per server; 0002 a line on the saves being removed.
- [ ] **Step 4: Commit.** `test,docs: online-only smoke test and README`

### Task 4: Finish
- [ ] Final whole-branch review, the fix pass, push, and open the PR.
