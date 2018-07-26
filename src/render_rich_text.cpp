#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include "render_rich_text.h"
#include "rich_text.h"
#include "renderer.h"
#include "ui.h"
#include "platform.h"

using namespace ImGui;

static inline ImFont* get_font_from_style(Rich_Text_Style& style) {
    u32 is_bold = style.flags & Rich_Text_Flags_Bold;
    u32 is_italic = style.flags & Rich_Text_Flags_Italic;

    if (is_bold && is_italic) {
        return font_bold_italic;
    } else if (is_bold) {
        return font_bold;
    } else if (is_italic) {
        return font_italic;
    }

    return font_regular;
}

void render_rich_text(
        ImDrawList* draw_list,
        float size,
        ImVec2 pos,
        const ImVec4& clip_rect,
        Rich_Text& text,
        float wrap_width,
        float alpha,
        bool cpu_fine_clip
) {
    static const u32 default_color = 0xFF000000;

    Rich_Text_String* rich_text_begin = &text.rich[0];
    Rich_Text_String* rich_text_end = &ARRAY_LAST(text.rich);

    char* text_begin = text.raw.start + rich_text_begin->start;
    char* text_end   = text.raw.start + rich_text_end->end;

    Rich_Text_String* current_string = rich_text_begin;
    ImFont* current_font = get_font_from_style(rich_text_begin->style);
    u32 alpha_col = ((u32) (alpha * 255.0f)) << IM_COL32_A_SHIFT;
    u32 bg_color = current_string->style.background_color;
    u32 fg_color = default_color;

    bool has_bg_color = bg_color != 0;
    bool is_a_link = (current_string->style.flags & Rich_Text_Flags_Link) != 0;

    if (has_bg_color) {
        fg_color = 0xFFFFFFFF;
    } if (is_a_link) {
        fg_color = color_link;
    }

    fg_color = (fg_color & ~IM_COL32_A_MASK) | alpha_col;
    bg_color = (bg_color & ~IM_COL32_A_MASK) | alpha_col; // Set alpha

    // Align to be pixel perfect
    pos.x = (float)(int)pos.x + current_font->DisplayOffset.x;
    pos.y = (float)(int)pos.y + current_font->DisplayOffset.y;
    float x = pos.x;
    float y = pos.y;
    if (y > clip_rect.w)
        return;

    PushFont(current_font);

    const float scale = size / current_font->FontSize;
    const float line_height = current_font->FontSize * scale;
    const bool word_wrap_enabled = (wrap_width > 0.0f);
    const char* word_wrap_eol = NULL;

    // Skip non-visible lines
    const char* s = text_begin;
    if (!word_wrap_enabled && y + line_height < clip_rect.y)
        while (s < text_end && *s != '\n')  // Fast-forward to next line
            s++;

    u32 highlighted_strings = 0;
    const Rich_Text_String* rs = rich_text_begin;

    /* TODO this is not correct for multiline strings and word wrapping,
     * and I'm frankly not sure how to solve this. Every newline adds 1 more highlighted
     * string and we need to account for this everywhere
     */
    while (rs < rich_text_end) {
        if (rs->style.background_color) {
            highlighted_strings++;
        }

        rs++;
    }

    // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
    const int vtx_count_max = (int)(text_end - s + highlighted_strings) * 4;
    const int idx_count_max = (int)(text_end - s + highlighted_strings) * 6;
    const int idx_expected_size = draw_list->IdxBuffer.Size + idx_count_max;
    draw_list->PrimReserve(idx_count_max, vtx_count_max);

    ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
    unsigned int vtx_current_idx = draw_list->_VtxCurrentIdx;

    ImVec2 uv_white(draw_list->_Data->TexUvWhitePixel);

    ImVec2 string_top_left{ x, y };
    ImDrawVert* bg_color_vtx_write;
    ImDrawIdx* bg_color_idx_write;
    unsigned int bg_color_vtx_current_idx;

    if (has_bg_color) {
        // TODO copypaste from below
        bg_color_vtx_write = vtx_write;
        bg_color_idx_write = idx_write;
        bg_color_vtx_current_idx = vtx_current_idx;
        vtx_write += 4;
        vtx_current_idx += 4;
        idx_write += 6;
    }

    auto fill_bg_rect = [&] {
        ImVec2& a = string_top_left;
        ImVec2 c = ImVec2(x, y + line_height);
        ImVec2 b(c.x, a.y), d(a.x, c.y);

        ImDrawIdx idx = (ImDrawIdx)bg_color_vtx_current_idx;
        bg_color_idx_write[0] = idx; bg_color_idx_write[1] = (ImDrawIdx)(idx+1); bg_color_idx_write[2] = (ImDrawIdx)(idx+2);
        bg_color_idx_write[3] = idx; bg_color_idx_write[4] = (ImDrawIdx)(idx+2); bg_color_idx_write[5] = (ImDrawIdx)(idx+3);
        bg_color_vtx_write[0].pos = a; bg_color_vtx_write[0].uv = uv_white; bg_color_vtx_write[0].col = bg_color;
        bg_color_vtx_write[1].pos = b; bg_color_vtx_write[1].uv = uv_white; bg_color_vtx_write[1].col = bg_color;
        bg_color_vtx_write[2].pos = c; bg_color_vtx_write[2].uv = uv_white; bg_color_vtx_write[2].col = bg_color;
        bg_color_vtx_write[3].pos = d; bg_color_vtx_write[3].uv = uv_white; bg_color_vtx_write[3].col = bg_color;
    };

    auto link_button = [&] (Rich_Text_String* link_string) {
        ImVec2 bottom_right = { x, y + line_height };
        ImVec2 link_size = bottom_right - string_top_left;

        String link_url;
        link_url.start = text.raw.start + link_string->style.link_start;
        link_url.length = link_string->style.link_end - link_string->style.link_start;

        String link_text;
        link_text.start = text.raw.start + link_string->start;
        link_text.length = link_string->end - link_string->start;

        Button_State state = button((void*) link_url.start, string_top_left, link_size);

        if (state.pressed) {
            platform_open_url(link_url.length ? link_url : link_text);
        }
    };

    while (s < text_end) {
        if (s >= text.raw.start + current_string->end) {
            Rich_Text_String* previous_string = current_string;

            current_string++;
            ImFont* new_font = get_font_from_style(current_string->style);

            bool had_bg_color = has_bg_color;
            bool was_a_link = is_a_link;

            if (had_bg_color) {
                fill_bg_rect();
            }

            if (was_a_link) {
                link_button(previous_string);
            }

            bg_color = current_string->style.background_color;
            is_a_link = (current_string->style.flags & Rich_Text_Flags_Link) != 0;
            has_bg_color = bg_color != 0;

            if (has_bg_color) {
                fg_color = 0xFFFFFFFF;
            } else if (is_a_link) {
                fg_color = color_link;
            } else {
                fg_color = default_color;
            }

            fg_color = (fg_color & ~IM_COL32_A_MASK) | alpha_col;

            if (bg_color) {
                bg_color = (bg_color & ~IM_COL32_A_MASK) | alpha_col; // Set alpha
            }

            string_top_left = { x, y };

            if (has_bg_color) {
                // Reserve some space for background
                bg_color_vtx_write = vtx_write;
                bg_color_idx_write = idx_write;
                bg_color_vtx_current_idx = vtx_current_idx;
                vtx_write += 4;
                vtx_current_idx += 4;
                idx_write += 6;
            }

            if (new_font != current_font) {
                PopFont();
                PushFont(new_font);

                current_font = new_font;
            }
        }

        if (word_wrap_enabled)
        {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol)
            {
                word_wrap_eol = current_font->CalcWordWrapPositionA(scale, s, text_end, wrap_width - (x - pos.x));
                if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
                    word_wrap_eol++;    // +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
            }

            if (s >= word_wrap_eol)
            {
                x = pos.x;
                y += line_height;
                word_wrap_eol = NULL;

                // Wrapping skips upcoming blanks
                while (s < text_end)
                {
                    const char c = *s;
                    if (ImCharIsBlankA(c)) { s++; } else if (c == '\n') { s++; break; } else { break; }
                }
                continue;
            }
        }

        // Decode and advance source
        unsigned int c = (unsigned int) *s;
        if (c < 0x80) {
            s += 1;
        } else {
            s += ImTextCharFromUtf8(&c, s, text_end);
            if (c == 0) break; // Malformed UTF-8?
        }

        if (c < 32) {
            if (c == '\n') {
                x = pos.x;
                y += line_height;

                if (y > clip_rect.w) break;

                if (!word_wrap_enabled && y + line_height < clip_rect.y)
                    while (s < text_end && *s != '\n')  // Fast-forward to next line
                        s++;
                continue;
            }

            if (c == '\r') continue;
        }

        float char_width = 0.0f;
        if (const ImFontGlyph* glyph = current_font->FindGlyph((unsigned short)c)) {
            char_width = glyph->AdvanceX * scale;

            // Arbitrarily assume that both space and tabs are empty glyphs as an optimization
            if (c != ' ' && c != '\t') {
                // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
                float x1 = x + glyph->X0 * scale;
                float x2 = x + glyph->X1 * scale;
                float y1 = y + glyph->Y0 * scale;
                float y2 = y + glyph->Y1 * scale;
                if (x1 <= clip_rect.z && x2 >= clip_rect.x) {
                    // Render a character
                    float u1 = glyph->U0;
                    float v1 = glyph->V0;
                    float u2 = glyph->U1;
                    float v2 = glyph->V1;

                    // CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
                    if (cpu_fine_clip) {
                        if (x1 < clip_rect.x) {
                            u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                            x1 = clip_rect.x;
                        }

                        if (y1 < clip_rect.y) {
                            v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                            y1 = clip_rect.y;
                        }

                        if (x2 > clip_rect.z) {
                            u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                            x2 = clip_rect.z;
                        }

                        if (y2 > clip_rect.w) {
                            v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                            y2 = clip_rect.w;
                        }

                        if (y1 >= y2) {
                            x += char_width;
                            continue;
                        }
                    }

                    // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a debug builds. Inlined here:
                    {
                        idx_write[0] = (ImDrawIdx)(vtx_current_idx); idx_write[1] = (ImDrawIdx)(vtx_current_idx+1); idx_write[2] = (ImDrawIdx)(vtx_current_idx+2);
                        idx_write[3] = (ImDrawIdx)(vtx_current_idx); idx_write[4] = (ImDrawIdx)(vtx_current_idx+2); idx_write[5] = (ImDrawIdx)(vtx_current_idx+3);
                        vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = fg_color; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                        vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = fg_color; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                        vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = fg_color; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                        vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = fg_color; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                        vtx_write += 4;
                        vtx_current_idx += 4;
                        idx_write += 6;
                    }
                }
            }
        }

        x += char_width;
    }

    if (has_bg_color) {
        fill_bg_rect();
    }

    if (is_a_link) {
        link_button(current_string);
    }

    // Give back unused vertices
    draw_list->VtxBuffer.resize((int)(vtx_write - draw_list->VtxBuffer.Data));
    draw_list->IdxBuffer.resize((int)(idx_write - draw_list->IdxBuffer.Data));
    draw_list->CmdBuffer[draw_list->CmdBuffer.Size-1].ElemCount -= (idx_expected_size - draw_list->IdxBuffer.Size);
    draw_list->_VtxWritePtr = vtx_write;
    draw_list->_IdxWritePtr = idx_write;
    draw_list->_VtxCurrentIdx = (unsigned int)draw_list->VtxBuffer.Size;

    PopFont();
}

void add_rich_text(
        ImDrawList* draw_list,
        const ImVec2& pos,
        Rich_Text& text,
        float wrap_width,
        float alpha
) {
    if (text.rich[0].start == ARRAY_LAST(text.rich).end) {
        return;
    }

    // Pull default font/size from the shared ImDrawListSharedData instance
    float font_size = draw_list->_Data->FontSize;

    ImVec4 clip_rect = draw_list->_ClipRectStack.back();

    render_rich_text(draw_list, font_size, pos, clip_rect, text, wrap_width, alpha, false);
}
