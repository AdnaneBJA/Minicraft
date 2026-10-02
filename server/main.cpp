// minicraft-server: run it, then pick Multiplayer in the game and connect to this machine.
//   minicraft-server [port]     (default 7777)
#include "enet_util.h"
#include "protocol.h"
#include "server.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // log lines show up right away, even when redirected to a file
    const int port = argc > 1 ? std::atoi(argv[1]) : protocol::kDefaultPort;
    if (port <= 0 || port > 65535) {
        std::printf("Usage: minicraft-server [port]\n");
        return 1;
    }
    const EnetLibrary enet;
    if (!enet.ok()) {
        std::printf("Could not start ENet\n");
        return 1;
    }
    Server server;
    if (!server.start(static_cast<std::uint16_t>(port))) return 1;
    server.run();
    return 0;
}
