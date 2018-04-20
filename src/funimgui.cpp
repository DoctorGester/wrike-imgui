#include "funimgui.h"
#include "draw.h"
#include <imgui.h>
#include <stdio.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <lodepng.h>
#include "common.h"
#include "temporary_storage.h"

#define STB_IMAGE_IMPLEMENTATION
#include "sdf.h"
#include "main.h"

#define DEBUGPRINT_KEYBOARD 0

const char* FunImGui::vertexShader = 
"uniform mat4 ProjMtx;                                  \n"
"attribute vec2 Position;                               \n"
"attribute vec2 UV;                                     \n"
"attribute vec4 Color;                                  \n"
"varying vec2 Frag_UV;                                  \n"
"varying vec4 Frag_Color;                               \n"
"void main()                                            \n"
"{                                                      \n"
"   Frag_UV = UV;                                       \n"
"   Frag_Color = Color;                                 \n"
"   gl_Position = ProjMtx * vec4(Position.xy, 0, 1);    \n"
"}                                                      \n";

const char* FunImGui::fragmentShader =
"uniform sampler2D Texture;                                     \n"
"varying mediump vec2 Frag_UV;                                  \n"
"varying mediump vec4 Frag_Color;                               \n"
"void main()                                                    \n"
"{                                                              \n"
"   gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV);    \n"
"}                                                              \n";

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

void FunImGui::init()
{
    ImGuiIO& io = ImGui::GetIO();

    io.KeyMap[ImGuiKey_Tab] = 9;
    io.KeyMap[ImGuiKey_LeftArrow] = 37;
    io.KeyMap[ImGuiKey_RightArrow] = 39;
    io.KeyMap[ImGuiKey_UpArrow] = 38;
    io.KeyMap[ImGuiKey_DownArrow] = 40;
    io.KeyMap[ImGuiKey_PageUp] = 33;
    io.KeyMap[ImGuiKey_PageDown] = 34;
    io.KeyMap[ImGuiKey_Home] = 36;
    io.KeyMap[ImGuiKey_End] = 35;
    io.KeyMap[ImGuiKey_Delete] = 46;
    io.KeyMap[ImGuiKey_Backspace] = 8;
    io.KeyMap[ImGuiKey_Enter] = 13;
    io.KeyMap[ImGuiKey_Escape] = 27;
    io.KeyMap[ImGuiKey_A] = 65;
    io.KeyMap[ImGuiKey_C] = 67;
    io.KeyMap[ImGuiKey_V] = 86;
    io.KeyMap[ImGuiKey_X] = 88;
    io.KeyMap[ImGuiKey_Y] = 89;
    io.KeyMap[ImGuiKey_Z] = 90;
    
    io.RenderDrawListsFn = RenderDrawLists;
    io.SetClipboardTextFn = SetClipboardText;
    io.GetClipboardTextFn = GetClipboardText;
    io.ClipboardUserData = nullptr;
    initGraphics();

    io.KeyRepeatDelay = 1.0f;
    io.KeyRepeatRate = 0.5f;

    ImGuiStyle* style = &ImGui::GetStyle();
    ImGui::StyleColorsLight(style);

    style->Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(color_background_dark);
    style->FrameRounding = 4.0f;
    style->ScaleAllSizes((float) emscripten_get_device_pixel_ratio());

    //io.MouseDrawCursor = true;
    emscripten_set_mousemove_callback(nullptr, nullptr, false, &FunImGui::mouseCallback);
    emscripten_set_mousedown_callback(nullptr, nullptr, false, &FunImGui::mouseCallback);
    emscripten_set_mouseup_callback(nullptr, nullptr, false, &FunImGui::mouseCallback);
    emscripten_set_wheel_callback("canvas", nullptr, false, &FunImGui::wheelCallback);
    emscripten_set_touchstart_callback(nullptr, nullptr, false, &FunImGui::touchCallback);
    emscripten_set_touchmove_callback(nullptr, nullptr, false, &FunImGui::touchCallback);
    emscripten_set_touchend_callback(nullptr, nullptr, false, &FunImGui::touchCallback);
    emscripten_set_touchcancel_callback(nullptr, nullptr, false, &FunImGui::touchCallback);

    //emscripten_set_keypress_callback(nullptr, nullptr, false, &FunImGui::keyboardCallback);
    emscripten_set_keydown_callback(nullptr, nullptr, false, &FunImGui::keyboardCallback);
    emscripten_set_keyup_callback(nullptr, nullptr, false, &FunImGui::keyboardCallback);

    //emscripten_request_pointerlock(nullptr, true);
    printf("pixel ratio: %f\n", emscripten_get_device_pixel_ratio());

    init_sdf();
}

