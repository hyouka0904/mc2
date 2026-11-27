#ifndef LANADDRESSES_H
#define LANADDRESSES_H
// This machine's LAN addresses in the multiplayer screens. A computer with
// VPN, virtual or container adapters has many; the line shows only the one
// used for the default route and how many more there are, and hovering the
// line lists every address with its adapter name.

#include <string>

class aText;

// "192.168.1.23 (+2)", or the only address.
std::string LanAddressSummary();

// While the mouse is over `line`, draws the full list next to it in the
// line's font. Call after the screen's other rendering.
void RenderLanAddressList(aText& line);

#endif
