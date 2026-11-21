#include "utils/kv_registry.h"

#include <stdio.h>
#include <string.h>

namespace kv_registry {

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isValidKey(const std::string& key)
{
    return !key.empty() && key.find_first_of("=\r\n") == std::string::npos;
}

Store parse(const std::string& text)
{
    Store store;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;

        const std::string hex = line.substr(eq + 1);
        if (hex.size() % 2 != 0)
            continue;

        Value value;
        value.reserve(hex.size() / 2);
        bool ok = true;
        for (size_t i = 0; i < hex.size(); i += 2) {
            const int hi = hexDigit(hex[i]);
            const int lo = hexDigit(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                ok = false;
                break;
            }
            value.push_back((uint8_t)((hi << 4) | lo));
        }
        if (ok)
            store[line.substr(0, eq)] = value;
    }
    return store;
}

std::string format(const Store& store)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (Store::const_iterator it = store.begin(); it != store.end(); ++it) {
        if (!isValidKey(it->first))
            continue;
        out += it->first;
        out += '=';
        for (size_t i = 0; i < it->second.size(); ++i) {
            out += digits[it->second[i] >> 4];
            out += digits[it->second[i] & 0xF];
        }
        out += '\n';
    }
    return out;
}

void load(const Store& store, const std::string& key, void* data, uint32_t* size)
{
    Store::const_iterator it = store.find(key);
    if (it == store.end() || it->second.size() > *size) {
        *size = 0;
        return;
    }
    if (!it->second.empty())
        memcpy(data, it->second.data(), it->second.size());
    *size = (uint32_t)it->second.size();
}

Store readFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return Store();

    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    return parse(text);
}

bool writeFile(const std::string& path, const Store& store)
{
    // Write to a temporary file and rename, so a crash mid-write never leaves
    // a truncated registry behind.
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;

    const std::string text = format(store);
    const bool written = fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = fclose(f) == 0;
    if (!written || !closed) {
        remove(tmp.c_str());
        return false;
    }
#ifdef PLATFORM_WINDOWS
    remove(path.c_str()); // rename() does not overwrite on Windows
#endif
    return rename(tmp.c_str(), path.c_str()) == 0;
}

} // namespace kv_registry