void FunImGui::BeginFrame()
{
    ImGuiIO& io = ImGui::GetIO();
    int width = 0;
    int height = 0;
    int isFullscreen = 0;
    // int display_w = 0;
    // int display_h = 0;
    //
    //
    double ratio = emscripten_get_device_pixel_ratio();

    emscripten_get_canvas_element_size("canvas", &width, &height);

    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DisplayFramebufferScale = ImVec2(ratio, ratio);
    /*io.DisplayFramebufferScale = ImVec2(
            width  > 0 ? ((float)frame_buffer_width/width) : 0,
            height > 0 ? ((float)frame_buffer_height/height): 0
    );*/

    static double last;
    double now = emscripten_get_now();
    io.DeltaTime = (float)(now - last)/1000.0;
    last = now;
    ImGui::NewFrame();
}

void FunImGui::RenderDrawLists(ImDrawData* drawData)
{
    ImGuiIO& io = ImGui::GetIO();
    float fb_width = io.DisplaySize.x * io.DisplayFramebufferScale.x;
    float fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;

    //printf("size: %d, %d\n", fb_width, fb_height);

    /*fb_width = frame_buffer_width;
    fb_height = frame_buffer_height;*/

    if(fb_width == 0 || fb_height == 0)
        return;
    //drawData->ScaleClipRects(io.DisplayFramebufferScale);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);

    glViewport(0, 0, (GLsizei)io.DisplaySize.x, (GLsizei)io.DisplaySize.y);

    const float orthProjection[4][4] =
    {
        { 2.0f/io.DisplaySize.x, 0.0f,                    0.0f, 0.0f },
        { 0.0f,                  2.0f/-io.DisplaySize.y,  0.0f, 0.0f },
        { 0.0f,                  0.0f,                   -1.0f, 0.0f },
        {-1.0f,                  1.0f,                    0.0f, 1.0f },
    };

#if 1
    glUseProgram(m_shaderHandle);
    glUniform1i(m_texture, 0);
    glUniformMatrix4fv(m_projectionMatrix, 1, GL_FALSE, &orthProjection[0][0]);
    glBindVertexArrayOES(m_vao);

    for(int i=0; i < drawData->CmdListsCount; ++i)
    {
        const ImDrawList* cmdList = drawData->CmdLists[i];
        const ImDrawIdx* idxBufferOffset = nullptr;

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmdList->VtxBuffer.Size * sizeof(ImDrawVert), (GLvoid*)cmdList->VtxBuffer.Data, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_elements);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmdList->IdxBuffer.Size * sizeof(ImDrawIdx), (GLvoid*)cmdList->IdxBuffer.Data, GL_STREAM_DRAW);

        for(int j=0; j < cmdList->CmdBuffer.Size; ++j)
        {
            const ImDrawCmd* drawCommand = &cmdList->CmdBuffer[j];

            if(drawCommand->UserCallback)
            {
                drawCommand->UserCallback(cmdList, drawCommand);
            }
            else
            {
                glBindTexture(
                    GL_TEXTURE_2D,
                    (GLuint)(intptr_t)drawCommand->TextureId
                );

                glScissor(
                    (int)drawCommand->ClipRect.x,
                    (int)(io.DisplaySize.y - drawCommand->ClipRect.w),
                    (int)(drawCommand->ClipRect.z - drawCommand->ClipRect.x),
                    (int)(drawCommand->ClipRect.w - drawCommand->ClipRect.y)
                );
                glDrawElements(
                    GL_TRIANGLES,
                    (GLsizei)drawCommand->ElemCount,
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

const char* FunImGui::GetClipboardText(void*)
{
    return "";
}

void FunImGui::SetClipboardText(void*, const char* text)
{
}

void FunImGui::initGraphics()
{
    GLint lastTexture;
    GLint lastArrayBuffer;
    GLint lastVertexArray;
    
    GL_CHECKED( glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture) );
    GL_CHECKED( glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer) );
    GL_CHECKED( glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &lastVertexArray) );

    m_shaderHandle = glCreateProgram();
    GLint vertexHandle = glCreateShader(GL_VERTEX_SHADER);
    GLint fragmentHandle = glCreateShader(GL_FRAGMENT_SHADER);

    GL_CHECKED( glShaderSource(vertexHandle, 1, &vertexShader, 0) );
    GL_CHECKED( glShaderSource(fragmentHandle, 1, &fragmentShader, 0) );
    GL_CHECKED( glCompileShader(vertexHandle) );
    Draw::shaderErrors(vertexHandle);
    GL_CHECKED( glCompileShader(fragmentHandle) );
    Draw::shaderErrors(fragmentHandle);

    GL_CHECKED( glAttachShader(m_shaderHandle, vertexHandle) );
    GL_CHECKED( glAttachShader(m_shaderHandle, fragmentHandle) );

    GL_CHECKED( glLinkProgram(m_shaderHandle) );

    GL_CHECKED( m_texture = glGetUniformLocation(m_shaderHandle, "Texture") );
    GL_CHECKED( m_projectionMatrix = glGetUniformLocation(m_shaderHandle, "ProjMtx") );
    GL_CHECKED( m_position = glGetAttribLocation(m_shaderHandle, "Position") );
    GL_CHECKED( m_uv = glGetAttribLocation(m_shaderHandle, "UV") );
    GL_CHECKED( m_color = glGetAttribLocation(m_shaderHandle, "Color") );

    GL_CHECKED( glGenBuffers(1, &m_vbo) );
    GL_CHECKED( glGenBuffers(1, &m_elements) ); 

    GL_CHECKED( glGenVertexArraysOES(1, &m_vao) );
    GL_CHECKED( glBindVertexArrayOES(m_vao) );
    GL_CHECKED( glBindBuffer(GL_ARRAY_BUFFER, m_vbo) );
    GL_CHECKED( glEnableVertexAttribArray(m_position) );
    GL_CHECKED( glEnableVertexAttribArray(m_uv) );
    GL_CHECKED( glEnableVertexAttribArray(m_color) );
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    GL_CHECKED( glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos)) );
    GL_CHECKED( glVertexAttribPointer(m_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv)) );
    GL_CHECKED( glVertexAttribPointer(m_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col)) );
