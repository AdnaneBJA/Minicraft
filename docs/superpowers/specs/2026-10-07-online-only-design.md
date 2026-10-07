# Online only: one shared world, straight in

## Goal
Opening the game link puts you in the one shared world on the hosted server. The only thing asked is a name.
There's no title screen, single-player, lobby list or world creation.

**What the user said:**
- "When you open it, you are not presented with a menu but instead directly thrown into a server."
- "All players will be forced to play online in that server."
- "They should just have the option to put their name then they join."
- Keep it simple: the world lasts while people play.
- Remove single-player entirely.

**Assumptions:**
- A 3-hour safety reset is acceptable. It keeps late-join replay fast and under the protocol's 6-hour limit.
- Names must be unique among the players online, so name tags and chat stay readable.

**Success criteria:**
1. Web: click to play → name screen (last name prefilled) → Enter → in the shared world. A second visitor lands in the
   same world.
2. When the last player leaves, the world ends; the next visitor gets a fresh world (new seed).
3. A world older than 3 hours resets for everyone, with a 1-minute warning in chat.
4. A name already in use is refused with "Name already in use", and the player stays on the name screen.
5. Leave Game and a lost connection both return to the name screen; Enter reconnects.
6. Single-player, world saves and the lobby protocol are gone from the code. The tests are updated and pass, and the
   browser smoke test passes.

## Server
- **One world:** `Server` holds `std::unique_ptr<Lobby> world_` (null when nobody is playing).
- **Hello** with a valid, unused name:
  - creates the world if needed (random seed);
  - sends `Joined{seed, history}`;
  - adds the player, and tells everyone "<name> joined the game".
- **Errors:**
  - an invalid name → `Error{"Invalid name"}`;
  - a name already used by a connected player → `Error{"Name already in use"}`.

  In both cases the connection stays open, and the client may send another Hello.
- **Disconnect:** the player is removed ("<name> left the game"). If nobody is left, `world_` is reset to null.
- **Reset:**
  - The world's age is counted in ticks (`history.size()`).
  - At `resetAfterTicks - 60 s` the server sends the chat line "The world resets in 1 minute!".
  - At `resetAfterTicks` it creates a new `Lobby` with a new seed, adds every connected player to it (same ids
    and names), and sends each of them `Joined{seed, history}`. The first tick of the new world carries their Join
    commands.
  - `resetAfterTicks` is a `Server` constructor parameter; it defaults to 3 hours (60 × 60 × 60 × 3 ticks), so the
    tests can use a short one.
- **Unchanged:** Input, Command, Chat, StateHash and the desync check all work as before, on the one world.
- **Removed from the protocol:** `CreateLobby`, `JoinLobby`, `LeaveLobby`, `LobbyList`, `LobbyInfo`. `Joined` loses
  `lobbyId` and `lobbyName`. Leaving is closing the connection.

## Client
**Screens:**
- **Connect** (the start screen): name, the server address on desktop only, and Play.
- **Connecting:** "Connecting..." until `Joined` arrives.
- **Pause** (Esc in game): Return to Game / Options / Leave Game.
- **Options.**
- **Dead:** Respawn / Leave Game.
- **Won:** Continue / Leave Game.

**Removed:**
- the Title, Play, NewWorld, LoadWorld and Lobbies screens, with their actions (CreateWorld, LoadWorld, Save,
  SaveAndQuit, Quit, CreateLobby, JoinLobby, Disconnect);
- `world_save.{h,cpp}`, `createWorld` / `loadWorld` / `saveWorld`;
- the offline tick path (`localCommands_`, the `online_` flag);
- the title logo loading, if only the title screen uses it.

**Behaviour:**
- **Esc on the Connect screen does nothing.** There's nowhere to go back to.
- **Leave Game:** disconnect, back to Connect.
- **Connection lost:** back to Connect, with "Lost connection to the server" when in game, or "Could not connect".
- **A server Error** (like "Name already in use") while connecting: disconnect, back to Connect with the error
  shown.
- **A new `Joined` while in game** (a reset): start the new world, the same path as the first join.
- **`persist`:** keeps the player name file (`player_name.txt`); saves are gone.
- **Desktop:** the same flow. The Connect screen keeps its server address field (default `localhost`), for
  development against a local server.

**game-core is untouched.** `Simulation::singlePlayer` stays for the simulation's own tests; the client always
sets it false.

## Tests
- **Server tests, rewritten for the one world:**
  - `HelloPutsPlayerInWorld`
  - `SecondPlayerJoinsSameWorld`: same seed; A sees "B joined the game".
  - `BothPlayersGetSameTicks`
  - `ChatReachesEveryone`
  - `LeaveIsAnnounced`
  - `LateJoinerGetsHistory`
  - `InvalidNameIsRejected`
  - `DuplicateNameIsRejected`
  - `EmptyWorldEnds`: A leaves; B arrives and gets a different seed, with history starting at tick 1.
  - `WorldResetsAfterLimit`: with a short `resetAfterTicks`, both players get a second `Joined` with a new seed and
    the warning line.
  - `DisconnectDuringTicksIsHarmless`
  - `UnresponsiveClientIsDropped`
  - The client and socket tests stay: URL, unreachable, silent server, large message.
- **game_core_tests:** unchanged.
- **Browser smoke:** Alice and Bob type names and land in the same world, and chat. Alice uses Leave Game, lands on
  the name screen, and rejoins. A third page with Alice's name gets "Name already in use".

## Docs
- **README:**
  - online only: the play link, the flow, no single-player, no saves;
  - controls: Esc menu;
  - the tests section;
  - remove the "Play alone" and save-folder text.
- **ADR 0001:** a note on "one world per server, reset when empty or after 3 h".
- **ADR 0002:** a line on the IndexedDB saves being removed.
