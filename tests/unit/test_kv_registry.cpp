// SPDX-License-Identifier: GPL-3.0-only
// Tests for the registry emulation used by gos_Load/SaveDataToRegistry.

#include "mc2test.h"

#include "utils/kv_registry.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <filesystem>

using kv_registry::Store;
using kv_registry::Value;

static Value bytes(const char* s, size_t n) { return Value(s, s + n); }

TEST(registry_format_then_parse_roundtrips)
{
    Store s;
    s["PID"] = bytes("12345-OEM", 9);
    s["Binary"] = bytes("\x00\xff\x10\x7f", 4);
    s["Empty"] = Value();

    const std::string text = kv_registry::format(s);
    CHECK(kv_registry::parse(text) == s);
}

TEST(registry_format_is_readable_hex)
{
    Store s;
    s["k"] = bytes("\x01\xab", 2);
    CHECK_EQ(kv_registry::format(s), std::string("k=01ab\n"));
}

TEST(registry_parse_skips_malformed_lines)
{
    const Store s = kv_registry::parse(
        "good=4142\r\n"   // CRLF tolerated
        "noequals\n"
        "=00\n"           // empty key
        "odd=123\n"       // odd number of hex digits
        "bad=zz\n"        // not hex
        "upper=FF\n");
    CHECK_EQ(s.size(), (size_t)2);
    CHECK(s.at("good") == bytes("AB", 2));
    CHECK(s.at("upper") == bytes("\xff", 1));
}

TEST(registry_invalid_keys_are_not_written)
{
    CHECK(kv_registry::isValidKey("GraphicsData"));
    CHECK(!kv_registry::isValidKey(""));
    CHECK(!kv_registry::isValidKey("a=b"));
    CHECK(!kv_registry::isValidKey("line\nbreak"));

    Store s;
    s["a=b"] = bytes("x", 1);
    CHECK_EQ(kv_registry::format(s), std::string(""));
}

TEST(registry_load_follows_gos_size_contract)
{
    Store s;
    s["PID"] = bytes("ABCD", 4);

    char buf[8] = {0};
    uint32_t size = sizeof(buf);
    kv_registry::load(s, "PID", buf, &size);
    CHECK_EQ(size, (uint32_t)4);
    CHECK_EQ(std::string(buf, 4), std::string("ABCD"));

    // missing key -> size 0, buffer untouched
    char untouched[4] = {'x', 'x', 'x', 'x'};
    size = sizeof(untouched);
    kv_registry::load(s, "Nope", untouched, &size);
    CHECK_EQ(size, (uint32_t)0);
    CHECK_EQ(untouched[0], 'x');

    // value larger than the buffer -> size 0, no overflow
    char small[2] = {0, 0};
    size = sizeof(small);
    kv_registry::load(s, "PID", small, &size);
    CHECK_EQ(size, (uint32_t)0);
}

TEST(registry_file_roundtrip)
{
    namespace fs = std::filesystem;
    const std::string path = (fs::temp_directory_path() / "mc2_registry_roundtrip").string();
    fs::remove(path);

    Store s;
    s["PID"] = bytes("OEM-42", 6);
    CHECK(kv_registry::writeFile(path, s));
    CHECK(kv_registry::readFile(path) == s);

    // overwrite keeps only the new contents and leaves no temp file behind
    s["Other"] = bytes("\x01", 1);
    CHECK(kv_registry::writeFile(path, s));
    CHECK(kv_registry::readFile(path) == s);
    CHECK(!std::filesystem::exists(path + ".tmp"));

    remove(path.c_str());
    CHECK(kv_registry::readFile(path).empty()); // missing file reads as empty
}