#undef OFFSETOF
    GL_CHECKED( glBindTexture(GL_TEXTURE_2D, lastTexture) );
    GL_CHECKED( glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer) );
    GL_CHECKED( glBindVertexArrayOES(lastVertexArray) );
    initFont();
}

ImFont* font_header;
ImFont* font_regular;
ImFont* font_bold;
ImFont* font_italic;
ImFont* font_bold_italic;

void FunImGui::initFont()
{
    float scale = (float) emscripten_get_device_pixel_ratio();
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
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
    io.Fonts->TexID = (void*)(intptr_t)m_fontTexture;
    glBindTexture(GL_TEXTURE_2D, lastTexture);

}

int FunImGui::mouseCallback(int eventType, const EmscriptenMouseEvent* mouseEvent, void* /*userData*/)
{
    float scale = (float) emscripten_get_device_pixel_ratio();
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float)mouseEvent->canvasX * scale, (float)mouseEvent->canvasY * scale);
    io.MouseDown[0] = mouseEvent->buttons & 1;
    io.MouseDown[1] = mouseEvent->buttons & 2;
    io.MouseDown[2] = mouseEvent->buttons & 4;

    if (eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
        // This allows us to respond to user clicks with stuff like opening urls, which otherwise get popup blocked
        loop();
    }

    return true;
}

