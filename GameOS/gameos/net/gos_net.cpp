// SPDX-License-Identifier: GPL-3.0-only
#include "gos_net.h"

#include <enet/enet.h>

#include <algorithm>
#include <cstring>

const uint8_t gosNetQueryMagic[4] = {'M', 'C', '2', '?'};
const uint8_t gosNetAnswerMagic[4] = {'M', 'C', '2', '!'};

namespace {

int g_enetRefs = 0;
bool g_enetUp = false;

bool enetUp()
{
    if (!g_enetUp) {
        if (enet_initialize() != 0)
            return false;
        g_enetUp = true;
    }
    return true;
}

// ENetHost has no user pointer: map hosts back to their sessions for the
// raw packet intercept (discovery queries).
struct HostEntry {
    ENetHost* host;
    GosNetSession* session;
};
std::vector<HostEntry> g_hosts;

GosNetSession* sessionFor(ENetHost* host)
{
    for (const HostEntry& e : g_hosts)
        if (e.host == host)
            return e.session;
    return nullptr;
}

int ENET_CALLBACK interceptCallback(ENetHost* host, ENetEvent*)
{
    GosNetSession* s = sessionFor(host);
    return s ? s->handleRawPacket(host) : 0;
}

inline ENetHost* H(void* p) { return static_cast<ENetHost*>(p); }

} // namespace

void gos_NetGlobalShutdown()
{
    if (g_enetUp && g_enetRefs == 0) {
        enet_deinitialize();
        g_enetUp = false;
    }
}

std::string gos_NetLocalAddress()
{
    std::string result = "127.0.0.1";
    if (!enetUp())
        return result;
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL)
        return result;
    // connect() on a UDP socket only picks the route and local address.
    ENetAddress probe;
    enet_address_set_host_ip(&probe, "192.0.2.1"); // TEST-NET-1, never reached
    probe.port = 9;
    ENetAddress local;
    if (enet_socket_connect(s, &probe) == 0 && enet_socket_get_address(s, &local) == 0
        && local.host != ENET_HOST_ANY) {
        char buf[64] = {0};
        if (enet_address_get_host_ip(&local, buf, sizeof(buf)) == 0)
            result = buf;
    }
    enet_socket_destroy(s);
    return result;
}

// ---------------------------------------------------------------------------

GosNetSession::GosNetSession() { ++g_enetRefs; }

GosNetSession::~GosNetSession()
{
    shutdown();
    --g_enetRefs;
}

bool GosNetSession::host(uint16_t port, int maxPeers)
{
    shutdown();
    if (!enetUp())
        return false;
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    ENetHost* h = enet_host_create(&addr, (size_t)std::max(1, maxPeers), 2, 0, 0);
    if (!h)
        return false;
    h->intercept = interceptCallback;
    g_hosts.push_back(HostEntry{h, this});
    host_ = h;
    server_ = true;
    return true;
}

bool GosNetSession::join(const char* hostName, uint16_t port)
{
    shutdown();
    if (!enetUp() || !hostName)
        return false;
    ENetHost* h = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!h)
        return false;
    ENetAddress addr;
    if (enet_address_set_host(&addr, hostName) != 0) {
        enet_host_destroy(h);
        return false;
    }
    addr.port = port;
    ENetPeer* p = enet_host_connect(h, &addr, 2, 0);
    if (!p) {
        enet_host_destroy(h);
        return false;
    }
    enet_peer_timeout(p, 0, timeoutMs_ / 2, timeoutMs_);
    host_ = h;
    server_ = false;
    return true;
}

bool GosNetSession::poll(gosNetEvent* ev, uint32_t timeoutMs)
{
    ev->type = gosNetEv_None;
    ev->peer = -1;
    ev->data.clear();
    if (!host_)
        return false;
    ENetEvent e;
    int r = enet_host_service(H(host_), &e, timeoutMs);
    if (r <= 0)
        return false;
    ev->peer = e.peer ? (int)(e.peer - H(host_)->peers) : -1;
    switch (e.type) {
    case ENET_EVENT_TYPE_CONNECT:
        ev->type = gosNetEv_Connect;
        enet_peer_timeout(e.peer, 0, timeoutMs_ / 2, timeoutMs_);
        break;
    case ENET_EVENT_TYPE_DISCONNECT:
        ev->type = gosNetEv_Disconnect;
        break;
    case ENET_EVENT_TYPE_RECEIVE:
        ev->type = gosNetEv_Receive;
        ev->data.assign(e.packet->data, e.packet->data + e.packet->dataLength);
        enet_packet_destroy(e.packet);
        break;
    default:
        return false;
    }
    return true;
}

bool GosNetSession::send(int peer, const void* data, size_t len, gosNetChannel ch)
{
    if (!host_ || peer < 0 || peer >= (int)H(host_)->peerCount)
        return false;
    ENetPeer* p = &H(host_)->peers[peer];
    if (p->state != ENET_PEER_STATE_CONNECTED)
        return false;
    ENetPacket* pk = enet_packet_create(data, len,
        ch == gosNet_Reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!pk)
        return false;
    if (enet_peer_send(p, (enet_uint8)ch, pk) != 0) {
        enet_packet_destroy(pk);
        return false;
    }
    return true;
}

void GosNetSession::broadcast(const void* data, size_t len, gosNetChannel ch, int exceptPeer)
{
    if (!host_)
        return;
    for (int i = 0; i < (int)H(host_)->peerCount; ++i)
        if (i != exceptPeer && H(host_)->peers[i].state == ENET_PEER_STATE_CONNECTED)
            send(i, data, len, ch);
}

void GosNetSession::flush()
{
    if (host_)
        enet_host_flush(H(host_));
}

