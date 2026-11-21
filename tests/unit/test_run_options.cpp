// Tests for GameOS command line options, the PNG writer and the fixed-step
// clock used by the deterministic screenshot tests.

#include "mc2test.h"

#include "platform_clock.h"
#include "utils/png_writer.h"
#include "utils/run_options.h"

#include <cstring>
#include <vector>
#include <zlib.h>

using gos_run_options::Options;

static bool parseArgs(std::vector<const char*> args, Options& o, std::string* err = nullptr)
{
    args.insert(args.begin(), "mc2");
    return gos_run_options::parse((int)args.size(), args.data(), o, err);
}

TEST(run_options_defaults_are_real_time)
{
    Options o;
    CHECK(parseArgs({"-mission", "mc2_01"}, o));
    CHECK_EQ(o.fixed_step_us, 0u);
    CHECK(!o.has_seed);
    CHECK(o.capture_frames.empty());
    CHECK_EQ(o.exit_after_frame, 0u);
}

TEST(run_options_parses_deterministic_flags)
{
    Options o;
    CHECK(parseArgs({"-fixedstep", "33", "-seed", "42", "-capture-frames", "600,300,300,900",
                     "-capture-dir", "/tmp/x", "-mission", "mc2_01"}, o));
    CHECK_EQ(o.fixed_step_us, 33000u);
    CHECK(o.has_seed);
    CHECK_EQ(o.seed, 42u);
    REQUIRE(o.capture_frames.size() == 3); // sorted, duplicates removed
    CHECK_EQ(o.capture_frames[0], 300u);
    CHECK_EQ(o.capture_frames[2], 900u);
    CHECK(o.should_capture(600));
    CHECK(!o.should_capture(601));
    CHECK(o.capture_dir == "/tmp/x");
    CHECK_EQ(o.exit_after_frame, 900u); // defaults to the last capture
}

TEST(run_options_explicit_exit_wins)
{
    Options o;
    CHECK(parseArgs({"-capture-frames", "10", "-exit-after", "5"}, o));
    CHECK_EQ(o.exit_after_frame, 5u);
}

TEST(run_options_rejects_bad_values)
{
    const char* bad[][2] = {
        {"-fixedstep", "0"}, {"-fixedstep", "abc"}, {"-fixedstep", "5000"},
        {"-seed", "-1"}, {"-capture-frames", "1,,2"}, {"-capture-frames", "0"},
        {"-capture-frames", "12x"}, {"-exit-after", "0"},
    };
    for (auto& b : bad) {
        Options o;
        std::string err;
        CHECK(!parseArgs({b[0], b[1]}, o, &err));
        CHECK(err.find(b[0]) == 0);
    }
    Options o;
    CHECK(!parseArgs({"-seed"}, o)); // missing value
}

TEST(capture_file_name_is_zero_padded)
{
    CHECK(gos_run_options::capture_file_name(300) == "capture_000300.png");
}

static uint32_t be32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

