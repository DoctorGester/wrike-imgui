#include "folder_tree.h"

#pragma once

enum Rich_Text_Flags {
    Rich_Text_Flags_None    = 0,
    Rich_Text_Flags_Bold    = 1 << 0,
    Rich_Text_Flags_Italic  = 1 << 1,
    Rich_Text_Flags_Crossed = 1 << 2
};

struct Rich_Text_Style {
    String link{};
    u32 background_color = 0x00000000;
    u32 flags = Rich_Text_Flags_None;
    u32 list_depth = 0;
};

struct Rich_Text_String {
    String text{};
    Rich_Text_Style style;
};

void parse_rich_text(String& text, Rich_Text_String*& output, u32& output_size);
void destructively_strip_html_comments(String& source);