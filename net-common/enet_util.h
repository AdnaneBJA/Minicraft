#pragma once

#include "protocol.h"

#include <enet/enet.h>

#include <cstdint>
#include <vector>

// The two things the client and the server do with ENet besides connecting: start the library, and send a message.

// Starts ENet for as long as it exists (enet_initialize / enet_deinitialize).
class EnetLibrary {
public:
    EnetLibrary() : ok_(enet_initialize() == 0) {}
    ~EnetLibrary() {
        if (ok_) enet_deinitialize();
    }
    EnetLibrary(const EnetLibrary&) = delete;
    EnetLibrary& operator=(const EnetLibrary&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_;
};

// Sends a message to one peer on channel 0, reliable and in order (ENet resends it until it arrives).
template <typename Message>
void sendMessage(ENetPeer* peer, const Message& message) {
    const std::vector<std::uint8_t> bytes = protocol::encode(message);
    ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    // On success ENet owns the packet; if the peer is gone it's still ours to free.
    if (enet_peer_send(peer, 0, packet) < 0) enet_packet_destroy(packet);
}
