# Minicraft

A 2D top-down survival/crafting game with a shared persistent world (C++20 + SDL3).

> Status: an SDL3 window that draws a square.

## Run

Open the folder in CLion and run the **Minicraft** target. CMake downloads and builds SDL3 automatically
on the first configure, which takes about a minute.

From a terminal:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/Minicraft
```
