// SPDX-License-Identifier: GPL-3.0-only
// Tests for the LAN transport (GameOS/gameos/net/gos_net). Everything runs
// over 127.0.0.1 inside this process: a host and one or more clients are
// pumped in turn until the expected events arrive.

#include "mc2test.h"

#include "net/gos_net.h"

#include <chrono>
#include <cstring>

namespace {

// Ports away from the game default so a running game does not interfere.
const uint16_t kPort = 37015;

using Clock = std::chrono::steady_clock;

// Pump both ends until pred() is true or timeout.
template <typename Pred>
bool pumpUntil(GosNetSession& a, GosNetSession& b, std::vector<gosNetEvent>& ea,
    std::vector<gosNetEvent>& eb, Pred pred, int timeoutMs = 5000)
{
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < end) {
        gosNetEvent ev;
        while (a.poll(&ev, 1))
            ea.push_back(ev);
        while (b.poll(&ev, 1))
            eb.push_back(ev);
        if (pred())
            return true;
    }
    return pred();
}

bool hasEvent(const std::vector<gosNetEvent>& evs, gosNetEventType t)
{
    for (const gosNetEvent& e : evs)
        if (e.type == t)
            return true;
    return false;
}

} // namespace

TEST(net_connect_and_reliable_order)
{
    GosNetSession server, client;
    CHECK(server.host(kPort, 4));
    CHECK(client.join("127.0.0.1", kPort));
    std::vector<gosNetEvent> es, ec;
    CHECK(pumpUntil(server, client, es, ec,
        [&] { return hasEvent(es, gosNetEv_Connect) && hasEvent(ec, gosNetEv_Connect); }));
    CHECK_EQ(server.connectedCount(), 1);
    CHECK(client.connected(0));
    CHECK_EQ(client.peerAddress(0), std::string("127.0.0.1"));

    int serverPeer = -1;
    for (const gosNetEvent& e : es)
        if (e.type == gosNetEv_Connect)
            serverPeer = e.peer;
    CHECK(serverPeer >= 0);

    // 1000 reliable packets each way, in order, intact.
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        uint32_t v[4] = {(uint32_t)i, 0xC0FFEEu, (uint32_t)(i * 7), 0xA5A5A5A5u};
        CHECK(client.send(0, v, sizeof(v), gosNet_Reliable));
        v[1] = 0xBEEFu;
        CHECK(server.send(serverPeer, v, sizeof(v), gosNet_Reliable));
    }
    es.clear();
    ec.clear();
    auto countRecv = [](const std::vector<gosNetEvent>& evs) {
        int n = 0;
        for (const gosNetEvent& e : evs)
            n += e.type == gosNetEv_Receive ? 1 : 0;
        return n;
    };
    CHECK(pumpUntil(server, client, es, ec,
        [&] { return countRecv(es) == N && countRecv(ec) == N; }, 10000));

    int expect = 0;
    for (const gosNetEvent& e : es) {
        if (e.type != gosNetEv_Receive)
            continue;
        CHECK_EQ(e.data.size(), (size_t)16);
        uint32_t v[4];
        memcpy(v, e.data.data(), 16);
        CHECK_EQ(v[0], (uint32_t)expect);
        CHECK_EQ(v[1], 0xC0FFEEu);
        CHECK_EQ(v[2], (uint32_t)(expect * 7));
        ++expect;
    }
    CHECK_EQ(expect, N);
    expect = 0;
    for (const gosNetEvent& e : ec) {
        if (e.type != gosNetEv_Receive)
            continue;
        uint32_t v[4];
        memcpy(v, e.data.data(), 16);
        CHECK_EQ(v[0], (uint32_t)expect);
        CHECK_EQ(v[1], 0xBEEFu);
        ++expect;
    }
    CHECK_EQ(expect, N);

    // Clean disconnect reaches the server.
    es.clear();
    ec.clear();
    client.disconnect(0);
    CHECK(pumpUntil(server, client, es, ec, [&] { return hasEvent(es, gosNetEv_Disconnect); }));
    CHECK_EQ(server.connectedCount(), 0);
}

TEST(net_large_packet_and_broadcast)
{
    GosNetSession server, c1, c2;
    CHECK(server.host(kPort + 1, 4));
    CHECK(c1.join("127.0.0.1", kPort + 1));
    CHECK(c2.join("127.0.0.1", kPort + 1));
    std::vector<gosNetEvent> es, e1, e2;
    const auto end = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < end && !(c1.connected(0) && c2.connected(0) && server.connectedCount() == 2)) {
        gosNetEvent ev;
        while (server.poll(&ev, 1)) es.push_back(ev);
        while (c1.poll(&ev, 1)) e1.push_back(ev);
        while (c2.poll(&ev, 1)) e2.push_back(ev);
    }
    CHECK_EQ(server.connectedCount(), 2);

    // 64 KB payload is fragmented and reassembled by the transport.
    std::vector<uint8_t> big(64 * 1024);
    for (size_t i = 0; i < big.size(); ++i)
        big[i] = (uint8_t)(i * 31 + 7);
    server.broadcast(big.data(), big.size(), gosNet_Reliable);
    e1.clear();
    e2.clear();
    const auto end2 = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < end2 && !(hasEvent(e1, gosNetEv_Receive) && hasEvent(e2, gosNetEv_Receive))) {
        gosNetEvent ev;
        while (server.poll(&ev, 1)) es.push_back(ev);
        while (c1.poll(&ev, 1)) e1.push_back(ev);
        while (c2.poll(&ev, 1)) e2.push_back(ev);
    }
    for (auto* evs : {&e1, &e2}) {
        bool got = false;
        for (const gosNetEvent& e : *evs)
            if (e.type == gosNetEv_Receive) {
                got = true;
                CHECK(e.data == big);
            }
        CHECK(got);
    }
}

TEST(net_discovery_query)
{
    GosNetSession server;
    CHECK(server.host(kPort + 2, 4));
    const char payload[] = "room:Test|2/4";
    server.setDiscoveryPayload(payload, sizeof(payload));

    GosNetScanner scanner;
    CHECK(scanner.open());
    gosNetRoom room;
    bool found = false;
    const auto end = Clock::now() + std::chrono::seconds(3);
    int round = 0;
    while (!found && Clock::now() < end) {
        if (round++ % 50 == 0) {
            scanner.query("127.0.0.1", kPort + 2); // direct IP
            scanner.query(nullptr, kPort + 2);     // LAN broadcast + loopback
        }
        gosNetEvent ev;
        server.poll(&ev, 2); // the host answers from inside its service loop
        while (scanner.poll(&room))
            if (room.payload.size() == sizeof(payload)
                && memcmp(room.payload.data(), payload, sizeof(payload)) == 0)
                found = true;
    }
    CHECK(found);
    CHECK_EQ(room.port, (uint16_t)(kPort + 2));
    CHECK_EQ(room.address, std::string("127.0.0.1"));
}

TEST(net_join_unreachable_times_out)
{
    GosNetSession client;
    client.setTimeout(1000);
    CHECK(client.join("127.0.0.1", kPort + 9)); // nobody listening
    gosNetEvent ev;
    bool disconnected = false;
    const auto end = Clock::now() + std::chrono::seconds(8);
    while (!disconnected && Clock::now() < end)
        while (client.poll(&ev, 5))
            if (ev.type == gosNetEv_Disconnect)
                disconnected = true;
    CHECK(disconnected);
    CHECK(!client.connected(0));
}