int FunImGui::touchCallback(int eventType, const EmscriptenTouchEvent *touchEvent, void *userData) {
    float scale = (float) emscripten_get_device_pixel_ratio();
    ImGuiIO& io = ImGui::GetIO();

    if (touchEvent->numTouches > 0 || eventType == EMSCRIPTEN_EVENT_TOUCHEND) {
        const EmscriptenTouchPoint& touch = touchEvent->touches[0];
        io.MousePos = ImVec2((float)touch.canvasX * scale, (float)touch.canvasY * scale);
        io.MouseDown[0] = true;
    } else {
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
    }

    return true;
}

int FunImGui::wheelCallback(int /*eventType*/, const EmscriptenWheelEvent* wheelEvent, void* /*userData*/)
{
    printf("Scroll %f\n", wheelEvent->deltaY);

    ImGuiIO& io = ImGui::GetIO();

    if(wheelEvent->deltaY > 0)
        io.MouseWheel = -1.f/5.f;
    else if(wheelEvent->deltaY < 0)
        io.MouseWheel = 1.f/5.f;

    io.MouseWheel = (float) -wheelEvent->deltaY / 50.0f;

    return true;
}

int FunImGui::keyboardCallback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* /*userData*/)
{
    bool handled = true;
    ImGuiIO& io = ImGui::GetIO();
    //let copy paste fall through to browser
    if( 
        (keyEvent->ctrlKey || keyEvent->metaKey) &&
        (
            0 == strcmp("KeyC", keyEvent->code) || 
            0 == strcmp("KeyV", keyEvent->code) || 
            0 == strcmp("KeyX", keyEvent->code)
        )
    )
    {
        handled = true;
    }

    //todo, detect a single unicode codepoint instead of a single character

    if(
        strlen(keyEvent->key) <= 2 &&
        eventType == EMSCRIPTEN_EVENT_KEYDOWN
    )
    {
        io.AddInputCharactersUTF8(keyEvent->key);
    }
#if DEBUGPRINT_KEYBOARD
    printf(
            "%d key: %s, code: %s, ctrl: %d, shift: %d, alt: %d, meta: %d, repeat: %d, which: %lu\n",
            eventType,
            keyEvent->key,
            keyEvent->code,
            keyEvent->ctrlKey,
            keyEvent->shiftKey,
            keyEvent->altKey,
            keyEvent->metaKey,
            keyEvent->repeat,
            keyEvent->which
    );
#endif // DEBUGPRINT_KEYBOARD
    if(keyEvent->repeat)
    {
        return true;
    }
    io.KeyCtrl = keyEvent->ctrlKey;
    io.KeyShift = keyEvent->shiftKey;
    io.KeyAlt = keyEvent->altKey;
    io.KeySuper = keyEvent->metaKey;
    //io.KeySuper = false;

#if DEBUGPRINT_KEYBOARD
    printf("ctrl: %d, shift: %d, alt: %d, meta: %d\n",
        keyEvent->ctrlKey,
        keyEvent->shiftKey,
        keyEvent->altKey,
        keyEvent->metaKey
    );
#endif // DEBUGPRINT_KEYBOARD


    switch(eventType)
    {
    case EMSCRIPTEN_EVENT_KEYDOWN:
        {
            io.KeysDown[keyEvent->which] = 1;
        }
        break;
    case EMSCRIPTEN_EVENT_KEYUP:
        {
            io.KeysDown[keyEvent->which] = 0;
        }
        break;
    case EMSCRIPTEN_EVENT_KEYPRESS:
        {
            printf("%s was pressed\n", keyEvent->key);
        }
        break;
    }
    return handled;
}
