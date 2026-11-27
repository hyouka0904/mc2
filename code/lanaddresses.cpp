#include "lanaddresses.h"

#include "mclib.h"
#include "asystem.h"
#include "net/gos_net.h"
#include "userinput.h"

#include <algorithm>
#include <vector>

std::string LanAddressSummary()
{
    const std::vector<gosNetInterface> itfs = gos_NetLocalInterfaces();
    if (itfs.empty())
        return gos_NetLocalAddress();
    std::string text = itfs.front().address;
    if (itfs.size() > 1)
        text += " (+" + std::to_string(itfs.size() - 1) + ")";
    return text;
}

void RenderLanAddressList(aText& line)
{
    if (!line.isShowing() || !line.pointInside(userInput->getMouseX(), userInput->getMouseY()))
        return;
    const std::vector<gosNetInterface> itfs = gos_NetLocalInterfaces();
    if (itfs.size() < 2)
        return;

    aFont& font = line.font;
    const long padding = 4;
    const long gap = 12;
    long addressWidth = 0, nameWidth = 0;
    for (const gosNetInterface& itf : itfs) {
        addressWidth = std::max<long>(addressWidth, font.width(itf.address.c_str()));
        nameWidth = std::max<long>(nameWidth, font.width(itf.name.c_str()));
    }
    const long rowHeight = font.height() + 2;
    const long width = addressWidth + gap + nameWidth + 2 * padding;
    const long height = rowHeight * static_cast<long>(itfs.size()) + 2 * padding;

    // Above the line, or below when there is no room; inside the screen.
    long left = std::min<long>(line.globalX(), Environment.screenWidth - width - 2);
    long top = line.globalY() - height - 2;
    if (top < 0)
        top = line.globalY() + line.height() + 2;
    left = std::max<long>(left, 2);

    const GUI_RECT bounds = {left, top, left + width, top + height};
    drawRect(bounds, 0xf0000000);
    drawEmptyRect(bounds, 0xff1e94ff, 0xff1e94ff); // orange (BGR)
    long y = top + padding;
    for (size_t i = 0; i < itfs.size(); ++i, y += rowHeight) {
        // The default-route address comes first: brighter than the rest.
        const DWORD color = i == 0 ? 0xffffffff : 0xffb0b0b0;
        font.render(itfs[i].address.c_str(), left + padding, y, addressWidth + gap, rowHeight, color, false, 0);
        font.render(itfs[i].name.c_str(), left + padding + addressWidth + gap, y, nameWidth + 16, rowHeight,
                    color, false, 0);
    }
}
