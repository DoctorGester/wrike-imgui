#include "folder_tree.h"

#pragma once

enum Rich_Text_Flags {
    Rich_Text_Flags_None    = 0,
    Rich_Text_Flags_Bold    = 1 << 0,
    Rich_Text_Flags_Italic  = 1 << 1,
    Rich_Text_Flags_Crossed = 1 << 2,
    Rich_Text_Flags_Link    = 1 << 3
};

struct Rich_Text_Style {
    u32 link_start;
    u32 link_end;
    u32 background_color = 0x00000000;
    u32 flags = Rich_Text_Flags_None;
    u32 list_depth = 0;
};

struct Rich_Text_String {
    u32 start;
    u32 end;
    Rich_Text_Style style;
};

struct Rich_Text {
    Array<Rich_Text_String> rich{};
    String raw{};
};

Rich_Text parse_string_into_temporary_rich_text(String text);
void destructively_strip_html_comments(String& source);