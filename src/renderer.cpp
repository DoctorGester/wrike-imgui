#include "renderer.h"
#include <imgui.h>
#include <stdio.h>
#include <imgui_internal.h>

#define GL_GLEXT_PROTOTYPES
#if EMSCRIPTEN
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>

    #define glBindVertexArray(x) glBindVertexArrayOES(x)
    #define glGenVertexArrays(x, y) glGenVertexArraysOES(x, y)
    #define GL_VERTEX_ARRAY_BINDING GL_VERTEX_ARRAY_BINDING_OES
#else
    #include <SDL2/SDL_opengl.h>
#endif

#include "common.h"
#include "temporary_storage.h"

//#include "sdf.h"
#include "main.h"
#include "platform.h"

static u32 handle_shader;
static u32 handle_vao;
static u32 handle_vbo;
static u32 handle_elements;
static u32 handle_font_texture;

static s32 uniform_projection_matrix;
static s32 uniform_texture;

static s32 attribute_position;
static s32 attribute_color;
static s32 attribute_uv;

ImFont* font_regular;
ImFont* font_28px;
ImFont* font_19px;
ImFont* font_19px_bold;
ImFont* font_bold;
ImFont* font_italic;
ImFont* font_bold_italic;

void renderer_draw_lists(ImDrawData* drawData) {
    ImGuiIO &io = ImGui::GetIO();
    //drawData->ScaleClipRects(io.DisplayFramebufferScale);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    glViewport(0, 0, (GLsizei) io.DisplaySize.x, (GLsizei) io.DisplaySize.y);

    const float ortho_projection[4][4] =
            {
                    {2.0f / io.DisplaySize.x, 0.0f,                     0.0f,  0.0f},
                    {0.0f,                    2.0f / -io.DisplaySize.y, 0.0f,  0.0f},
                    {0.0f,                    0.0f,                     -1.0f, 0.0f},
                    {-1.0f,                   1.0f,                     0.0f,  1.0f},
            };

#if 1
    glUseProgram(handle_shader);
    glUniform1i(uniform_texture, 0);
    glUniformMatrix4fv(uniform_projection_matrix, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(handle_vao);

    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* cmd_list = drawData->CmdLists[i];
        const ImDrawIdx* idx_buffer_offset = nullptr;

        glBindBuffer(GL_ARRAY_BUFFER, handle_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) cmd_list->VtxBuffer.Size * sizeof(ImDrawVert),
                     (GLvoid*) cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle_elements);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx),
                     (GLvoid*) cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
            const ImDrawCmd* draw_command = &cmd_list->CmdBuffer[j];

            if (draw_command->UserCallback) {
                draw_command->UserCallback(cmd_list, draw_command);
            } else if (draw_command->TextureId) {
                glBindTexture(
                        GL_TEXTURE_2D,
                        (GLuint) (intptr_t) draw_command->TextureId
                );

                glScissor(
                        (int) draw_command->ClipRect.x,
                        (int) (io.DisplaySize.y - draw_command->ClipRect.w),
                        (int) (draw_command->ClipRect.z - draw_command->ClipRect.x),
                        (int) (draw_command->ClipRect.w - draw_command->ClipRect.y)
                );
                glDrawElements(
                        GL_TRIANGLES,
                        (GLsizei) draw_command->ElemCount,
                        sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                        idx_buffer_offset
                );
            }

            idx_buffer_offset += draw_command->ElemCount;
        }
    }
#endif

#if 0
    render_test_sdf_text(14.0f * io.DisplayFramebufferScale.x, &orthProjection[0][0]);
#endif
}

static void print_shader_errors(u32 shader) {
    static char error_buffer[2048];

    s32 ok = 0, error_length = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (ok) {
        return;
    }

    printf("Shader error\n");
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &error_length);

    if (error_length > 1) {
        glGetShaderInfoLog(shader, MIN(sizeof(error_buffer), error_length), NULL, error_buffer);
        printf("Shader failed:\n%s\n ", error_buffer);
    }
}

