#include "funimgui.h"
#include <imgui.h>
#include <stdio.h>

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

#include <lodepng.h>
#include "common.h"
#include "temporary_storage.h"

#define STB_IMAGE_IMPLEMENTATION

//#include "sdf.h"
#include "main.h"
#include "platform.h"

#define DEBUGPRINT_KEYBOARD 0

int FunImGui::m_shaderHandle = -1;
int FunImGui::m_texture = -1;
int FunImGui::m_projectionMatrix = -1;
int FunImGui::m_position = -1;
int FunImGui::m_uv = -1;
int FunImGui::m_color = -1;
unsigned int FunImGui::m_vao = -1;
unsigned int FunImGui::m_vbo = -1;
unsigned int FunImGui::m_elements = -1;
unsigned int FunImGui::m_fontTexture = -1;

void FunImGui::RenderDrawLists(ImDrawData* drawData) {
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

    const float orthProjection[4][4] =
            {
                    {2.0f / io.DisplaySize.x, 0.0f,                     0.0f,  0.0f},
                    {0.0f,                    2.0f / -io.DisplaySize.y, 0.0f,  0.0f},
                    {0.0f,                    0.0f,                     -1.0f, 0.0f},
                    {-1.0f,                   1.0f,                     0.0f,  1.0f},
            };

#if 1
    glUseProgram(m_shaderHandle);
    glUniform1i(m_texture, 0);
    glUniformMatrix4fv(m_projectionMatrix, 1, GL_FALSE, &orthProjection[0][0]);
    glBindVertexArray(m_vao);

    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* cmdList = drawData->CmdLists[i];
        const ImDrawIdx* idxBufferOffset = nullptr;

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) cmdList->VtxBuffer.Size * sizeof(ImDrawVert),
                     (GLvoid*) cmdList->VtxBuffer.Data, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_elements);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) cmdList->IdxBuffer.Size * sizeof(ImDrawIdx),
                     (GLvoid*) cmdList->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int j = 0; j < cmdList->CmdBuffer.Size; ++j) {
            const ImDrawCmd* drawCommand = &cmdList->CmdBuffer[j];

            if (drawCommand->UserCallback) {
                drawCommand->UserCallback(cmdList, drawCommand);
            } else {
                glBindTexture(
                        GL_TEXTURE_2D,
                        (GLuint) (intptr_t) drawCommand->TextureId
                );

                glScissor(
                        (int) drawCommand->ClipRect.x,
                        (int) (io.DisplaySize.y - drawCommand->ClipRect.w),
                        (int) (drawCommand->ClipRect.z - drawCommand->ClipRect.x),
                        (int) (drawCommand->ClipRect.w - drawCommand->ClipRect.y)
                );
                glDrawElements(
                        GL_TRIANGLES,
                        (GLsizei) drawCommand->ElemCount,
                        sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                        idxBufferOffset
                );
            }
            idxBufferOffset += drawCommand->ElemCount;
        }
    }
#endif

#if 0
    render_test_sdf_text(14.0f * io.DisplayFramebufferScale.x, &orthProjection[0][0]);
#endif
}


static void print_shader_errors(int shader) {
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        printf("Shader error\n");
        int infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = new char[infoLen + 1];
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            printf("Shader failed:\n%s\n ", infoLog);
            delete[] infoLog;
        }
    }
}

void FunImGui::initGraphics(const char* vertex_shader_source, const char* fragment_shader_source) {
    GLint lastArrayBuffer;
    GLint lastVertexArray;

    GL_CHECKED(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer));
    GL_CHECKED(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray));

    m_shaderHandle = glCreateProgram();
    GLint vertexHandle = glCreateShader(GL_VERTEX_SHADER);
    GLint fragmentHandle = glCreateShader(GL_FRAGMENT_SHADER);

    GL_CHECKED(glShaderSource(vertexHandle, 1, &vertex_shader_source, 0));
    GL_CHECKED(glShaderSource(fragmentHandle, 1, &fragment_shader_source, 0));
    GL_CHECKED(glCompileShader(vertexHandle));
    print_shader_errors(vertexHandle);
    GL_CHECKED(glCompileShader(fragmentHandle));
    print_shader_errors(fragmentHandle);

    GL_CHECKED(glAttachShader(m_shaderHandle, vertexHandle));
    GL_CHECKED(glAttachShader(m_shaderHandle, fragmentHandle));

    GL_CHECKED(glLinkProgram(m_shaderHandle));

    GL_CHECKED(m_texture = glGetUniformLocation(m_shaderHandle, "Texture"));
    GL_CHECKED(m_projectionMatrix = glGetUniformLocation(m_shaderHandle, "ProjMtx"));
    GL_CHECKED(m_position = glGetAttribLocation(m_shaderHandle, "Position"));
    GL_CHECKED(m_uv = glGetAttribLocation(m_shaderHandle, "UV"));
    GL_CHECKED(m_color = glGetAttribLocation(m_shaderHandle, "Color"));

    GL_CHECKED(glGenBuffers(1, &m_vbo));
    GL_CHECKED(glGenBuffers(1, &m_elements));

    GL_CHECKED(glGenVertexArrays(1, &m_vao));
    GL_CHECKED(glBindVertexArray(m_vao));
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, m_vbo));
    GL_CHECKED(glEnableVertexAttribArray(m_position));
    GL_CHECKED(glEnableVertexAttribArray(m_uv));
    GL_CHECKED(glEnableVertexAttribArray(m_color));
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    GL_CHECKED(glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, pos)));
    GL_CHECKED(glVertexAttribPointer(m_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, uv)));
    GL_CHECKED(glVertexAttribPointer(m_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, col)));
#undef OFFSETOF
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer));
    GL_CHECKED(glBindVertexArray(lastVertexArray));
    initFont();
}

ImFont* font_header;
ImFont* font_regular;
ImFont* font_bold;
ImFont* font_italic;
ImFont* font_bold_italic;

void FunImGui::initFont() {
    float scale = platform_get_pixel_ratio();
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    ImFontConfig font_config{};
    font_config.OversampleH = 3;
    font_config.OversampleV = 1;

#define LOAD_FONT(name, size) io.Fonts->AddFontFromFileTTF((name), (size) * scale, &font_config, io.Fonts->GetGlyphRangesCyrillic())

    const float default_font_size = 16.0f;

    font_regular = LOAD_FONT("resources/OpenSans-Regular.ttf", default_font_size);
    font_header = LOAD_FONT("resources/OpenSans-Regular.ttf", 28.0f);
    font_bold = LOAD_FONT("resources/OpenSans-Bold.ttf", default_font_size);
    font_italic = LOAD_FONT("resources/OpenSans-Italic.ttf", default_font_size);
    font_bold_italic = LOAD_FONT("resources/OpenSans-BoldItalic.ttf", default_font_size);

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    //io.FontGlobalScale = 1.0f / scale;

#undef LOAD_FONT

    GLint lastTexture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
    glGenTextures(1, &m_fontTexture);
    glBindTexture(GL_TEXTURE_2D, m_fontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    //glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = (void*) (intptr_t) m_fontTexture;
    glBindTexture(GL_TEXTURE_2D, lastTexture);

}