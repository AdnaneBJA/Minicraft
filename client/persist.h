#pragma once

#include <filesystem>

// Where the game keeps what must outlive it: saves and the last multiplayer name.
//
// On the desktop that's the per-user data folder (e.g. %APPDATA%/Minicraft/Minicraft on Windows). In a browser it's
// /persist, a folder Emscripten keeps in the browser's IndexedDB: persistMount() loads it when the page opens,
// and persistFlush() writes it back after every change.

// Starts loading the browser's stored files (nothing to do on the desktop).
void persistMount();
// True once persistMount() has finished; until then the folder is empty.
bool persistReady();
// Writes changes in the folder back to the browser's storage (nothing to do on the desktop).
void persistFlush();
std::filesystem::path persistDirectory();