void GosNetSession::disconnect(int peer)
{
    if (!host_ || peer < 0 || peer >= (int)H(host_)->peerCount)
        return;
    enet_peer_disconnect_later(&H(host_)->peers[peer], 0);
}

void GosNetSession::shutdown()
{
    if (!host_)
        return;
    ENetHost* h = H(host_);
    bool any = false;
    for (size_t i = 0; i < h->peerCount; ++i)
        if (h->peers[i].state == ENET_PEER_STATE_CONNECTED) {
            enet_peer_disconnect(&h->peers[i], 0);
            any = true;
        }
    if (any) {
        // Give the disconnect notices a short chance to go out.
        ENetEvent e;
        for (int tries = 0; tries < 10 && enet_host_service(h, &e, 10) >= 0; ++tries)
            if (e.type == ENET_EVENT_TYPE_RECEIVE)
                enet_packet_destroy(e.packet);
        for (size_t i = 0; i < h->peerCount; ++i)
            enet_peer_reset(&h->peers[i]);
    }
    g_hosts.erase(std::remove_if(g_hosts.begin(), g_hosts.end(),
                      [h](const HostEntry& e) { return e.host == h; }),
        g_hosts.end());
    enet_host_destroy(h);
    host_ = nullptr;
    server_ = false;
}

bool GosNetSession::connected(int peer) const
{
    return host_ && peer >= 0 && peer < (int)H(host_)->peerCount
        && H(host_)->peers[peer].state == ENET_PEER_STATE_CONNECTED;
}

int GosNetSession::maxPeers() const { return host_ ? (int)H(host_)->peerCount : 0; }

int GosNetSession::connectedCount() const
{
    int n = 0;
    for (int i = 0; i < maxPeers(); ++i)
        n += connected(i) ? 1 : 0;
    return n;
}

std::string GosNetSession::peerAddress(int peer) const
{
    if (!host_ || peer < 0 || peer >= (int)H(host_)->peerCount)
        return std::string();
    char buf[64] = {0};
    enet_address_get_host_ip(&H(host_)->peers[peer].address, buf, sizeof(buf));
    return buf;
}

uint32_t GosNetSession::peerRoundTripMs(int peer) const
{
    if (!connected(peer))
        return 0;
    return H(host_)->peers[peer].roundTripTime;
}

void GosNetSession::setTimeout(uint32_t ms)
{
    timeoutMs_ = ms ? ms : 10000;
    if (!host_)
        return;
    for (size_t i = 0; i < H(host_)->peerCount; ++i)
        enet_peer_timeout(&H(host_)->peers[i], 0, timeoutMs_ / 2, timeoutMs_);
}

void GosNetSession::setDiscoveryPayload(const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    discovery_.assign(p, p + len);
}

int GosNetSession::handleRawPacket(void* hostPtr)
{
    ENetHost* h = H(hostPtr);
    if (h->receivedDataLength < 4 || memcmp(h->receivedData, gosNetQueryMagic, 4) != 0)
        return 0; // a regular ENet packet
    if (server_ && !discovery_.empty()) {
        std::vector<uint8_t> reply(gosNetAnswerMagic, gosNetAnswerMagic + 4);
        reply.insert(reply.end(), discovery_.begin(), discovery_.end());
        ENetBuffer buf;
        buf.data = reply.data();
        buf.dataLength = reply.size();
        enet_socket_send(h->socket, &h->receivedAddress, &buf, 1);
    }
    return 1;
}

// ---------------------------------------------------------------------------

GosNetScanner::GosNetScanner() { ++g_enetRefs; }

GosNetScanner::~GosNetScanner()
{
    close();
    --g_enetRefs;
}

bool GosNetScanner::open()
{
    if (socket_ != -1)
        return true;
    if (!enetUp())
        return false;
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL)
        return false;
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
    ENetAddress any;
    any.host = ENET_HOST_ANY;
    any.port = 0;
    if (enet_socket_bind(s, &any) != 0) {
        enet_socket_destroy(s);
        return false;
    }
    socket_ = (intptr_t)s;
    return true;
}

void GosNetScanner::close()
{
    if (socket_ != -1) {
        enet_socket_destroy((ENetSocket)socket_);
        socket_ = -1;
    }
}

void GosNetScanner::query(const char* address, uint16_t port)
{
    if (socket_ == -1 && !open())
        return;
    ENetBuffer buf;
    buf.data = (void*)gosNetQueryMagic;
    buf.dataLength = 4;
    ENetAddress to;
    to.port = port;
    if (address && *address) {
        if (enet_address_set_host(&to, address) == 0)
            enet_socket_send((ENetSocket)socket_, &to, &buf, 1);
        return;
    }
    to.host = ENET_HOST_BROADCAST;
    enet_socket_send((ENetSocket)socket_, &to, &buf, 1);
    // Hosts on this machine (useful for testing and when no broadcast route).
    enet_address_set_host_ip(&to, "127.0.0.1");
    enet_socket_send((ENetSocket)socket_, &to, &buf, 1);
}

bool GosNetScanner::poll(gosNetRoom* room)
{
    if (socket_ == -1)
        return false;
    uint8_t data[1500];
    for (;;) {
        ENetAddress from;
        ENetBuffer buf;
        buf.data = data;
        buf.dataLength = sizeof(data);
        int n = enet_socket_receive((ENetSocket)socket_, &from, &buf, 1);
        if (n <= 0)
            return false;
        if (n < 4 || memcmp(data, gosNetAnswerMagic, 4) != 0)
            continue;
        char ip[64] = {0};
        enet_address_get_host_ip(&from, ip, sizeof(ip));
        room->address = ip;
        room->port = from.port;
        room->payload.assign(data + 4, data + n);
        return true;
    }
}
