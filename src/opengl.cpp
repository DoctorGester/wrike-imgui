struct Gl_Data {
    u32 handle_shader;
    u32 handle_vao;
    u32 handle_vbo;
    u32 handle_elements;

    s32 uniform_projection_matrix;
    s32 uniform_texture;

    s32 attribute_position;
    s32 attribute_color;
    s32 attribute_uv;
};

#define GL_CHECKED(command)\
    command;\
    for(int error = glGetError(); (error=glGetError()); error != GL_NO_ERROR)\
    {\
        printf("glerror: %d\n", error);\
    }


GLuint opengl_make_texture(GLsizei width, GLsizei height, GLvoid* pixels) {
    GLuint new_texture;
    GLint last_texture;

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);

    {
        glGenTextures(1, &new_texture);
        glBindTexture(GL_TEXTURE_2D, new_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    glBindTexture(GL_TEXTURE_2D, last_texture);

    return new_texture;
}

void opengl_render_frame(ImDrawData* draw_data, Gl_Data gl) {
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

    glUseProgram(gl.handle_shader);
    glUniform1i(gl.uniform_texture, 0);
    glUniformMatrix4fv(gl.uniform_projection_matrix, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(gl.handle_vao);

    for (int i = 0; i < draw_data->CmdListsCount; ++i) {
        const ImDrawList* cmd_list = draw_data->CmdLists[i];
        const ImDrawIdx* idx_buffer_offset = nullptr;

        glBindBuffer(GL_ARRAY_BUFFER, gl.handle_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) cmd_list->VtxBuffer.Size * sizeof(ImDrawVert),
                     (GLvoid*) cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl.handle_elements);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx),
                     (GLvoid*) cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

        for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
            const ImDrawCmd* draw_command = &cmd_list->CmdBuffer[j];

            if (draw_command->UserCallback) {
                draw_command->UserCallback(cmd_list, draw_command);
            } else if (draw_command->TextureId) {
                glBindTexture(
                        GL_TEXTURE_2D,
                        (GLuint) (uintptr_t) draw_command->TextureId
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

void opengl_program_init(Gl_Data& gl, const char* vertex_shader_source, const char* fragment_shader_source) {
    GLint last_array_buffer;
    GLint last_vertex_array;

    GL_CHECKED(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer));
    GL_CHECKED(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array));

    gl.handle_shader = glCreateProgram();

    GLuint vertexHandle = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentHandle = glCreateShader(GL_FRAGMENT_SHADER);

    GL_CHECKED(glShaderSource(vertexHandle, 1, &vertex_shader_source, 0));
    GL_CHECKED(glShaderSource(fragmentHandle, 1, &fragment_shader_source, 0));
    GL_CHECKED(glCompileShader(vertexHandle));
    print_shader_errors(vertexHandle);
    GL_CHECKED(glCompileShader(fragmentHandle));
    print_shader_errors(fragmentHandle);

    GL_CHECKED(glAttachShader(gl.handle_shader, vertexHandle));
    GL_CHECKED(glAttachShader(gl.handle_shader, fragmentHandle));

    GL_CHECKED(glLinkProgram(gl.handle_shader));

    GL_CHECKED(gl.uniform_texture = glGetUniformLocation(gl.handle_shader, "Texture"));
    GL_CHECKED(gl.uniform_projection_matrix = glGetUniformLocation(gl.handle_shader, "ProjMtx"));
    GL_CHECKED(gl.attribute_position = glGetAttribLocation(gl.handle_shader, "Position"));
    GL_CHECKED(gl.attribute_uv = glGetAttribLocation(gl.handle_shader, "UV"));
    GL_CHECKED(gl.attribute_color = glGetAttribLocation(gl.handle_shader, "Color"));

    GL_CHECKED(glGenBuffers(1, &gl.handle_vbo));
    GL_CHECKED(glGenBuffers(1, &gl.handle_elements));

    GL_CHECKED(glGenVertexArrays(1, &gl.handle_vao));
    GL_CHECKED(glBindVertexArray(gl.handle_vao));
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, gl.handle_vbo));
    GL_CHECKED(glEnableVertexAttribArray(gl.attribute_position));
    GL_CHECKED(glEnableVertexAttribArray(gl.attribute_uv));
    GL_CHECKED(glEnableVertexAttribArray(gl.attribute_color));
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    GL_CHECKED(glVertexAttribPointer(gl.attribute_position, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, pos)));
    GL_CHECKED(glVertexAttribPointer(gl.attribute_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, uv)));
    GL_CHECKED(glVertexAttribPointer(gl.attribute_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*) OFFSETOF(ImDrawVert, col)));
#undef OFFSETOF
    GL_CHECKED(glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer));
    GL_CHECKED(glBindVertexArray(last_vertex_array));
}