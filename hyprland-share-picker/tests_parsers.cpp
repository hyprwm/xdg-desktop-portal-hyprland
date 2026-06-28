// real assertion-based test for the picker parsers. compiled against the
// actual Parsers.cpp from the branch. exits non-zero on any failure.
#include "Parsers.hpp"
#include <cassert>
#include <iostream>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                                                                                                                                  \
    do {                                                                                                                                                                             \
        if (!(cond)) {                                                                                                                                                               \
            std::cerr << "FAIL line " << __LINE__ << ": " << #cond << "\n";                                                                                                          \
            ++failures;                                                                                                                                                              \
        }                                                                                                                                                                            \
    } while (0)

int main() {
    using namespace Picker;

    // ---- parseWindowList ----
    {
        auto w = parseWindowList("0[HC>]firefox[HT>]Mozilla Firefox[HE>]0x1[HA>]");
        CHECK(w.size() == 1);
        CHECK(w[0].id == 0);
        CHECK(w[0].clazz == "firefox");
        CHECK(w[0].title == "Mozilla Firefox");
    }
    {
        // two entries
        auto w = parseWindowList("0[HC>]firefox[HT>]FF[HE>]0x1[HA>]42[HC>]kitty[HT>]term[HE>]0x2[HA>]");
        CHECK(w.size() == 2);
        CHECK(w[0].id == 0);
        CHECK(w[0].clazz == "firefox");
        CHECK(w[1].id == 42);
        CHECK(w[1].clazz == "kitty");
        CHECK(w[1].title == "term");
    }
    {
        // empty input
        auto w = parseWindowList("");
        CHECK(w.empty());
    }
    {
        // malformed: no separators -> nothing parsed, no crash
        auto w = parseWindowList("garbage_no_separators");
        CHECK(w.empty());
    }
    {
        // non-numeric id -> entry skipped (stoull throws, caught)
        auto w = parseWindowList("abc[HC>]firefox[HT>]FF[HE>]0x1[HA>]");
        CHECK(w.empty());
    }
    {
        // partial separators (missing [HA>]) -> not parsed
        auto w = parseWindowList("1[HC>]firefox[HT>]FF[HE>]");
        CHECK(w.empty());
    }
    {
        // title containing spaces and colon
        auto w = parseWindowList("7[HC>]code[HT>]file.cpp: edited[HE>]0xabc[HA>]");
        CHECK(w.size() == 1);
        CHECK(w[0].title == "file.cpp: edited");
        CHECK(w[0].id == 7);
    }

    // ---- parseMonitors ----
    {
        const std::string OUT = "Monitor eDP-1 (ID 0):\n\t1920x1080@60.00000 at 0x0\n\tdescription: foo\n";
        auto              m   = parseMonitors(OUT);
        CHECK(m.size() == 1);
        CHECK(m[0].name == "eDP-1");
        CHECK(m[0].id == 0);
        CHECK(m[0].width == 1920);
        CHECK(m[0].height == 1080);
        CHECK(m[0].x == 0);
        CHECK(m[0].y == 0);
    }
    {
        // two monitors with offset
        const std::string OUT = "Monitor eDP-1 (ID 0):\n\t1920x1080@60.00000 at 0x0\nMonitor HDMI-A-1 (ID 1):\n\t2560x1440@144.00000 at 1920x0\n";
        auto              m   = parseMonitors(OUT);
        CHECK(m.size() == 2);
        CHECK(m[1].name == "HDMI-A-1");
        CHECK(m[1].id == 1);
        CHECK(m[1].width == 2560);
        CHECK(m[1].height == 1440);
        CHECK(m[1].x == 1920);
        CHECK(m[1].y == 0);
    }
    {
        auto m = parseMonitors("");
        CHECK(m.empty());
    }
    {
        // garbage that starts with "Monitor " but has no valid second line
        auto m = parseMonitors("Monitor weird (ID 5):\nnot a resolution line\n");
        CHECK(m.empty());
    }

    // ---- parseRegion ----
    {
        std::vector<SMonitorEntry> mons = {{.name = "eDP-1", .id = 0, .x = 0, .y = 0, .width = 1920, .height = 1080}};
        auto                       r    = parseRegion("eDP-1 100 200 300 400", mons);
        CHECK(r.has_value());
        CHECK(*r == "eDP-1@100,200,300,400");
    }
    {
        // monitor at an offset -> global coords converted to local
        std::vector<SMonitorEntry> mons = {{.name = "HDMI-A-1", .id = 1, .x = 1920, .y = 0, .width = 2560, .height = 1440}};
        auto                       r    = parseRegion("HDMI-A-1 2000 50 640 480", mons);
        CHECK(r.has_value());
        CHECK(*r == "HDMI-A-1@80,50,640,480"); // 2000-1920=80
    }
    {
        // empty slurp output -> nullopt (user cancelled)
        std::vector<SMonitorEntry> mons = {{.name = "eDP-1", .id = 0, .x = 0, .y = 0, .width = 1920, .height = 1080}};
        auto                       r    = parseRegion("", mons);
        CHECK(!r.has_value());
    }
    {
        // monitor name not in list -> nullopt
        std::vector<SMonitorEntry> mons = {{.name = "eDP-1", .id = 0, .x = 0, .y = 0, .width = 1920, .height = 1080}};
        auto                       r    = parseRegion("DP-9 0 0 100 100", mons);
        CHECK(!r.has_value());
    }
    {
        // trailing newline from slurp is stripped
        std::vector<SMonitorEntry> mons = {{.name = "eDP-1", .id = 0, .x = 0, .y = 0, .width = 1920, .height = 1080}};
        auto                       r    = parseRegion("eDP-1 10 20 30 40\n", mons);
        CHECK(r.has_value());
        CHECK(*r == "eDP-1@10,20,30,40");
    }

    if (failures == 0) {
        std::cout << "all parser assertions passed\n";
        return 0;
    }
    std::cerr << failures << " assertion(s) failed\n";
    return 1;
}
