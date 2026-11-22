// SPDX-License-Identifier: GPL-3.0-only
#include "input_script.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace input_script {

namespace {

bool to_int(const std::string& s, long min_v, long max_v, long& out)
{
    if (s.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (errno || *end || v < min_v || v > max_v)
        return false;
    out = v;
    return true;
}

int button_index(const std::string& name)
{
    if (name == "left") return 0;
    if (name == "middle") return 1;
    if (name == "right") return 2;
    return -1;
}

} // namespace

bool parse(const std::string& text, std::vector<Event>& out, std::string* error)
{
    std::istringstream in(text);
    std::string line;
    int line_no = 0;
    std::vector<Event> events;

    auto fail = [&](const std::string& why) {
        if (error)
            *error = "line " + std::to_string(line_no) + ": " + why;
        return false;
    };

    while (std::getline(in, line)) {
        ++line_no;
        const size_t hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream ls(line);
        std::vector<std::string> tok;
        for (std::string t; ls >> t;)
            tok.push_back(t);
        if (tok.empty())
            continue;
        if (tok.size() < 2)
            return fail("expected '<frame> <action> ...'");

        long frame = 0;
        if (!to_int(tok[0], 1, 0x7FFFFFFF, frame))
            return fail("bad frame number '" + tok[0] + "'");
        const std::string& action = tok[1];
        Event e;
        e.frame = (uint32_t)frame;

        if (action == "move") {
            long x, y;
            if (tok.size() != 4 || !to_int(tok[2], 0, 65535, x) || !to_int(tok[3], 0, 65535, y))
                return fail("expected 'move <x> <y>'");
            e.type = Type::Move;
            e.x = (int)x;
            e.y = (int)y;
            events.push_back(e);
        } else if (action == "down" || action == "up" || action == "click") {
            if (tok.size() != 3 || (e.button = button_index(tok[2])) < 0)
                return fail("expected '" + action + " left|middle|right'");
            e.type = action == "up" ? Type::ButtonUp : Type::ButtonDown;
            events.push_back(e);
            if (action == "click") {
                e.type = Type::ButtonUp;
                e.frame += 2;
                events.push_back(e);
            }
        } else if (action == "keydown" || action == "keyup" || action == "key") {
            if (tok.size() < 3)
                return fail("expected '" + action + " <key name>'");
            // SDL key names may contain spaces ("Left Shift")
            e.key = tok[2];
            for (size_t i = 3; i < tok.size(); ++i)
                e.key += " " + tok[i];
            e.type = action == "keyup" ? Type::KeyUp : Type::KeyDown;
            events.push_back(e);
            if (action == "key") {
                e.type = Type::KeyUp;
                e.frame += 2;
                events.push_back(e);
            }
        } else {
            return fail("unknown action '" + action + "'");
        }
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.frame < b.frame; });
    out.swap(events);
    return true;
}

bool load_file(const std::string& path, std::vector<Event>& out, std::string* error)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        if (error)
            *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    if (!parse(ss.str(), out, error)) {
        if (error)
            *error = path + ": " + *error;
        return false;
    }
    return true;
}

std::vector<Event> Player::take(uint32_t frame)
{
    std::vector<Event> due;
    while (next_ < events_.size() && events_[next_].frame <= frame)
        due.push_back(events_[next_++]);
    return due;
}

} // namespace input_script