static void renderer_init_font() {
    float scale = platform_get_pixel_ratio();
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    ImFontConfig font_config{};
    font_config.OversampleH = 3;
    font_config.OversampleV = 1;

    size_t file_size = 0;
    void* sans_regular = ImFileLoadToMemory("resources/OpenSans-Regular.ttf", "r", &file_size);

    assert(sans_regular);

#define LOAD_SANS(size) io.Fonts->AddFontFromMemoryTTF(sans_regular, file_size, (size) * scale, &font_config, io.Fonts->GetGlyphRangesCyrillic())
#define LOAD_FONT(name, size) io.Fonts->AddFontFromFileTTF((name), (size) * scale, &font_config, io.Fonts->GetGlyphRangesCyrillic())

    const float default_font_size = 16.0f;

    // font_regular should be loaded first, so it becomes a default font
    font_regular = LOAD_SANS(default_font_size);

    font_28px = LOAD_SANS(28.0f);
    font_19px = LOAD_SANS(19.0f);
    font_19px_bold = LOAD_FONT("resources/OpenSans-Bold.ttf", 19.0f);
    font_bold = LOAD_FONT("resources/OpenSans-Bold.ttf", default_font_size);
    font_italic = LOAD_FONT("resources/OpenSans-Italic.ttf", default_font_size);
    font_bold_italic = LOAD_FONT("resources/OpenSans-BoldItalic.ttf", default_font_size);

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    //io.FontGlobalScale = 1.0f / scale;

    ImGui::MemFree(sans_regular);

#undef LOAD_SANS
#undef LOAD_FONT

    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &handle_font_texture);
    glBindTexture(GL_TEXTURE_2D, handle_font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = (void*) (intptr_t) handle_font_texture;
    glBindTexture(GL_TEXTURE_2D, last_texture);

    io.Fonts->ClearTexData();
}

void renderer_init(const char* vertex_shader_source, const char* fragment_shader_source) {
    GLint last_array_buffer;
    GLint last_vertex_array;

    GL_CHECKED(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer));
    GL_CHECKED(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array));

    handle_shader = glCreateProgram();

    GLuint vertexHandle = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentHandle = glCreateShader(GL_FRAGMENT_SHADER);

    GL_CHECKED(glShaderSource(vertexHandle, 1, &vertex_shader_source, 0));
    GL_CHECKED(glShaderSource(fragmentHandle, 1, &fragment_shader_source, 0));
    GL_CHECKED(glCompileShader(vertexHandle));
    print_shader_errors(vertexHandle);
    GL_CHECKED(glCompileShader(fragmentHandle));
    print_shader_errors(fragmentHandle);

    GL_CHECKED(glAttachShader(handle_shader, vertexHandle));
    GL_CHECKED(glAttachShader(handle_shader, fragmentHandle));

    GL_CHECKED(glLinkProgram(handle_shader));

    GL_CHECKED(uniform_texture = glGetUniformLocation(handle_shader, "Texture"));
    GL_CHECKED(uniform_projection_matrix = glGetUniformLocation(handle_shader, "ProjMtx"));
    GL_CHECKED(attribute_position = glGetAttribLocation(handle_shader, "Position"));
    GL_CHECKED(attribute_uv = glGetAttribLocation(handle_shader, "UV"));
    GL_CHECKED(attribute_color = glGetAttribLocation(handle_shader, "Color"));

    GL_CHECKED(glGenBuffers(1, &handle_vbo));
    GL_CHECKED(glGenBuffers(1, &handle_elements));

    GL_CHECKED(glGenVertexArrays(1, &handle_vao));
    GL_CHECKED(glBindVertexArray(handle_vao));
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, handle_vbo));
    GL_CHECKED(glEnableVertexAttribArray(attribute_position));
    GL_CHECKED(glEnableVertexAttribArray(attribute_uv));
    GL_CHECKED(glEnableVertexAttribArray(attribute_color));
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    GL_CHECKED(glVertexAttribPointer(attribute_position, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, pos)));
    GL_CHECKED(glVertexAttribPointer(attribute_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, uv)));
    GL_CHECKED(glVertexAttribPointer(attribute_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, col)));
#undef OFFSETOF
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer));
    GL_CHECKED(glBindVertexArray(last_vertex_array));

    renderer_init_font();
}