#include "PickerData.hpp"

#include <iostream>

static bool expect(bool condition, const char* message) {
    if (condition)
        return true;

    std::cerr << message << '\n';
    return false;
}

static bool testWorkspaceListV3() {
    bool              success    = true;
    const std::string NUMBERED   = "42[HN>]1:1[HM>]1:4:DP-3[HE>]";
    const std::string NAMED      = "44[HN>]3:dev[HM>]2:4:DP-18:HDMI-A-1[HE>]";
    const auto        WORKSPACES = parseWorkspaceListV3(NUMBERED + "43[HN>]3:dev[HM>]0:[HE>]" + NAMED);
    success &= expect(WORKSPACES.size() == 3, "V3 workspace parser rejected numbered, named, or concatenated records");
    if (WORKSPACES.size() == 3) {
        success &= expect(WORKSPACES[0].id == 42 && WORKSPACES[0].name == "1" &&
                              WORKSPACES[0].outputs ==
                                  std::vector<std::string>{
                                      "DP-3",
                                  },
                          "V3 workspace parser mixed up numbered workspace fields");
        success &= expect(WORKSPACES[1].id == 43 && WORKSPACES[1].name == "dev" && WORKSPACES[1].outputs.empty(), "V3 workspace parser rejected a workspace without outputs");
        success &= expect(WORKSPACES[2].id == 44 && WORKSPACES[2].name == WORKSPACES[1].name &&
                              WORKSPACES[2].outputs ==
                                  std::vector<std::string>{
                                      "DP-1",
                                      "HDMI-A-1",
                                  },
                          "V3 workspace parser confused duplicate names or multiple outputs");
        success &= expect(workspaceLabel(WORKSPACES[0]) == "Workspace 1 on monitor DP-3", "numbered workspace label is incorrect");
        success &= expect(workspaceLabel(WORKSPACES[1]) == "Workspace dev", "workspace label without outputs is incorrect");
        success &= expect(workspaceLabel(WORKSPACES[2]) == "Workspace dev on monitors DP-1, HDMI-A-1", "multi-output workspace label is incorrect");
        for (const auto& workspace : WORKSPACES) {
            const SSelection SELECTION{
                .type        = SELECTION_WORKSPACE,
                .workspaceID = workspace.id,
            };
            success &= expect(formatSelection(SELECTION, false) == "[SELECTION]/workspace-id:" + std::to_string(workspace.id) + "\n",
                              "V3 workspace selection did not serialize only its tracker id");
        }
    }

    const auto DUPLICATES = parseWorkspaceListV3(NUMBERED + "45[HN>]1:1[HM>]1:4:DP-4[HE>]");
    success &= expect(DUPLICATES.size() == 2 && DUPLICATES[0].id == 42 && DUPLICATES[1].id == 45 && DUPLICATES[0].name == DUPLICATES[1].name &&
                          workspaceLabel(DUPLICATES[1]) == "Workspace 1 on monitor DP-4",
                      "V3 workspace parser lost duplicate numbered names on different monitors");

    const auto LABELS = parseWorkspaceListV3("46[HN>]1:2[HM>]0:[HE>]47[HN>]4:code[HM>]1:4:DP-2[HE>]48[HN>]1:3[HM>]3:4:DP-14:DP-24:DP-3[HE>]");
    success &= expect(LABELS.size() == 3, "V3 workspace parser rejected additional numbered or named records");
    if (LABELS.size() == 3) {
        success &= expect(workspaceLabel(LABELS[0]) == "Workspace 2", "numbered workspace label without outputs is incorrect");
        success &= expect(workspaceLabel(LABELS[1]) == "Workspace code on monitor DP-2", "named single-output workspace label is incorrect");
        success &= expect(workspaceLabel(LABELS[2]) == "Workspace 3 on monitors DP-1, DP-2, DP-3", "numbered workspace label with three outputs is incorrect");
    }

    const std::string NAME   = "dev[HN>][HM>][HE>]:;\n\r\xc3\xa9[SELECTION]/workspace-id:99";
    const std::string OUTPUT = "DP[HE>][HM>][HN>]:;\n\xe6\x97\xa5";
    const auto SPECIAL = parseWorkspaceListV3("18446744073709551615[HN>]" + std::to_string(NAME.size()) + ":" + NAME + "[HM>]2:" + std::to_string(OUTPUT.size()) + ":" + OUTPUT +
                                              "4:DP-3[HE>]" + NUMBERED);
    success &= expect(SPECIAL.size() == 2, "V3 workspace parser treated framed delimiters as record boundaries");
    if (SPECIAL.size() == 2) {
        success &= expect(SPECIAL[0].id == UINT64_MAX && SPECIAL[0].name == NAME &&
                              SPECIAL[0].outputs ==
                                  std::vector<std::string>{
                                      OUTPUT,
                                      "DP-3",
                                  },
                          "V3 workspace parser corrupted delimiters, UTF8, newlines, or a uint64 id");
        success &= expect(SPECIAL[1].id == 42, "V3 workspace parser lost the record after framed delimiter strings");
        success &= expect(workspaceLabel(SPECIAL[0]) == "Workspace " + NAME + " on monitors " + OUTPUT + ", DP-3", "workspace label altered original text");
        const SSelection SELECTION{
            .type        = SELECTION_WORKSPACE,
            .workspaceID = SPECIAL[0].id,
        };
        success &=
            expect(formatSelection(SELECTION, true) == "[SELECTION]r/workspace-id:18446744073709551615\n", "V3 workspace selection leaked display text or lost its restore flag");
        success &=
            expect(formatSelection(SELECTION, false) == "[SELECTION]/workspace-id:18446744073709551615\n", "V3 workspace selection leaked display text or added a restore flag");
    }

    const auto EMPTY_FIELDS = parseWorkspaceListV3("1[HN>]0:[HM>]2:0:0:[HE>]");
    success &= expect(EMPTY_FIELDS.size() == 1 && EMPTY_FIELDS[0].id == 1 && EMPTY_FIELDS[0].name.empty() &&
                          EMPTY_FIELDS[0].outputs ==
                              std::vector<std::string>{
                                  "",
                                  "",
                              },
                      "V3 workspace parser rejected zero byte lengths");
    success &= expect(parseWorkspaceListV3("").empty(), "V3 workspace parser rejected an empty list");

    for (const auto INVALID : {
             "",
             "-1",
             "+1",
             " 1",
             "1 ",
             "1x",
             "1.0",
             "\n1",
         }) {
        success &= expect(parseWorkspaceListV3(std::string{INVALID} + "[HN>]1:1[HM>]0:[HE>]").empty(), "V3 workspace parser accepted an invalid id");
        success &= expect(parseWorkspaceListV3("1[HN>]" + std::string{INVALID} + ":1[HM>]0:[HE>]").empty(), "V3 workspace parser accepted an invalid name length");
        success &= expect(parseWorkspaceListV3("1[HN>]1:1[HM>]" + std::string{INVALID} + ":[HE>]").empty(), "V3 workspace parser accepted an invalid output count");
        success &= expect(parseWorkspaceListV3("1[HN>]1:1[HM>]1:" + std::string{INVALID} + ":x[HE>]").empty(), "V3 workspace parser accepted an invalid output length");
    }
    for (const auto INVALID : {
             "0",
             "18446744073709551616",
             "999999999999999999999999999999999999999",
         }) {
        success &= expect(parseWorkspaceListV3(std::string{INVALID} + "[HN>]1:1[HM>]0:[HE>]").empty(), "V3 workspace parser accepted a zero or overflowing id");
    }
    for (const auto INVALID : {
             "4294967295",
             "4294967296",
             "18446744073709551616",
         }) {
        success &= expect(parseWorkspaceListV3("1[HN>]" + std::string{INVALID} + ":1[HM>]0:[HE>]").empty(), "V3 workspace parser accepted an oversized name length");
        success &= expect(parseWorkspaceListV3("1[HN>]1:1[HM>]" + std::string{INVALID} + ":[HE>]").empty(), "V3 workspace parser accepted an oversized output count");
        success &= expect(parseWorkspaceListV3("1[HN>]1:1[HM>]1:" + std::string{INVALID} + ":x[HE>]").empty(), "V3 workspace parser accepted an oversized output length");
    }
    for (const auto MALFORMED : {
             "1[HN>1:1[HM>]0:[HE>]",
             "1[HN>]1:1[HM>0:[HE>]",
             "1[HN>]2:1[HM>]0:[HE>]",
             "1[HN>]0:1[HM>]0:[HE>]",
             "1[HN>]1:1[HM>]0[HE>]",
             "1[HN>]1:1[HM>]0:4:DP-3[HE>]",
             "1[HN>]1:1[HM>]1:[HE>]",
             "1[HN>]1:1[HM>]2:4:DP-3[HE>]",
             "1[HN>]1:1[HM>]1:3:DP-3[HE>]",
             "1[HN>]1:1[HM>]1:5:DP-3[HE>]",
             "1[HN>]1:1[HM>]0:[HE> ",
             "1[HN>]1:1[HM>]0:[HM>]",
             "1[HN>]1:1[HM>]0: [HE>]",
         }) {
        success &= expect(parseWorkspaceListV3(MALFORMED).empty(), "V3 workspace parser accepted malformed framing or a terminator");
        const auto PREFIX = parseWorkspaceListV3(NUMBERED + MALFORMED + NAMED);
        success &= expect(PREFIX.size() == 1 && PREFIX[0].id == 42, "V3 workspace parser lost valid records or continued past malformed framing");
    }
    for (size_t i = 0; i < NAMED.size(); ++i) {
        success &= expect(parseWorkspaceListV3(std::string_view{NAMED}.substr(0, i)).empty(), "V3 workspace parser accepted a truncated record");
    }

    const SWorkspaceEntry MARKUP{
        .id   = 1,
        .name = "<dev>&\"'\n\xc3\xa9",
        .outputs =
            {
                "DP<&>\"'\r",
                "HDMI&2",
            },
    };
    success &= expect(escapeMarkup(workspaceLabel(MARKUP)) == "Workspace &lt;dev&gt;&amp;&quot;&apos; \xc3\xa9 on monitors DP&lt;&amp;&gt;&quot;&apos; , HDMI&amp;2",
                      "workspace label did not escape both its name and outputs exactly once");
    return success;
}