TEST(png_writer_produces_valid_png)
{
    const int w = 3, h = 2;
    // bottom-up RGBA: row 0 in memory is the bottom row of the image
    const uint8_t rgba[] = {
        1, 2, 3, 255,   4, 5, 6, 255,   7, 8, 9, 255,
        10, 11, 12, 0,  13, 14, 15, 0,  16, 17, 18, 0,
    };
    std::vector<uint8_t> png;
    REQUIRE(png_writer::encode_rgb(rgba, w, h, w * 4, true, png));
    REQUIRE(png.size() > 8 + 25 + 12 + 12);
    CHECK(memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);

    // walk chunks, verify CRCs and collect IDAT
    size_t pos = 8;
    std::vector<uint8_t> idat;
    int chunks = 0;
    while (pos + 12 <= png.size()) {
        const uint32_t len = be32(&png[pos]);
        const uint8_t* type = &png[pos + 4];
        REQUIRE(pos + 12 + len <= png.size());
        uLong crc = crc32(crc32(0L, Z_NULL, 0), type, 4 + len);
        CHECK_EQ((uint32_t)crc, be32(&png[pos + 8 + len]));
        if (!memcmp(type, "IHDR", 4)) {
            CHECK_EQ(be32(type + 4), (uint32_t)w);
            CHECK_EQ(be32(type + 8), (uint32_t)h);
            CHECK_EQ((int)type[12], 8);
            CHECK_EQ((int)type[13], 2);
        }
        if (!memcmp(type, "IDAT", 4))
            idat.insert(idat.end(), type + 4, type + 4 + len);
        pos += 12 + len;
        ++chunks;
    }
    CHECK_EQ(chunks, 3);
    CHECK_EQ(pos, png.size());

    std::vector<uint8_t> raw((w * 3 + 1) * h);
    uLongf rawLen = raw.size();
    REQUIRE(uncompress(raw.data(), &rawLen, idat.data(), idat.size()) == Z_OK);
    CHECK_EQ((size_t)rawLen, raw.size());
    // first image row = last memory row (flipped), filter byte 0, alpha dropped
    const uint8_t expect[] = {0, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                              0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    CHECK(memcmp(raw.data(), expect, sizeof(expect)) == 0);
}

TEST(png_writer_rejects_bad_input)
{
    std::vector<uint8_t> png;
    uint8_t px[4] = {};
    CHECK(!png_writer::encode_rgb(nullptr, 1, 1, 4, false, png));
    CHECK(!png_writer::encode_rgb(px, 0, 1, 4, false, png));
    CHECK(!png_writer::encode_rgb(px, 2, 1, 4, false, png)); // stride too small
}

TEST(fixed_step_clock_only_moves_on_advance)
{
    platform_clock::set_fixed_step(33000);
    CHECK(platform_clock::is_fixed());
    const uint64_t t0 = platform_clock::now_ns();
    CHECK_EQ(platform_clock::now_ns(), t0);
    platform_clock::advance_frame();
    platform_clock::advance_frame();
    CHECK_EQ(platform_clock::now_ns() - t0, (uint64_t)66000000);
    CHECK(t0 > 0); // 0 means "never" to game code

    platform_clock::set_fixed_step(0);
    CHECK(!platform_clock::is_fixed());
    const uint64_t r0 = platform_clock::now_ns();
    platform_clock::advance_frame(); // no-op in real time
    CHECK(platform_clock::now_ns() >= r0);
}

// ---------------------------------------------------------------- input script
#include "utils/input_script.h"

using input_script::Event;
using input_script::Type;

TEST(input_script_parses_all_actions)
{
    std::vector<Event> ev;
    std::string err;
    REQUIRE(input_script::parse(
        "# comment line\n"
        "\n"
        "20 key Escape   # trailing comment\n"
        "10 move 512 384\n"
        "11 click left\n"
        "30 keydown Left Shift\n"
        "31 up right\n",
        ev, &err));
    REQUIRE(ev.size() == 7);
    // sorted by frame, click and key expand to down + up two frames later
    CHECK_EQ(ev[0].frame, 10u);
    CHECK(ev[0].type == Type::Move);
    CHECK_EQ(ev[0].x, 512);
    CHECK_EQ(ev[0].y, 384);
    CHECK(ev[1].type == Type::ButtonDown && ev[1].frame == 11 && ev[1].button == 0);
    CHECK(ev[2].type == Type::ButtonUp && ev[2].frame == 13);
    CHECK(ev[3].type == Type::KeyDown && ev[3].key == "Escape" && ev[3].frame == 20);
    CHECK(ev[4].type == Type::KeyUp && ev[4].frame == 22);
    CHECK(ev[5].key == "Left Shift");
    CHECK(ev[6].type == Type::ButtonUp && ev[6].button == 2);
}

TEST(input_script_reports_line_numbers)
{
    const char* bad[] = {"5 move 1\n", "x move 1 2\n", "0 move 1 2\n", "5 click elbow\n",
                         "5 jump\n", "5\n", "5 key\n", "5 move -1 2\n"};
    for (const char* text : bad) {
        std::vector<Event> ev;
        std::string err;
        CHECK(!input_script::parse(std::string("1 move 0 0\n") + text, ev, &err));
        CHECK(err.find("line 2:") == 0);
    }
}

TEST(input_script_player_hands_out_due_events_once)
{
    std::vector<Event> ev;
    REQUIRE(input_script::parse("5 move 1 1\n5 down left\n9 up left\n", ev, nullptr));
    input_script::Player p(ev);
    CHECK(p.take(4).empty());
    CHECK_EQ(p.take(5).size(), (size_t)2);
    CHECK(p.take(5).empty());
    CHECK_EQ(p.take(20).size(), (size_t)1); // late frames still deliver
    CHECK(p.finished());
}

TEST(run_options_windowed_flag)
{
    Options o;
    CHECK(!o.windowed);
    CHECK(parseArgs({"-windowed", "-mission", "x"}, o));
    CHECK(o.windowed);
}

TEST(run_options_perf_csv_flag)
{
    Options o;
    CHECK(parseArgs({"-perf-csv", "perf.csv"}, o));
    CHECK(o.perf_csv == "perf.csv");
    Options o2;
    CHECK(!parseArgs({"-perf-csv"}, o2));
}

TEST(run_options_input_script_flag)
{
    Options o;
    CHECK(parseArgs({"-input-script", "menu.txt"}, o));
    CHECK(o.input_script == "menu.txt");
    Options o2;
    CHECK(!parseArgs({"-input-script"}, o2));
}
