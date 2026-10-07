// minicraft-server: run it, then pick Multiplayer in the game and connect to this machine.
//   minicraft-server [port]     (default 7777)
// It speaks WebSockets (ws://). In production a reverse proxy in front of it adds TLS (wss://); see deploy/.
#include "protocol.h"
#include "server.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {

Server* runningServer = nullptr;

// Ctrl+C, or `docker stop`: leave the loop so the connections close cleanly.
void onStopSignal(int) {
    if (runningServer) runningServer->stop();
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // log lines show up right away, even when redirected to a file
    const int port = argc > 1 ? std::atoi(argv[1]) : protocol::kDefaultPort;
    if (port <= 0 || port > 65535) {
        std::printf("Usage: minicraft-server [port]\n");
        return 1;
    }
    Server server;
    if (!server.start(static_cast<std::uint16_t>(port))) return 1;
    runningServer = &server;
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
    server.run();
    std::printf("minicraft-server stopped\n");
    return 0;
}