int main() {
    bool success = true;

    success &= testWorkspaceListV3();

    const auto WINDOWS = parseWindowList("4294967295[HC>]kitty[HT>]Terminal[HE>]123[HA>]broken[HC>]");
    success &= expect(WINDOWS.size() == 1, "window parser accepted a malformed record");
    if (!WINDOWS.empty()) {
        success &= expect(WINDOWS[0].id == UINT32_MAX, "window parser lost a uint32 id");
        success &= expect(WINDOWS[0].windowClass == "kitty" && WINDOWS[0].title == "Terminal", "window parser mixed up fields");
    }

    const auto OUTPUTS = parseOutputList("4:DP-1:-1920:0:1920:1080;6:HDMI-1:0:0:2560:1440;");
    success &= expect(OUTPUTS.size() == 2, "output parser rejected valid records");
    if (!OUTPUTS.empty())
        success &= expect(OUTPUTS[0].name == "DP-1" && OUTPUTS[0].x == -1920 && OUTPUTS[0].width == 1920, "output parser produced incorrect geometry");
    success &= expect(parseOutputList("20:DP-1:0:0:1:1;").empty(), "output parser accepted a truncated name");

    const std::string WORKSPACE_LABEL = "dev:[HN>];\n\r[SELECTION]/workspace:99/\xc3\xa9";
    const auto        WORKSPACES      = parseWorkspaceList(std::to_string(WORKSPACE_LABEL.size()) + ":" + WORKSPACE_LABEL + ":18446744073709551615;4:code:1;4:code:2;");
    success &= expect(WORKSPACES.size() == 3, "workspace parser rejected length-prefixed labels or duplicate names");
    if (WORKSPACES.size() == 3) {
        success &= expect(WORKSPACES[0].name == WORKSPACE_LABEL && WORKSPACES[0].id == UINT64_MAX, "workspace parser corrupted a label or uint64 id");
        success &= expect(WORKSPACES[1].name == WORKSPACES[2].name && WORKSPACES[1].id != WORKSPACES[2].id, "workspace parser confused identity with display name");
        const SSelection WORKSPACE_SELECTION{
            .type        = SELECTION_WORKSPACE,
            .workspaceID = WORKSPACES[0].id,
        };
        success &= expect(formatSelection(WORKSPACE_SELECTION, true) == "[SELECTION]r/workspace-id:18446744073709551615\n", "workspace selection did not serialize only its id");
        success &= expect(formatSelection(WORKSPACE_SELECTION, false) == "[SELECTION]/workspace-id:18446744073709551615\n", "workspace selection added an unwanted restore flag");
    }
    for (const auto MALFORMED :
         {"20:dev:1;", "3:dev1;", "3:dev:1", "-1:dev:1;", "4294967296:dev:1;", "3:dev:;", "3:dev:0;", "3:dev:-1;", "3:dev:1x;", "3:dev:18446744073709551616;"})
        success &= expect(parseWorkspaceList(MALFORMED).empty(), "workspace parser accepted a malformed record");
    const auto EMPTY_LABEL = parseWorkspaceList("0::3;");
    success &= expect(EMPTY_LABEL.size() == 1 && EMPTY_LABEL[0].name.empty() && EMPTY_LABEL[0].id == 3, "workspace parser used an empty label as identity");
    success &= expect(formatSelection(SSelection{.type = SELECTION_WORKSPACE}, false).empty(), "workspace formatter accepted an unset id");

    const auto REGION = parseRegion("DP-1 20 20 800 600\n", OUTPUTS);
    success &= expect(REGION.has_value(), "region parser rejected a valid region");
    success &= expect(REGION && REGION->x == 20 && REGION->y == 20 && REGION->width == 800 && REGION->height == 600, "region parser converted coordinates incorrectly");
    success &= expect(!parseRegion("DP-1 20 20 -1 600", OUTPUTS), "region parser accepted a negative width");
    success &= expect(!parseRegion("unknown 0 0 10 10", OUTPUTS), "region parser accepted an unknown output");

    const SSelection WINDOW_SELECTION{
        .type     = SELECTION_WINDOW,
        .output   = "",
        .windowID = UINT32_MAX,
    };
    success &= expect(formatSelection(WINDOW_SELECTION, true) == "[SELECTION]r/window:4294967295\n", "window selection was serialized incorrectly");
    success &= expect(escapeMarkup("<&>'\"\n") == "&lt;&amp;&gt;&apos;&quot; ", "markup escaping is incomplete");

    return success ? 0 : 1;
}
