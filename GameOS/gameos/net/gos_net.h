// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Thin networking layer for LAN multiplayer. The game only talks to this
// header; ENet (thirdparty/enet) is an implementation detail of gos_net.cpp.
//
//  - GosNetSession: one host (server, up to maxPeers clients) or one client
//    connection. Two channels: reliable+ordered and unreliable (sequenced).
//    Peer ids are small integers. On a client the server is always peer 0.
//  - Room discovery uses the game port too: a scanner sends a small query
//    datagram (broadcast on the LAN, or to one address for "direct IP") and
//    every host answers with its discovery payload (an opaque blob the game
//    fills in). No second port is needed.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum { GOS_NET_DEFAULT_PORT = 27015 };

enum gosNetChannel {
    gosNet_Reliable = 0,
    gosNet_Unreliable = 1,
};

enum gosNetEventType {
    gosNetEv_None = 0,
    gosNetEv_Connect,
    gosNetEv_Disconnect, // clean disconnect or timeout
    gosNetEv_Receive,
};

struct gosNetEvent {
    gosNetEventType type = gosNetEv_None;
    int peer = -1;
    std::vector<uint8_t> data;
};

struct gosNetRoom {
    std::string address; // dotted IPv4
    uint16_t port = 0;
    std::vector<uint8_t> payload;
};

// ENet is initialised on first use; this is only needed to free it early.
void gos_NetGlobalShutdown();

// This machine's LAN IPv4 address (the one used for the default route), or
// "127.0.0.1" when there is no network. Sends no packets.
std::string gos_NetLocalAddress();

class GosNetSession {
public:
    GosNetSession();
    ~GosNetSession();
    GosNetSession(const GosNetSession&) = delete;
    GosNetSession& operator=(const GosNetSession&) = delete;

    // Start a server on port (0.0.0.0). maxPeers is the number of clients.
    bool host(uint16_t port, int maxPeers);
    // Begin connecting to a server; completion arrives as gosNetEv_Connect
    // (peer 0) or gosNetEv_Disconnect on failure. host may be a name or IP.
    bool join(const char* host, uint16_t port);
    // Pump the connection; returns true and fills ev while events are pending.
    // timeoutMs > 0 waits for the first event up to that long.
    bool poll(gosNetEvent* ev, uint32_t timeoutMs = 0);

    bool send(int peer, const void* data, size_t len, gosNetChannel ch);
    // To every connected peer except exceptPeer (-1: none).
    void broadcast(const void* data, size_t len, gosNetChannel ch, int exceptPeer = -1);
    void flush();
    // Graceful disconnect of one peer (server) or of the server (client).
    void disconnect(int peer);
    // Disconnect everyone, flush, and free the socket.
    void shutdown();

    bool active() const { return host_ != nullptr; }
    bool isServer() const { return server_; }
    bool connected(int peer) const;
    int maxPeers() const;
    int connectedCount() const;
    std::string peerAddress(int peer) const;
    uint32_t peerRoundTripMs(int peer) const;
    // Seconds without traffic before a peer counts as gone (default 10).
    void setTimeout(uint32_t ms);

    // Server only: reply payload for discovery queries.
    void setDiscoveryPayload(const void* data, size_t len);

    // Used internally by the ENet intercept callback.
    int handleRawPacket(void* host);

private:
    void* host_ = nullptr; // ENetHost*
    bool server_ = false;
    uint32_t timeoutMs_ = 10000;
    std::vector<uint8_t> discovery_;
};

class GosNetScanner {
public:
    GosNetScanner();
    ~GosNetScanner();
    GosNetScanner(const GosNetScanner&) = delete;
    GosNetScanner& operator=(const GosNetScanner&) = delete;

    bool open();
    void close();
    // address == nullptr: LAN broadcast plus loopback; otherwise that host.
    void query(const char* address, uint16_t port = GOS_NET_DEFAULT_PORT);
    // Returns true while answers are pending.
    bool poll(gosNetRoom* room);

private:
    intptr_t socket_ = -1;
};

// Discovery datagrams start with these 4 bytes (query / answer).
extern const uint8_t gosNetQueryMagic[4];
extern const uint8_t gosNetAnswerMagic[4];
