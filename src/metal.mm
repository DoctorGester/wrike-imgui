struct Framebuffer_Descriptor {
    unsigned long sample_count;
    MTLPixelFormat color_pixel_format;
    MTLPixelFormat depth_pixel_format;
    MTLPixelFormat stencil_pixel_format;
};

struct Pipeline_Context {
    id<MTLFunction> vertex_function;
    id<MTLFunction> fragment_function;
    MTLVertexDescriptor* vertex_descriptor;
};

struct Descriptor_To_Pipeline_State {
    Framebuffer_Descriptor descriptor;
    id<MTLRenderPipelineState> state;
};

struct Resizable_Buffer {
    id<MTLBuffer> buffer;
    bool is_claimed;
    u64 claimed_at;
};

static Pipeline_Context pipeline_context;
static Array<Descriptor_To_Pipeline_State> descriptor_to_context{};
static id<MTLDepthStencilState> stencil_state;

static Array<Resizable_Buffer> vertex_buffers{};
static Array<Resizable_Buffer> index_buffers{};

static inline bool descriptors_equal(Framebuffer_Descriptor a, Framebuffer_Descriptor b) {
    return memcmp(&a, &b, sizeof(Framebuffer_Descriptor)) == 0;
}

static id<MTLRenderPipelineState> render_pipeline_state_for_framebuffer_descriptor(id<MTLDevice> device, Pipeline_Context* context, Framebuffer_Descriptor descriptor) {
    MTLRenderPipelineDescriptor* pipeline_descriptor = [MTLRenderPipelineDescriptor new];
    pipeline_descriptor.vertexFunction = context->vertex_function;
    pipeline_descriptor.fragmentFunction = context->fragment_function;
    pipeline_descriptor.vertexDescriptor = context->vertex_descriptor;
    pipeline_descriptor.sampleCount = descriptor.sample_count;
    pipeline_descriptor.colorAttachments[0].pixelFormat = descriptor.color_pixel_format;
    pipeline_descriptor.colorAttachments[0].blendingEnabled = YES;
    pipeline_descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeline_descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeline_descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline_descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline_descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline_descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeline_descriptor.depthAttachmentPixelFormat = descriptor.depth_pixel_format;
    pipeline_descriptor.stencilAttachmentPixelFormat = descriptor.stencil_pixel_format;

    NSError* error = nil;

    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];

    if (error != nil) {
        NSLog(@"Error: failed to create Metal pipeline state: %@", error);
    }

    return pipeline;
}

static id<MTLRenderPipelineState> find_pipeline_state_by_framebuffer_descriptor_or_allocate_one(id<MTLDevice> device, Framebuffer_Descriptor needle) {
    Descriptor_To_Pipeline_State* array_end = descriptor_to_context.data + descriptor_to_context.length;

    for (Descriptor_To_Pipeline_State* it = descriptor_to_context.data; it < array_end; it++) {
        if (descriptors_equal(it->descriptor, needle)) {
            return it->state;
        }
    }

    NSLog(@"Creating new pipeline state");

    Descriptor_To_Pipeline_State new_pair;
    new_pair.descriptor = needle;
    new_pair.state = render_pipeline_state_for_framebuffer_descriptor(device, &pipeline_context, needle);

    add_item_to_array(descriptor_to_context, new_pair);

    return new_pair.state;
}

bool fill_pipeline_context(Pipeline_Context* context, id<MTLDevice> device) {
    context->vertex_function = nil;
    context->fragment_function = nil;
    context->vertex_descriptor = nil;

    NSError* error = nil;

    NSString* shader_source = @""
                             "#include <metal_stdlib>\n"
                             "using namespace metal;\n"
                             "\n"
                             "struct Uniforms {\n"
                             "    float4x4 projectionMatrix;\n"
                             "};\n"
                             "\n"
                             "struct VertexIn {\n"
                             "    float2 position  [[attribute(0)]];\n"
                             "    float2 texCoords [[attribute(1)]];\n"
                             "    uchar4 color     [[attribute(2)]];\n"
                             "};\n"
                             "\n"
                             "struct VertexOut {\n"
                             "    float4 position [[position]];\n"
                             "    float2 texCoords;\n"
                             "    float4 color;\n"
                             "};\n"
                             "\n"
                             "vertex VertexOut vertex_main(VertexIn in                 [[stage_in]],\n"
                             "                             constant Uniforms &uniforms [[buffer(1)]]) {\n"
                             "    VertexOut out;\n"
                             "    out.position = uniforms.projectionMatrix * float4(in.position, 0, 1);\n"
                             "    out.texCoords = in.texCoords;\n"
                             "    out.color = float4(in.color) / float4(255.0);\n"
                             "    return out;\n"
                             "}\n"
                             "\n"
                             "fragment half4 fragment_main(VertexOut in [[stage_in]],\n"
                             "                             texture2d<half, access::sample> texture [[texture(0)]]) {\n"
                             "    constexpr sampler linearSampler(coord::normalized, min_filter::linear, mag_filter::linear, mip_filter::linear);\n"
                             "    half4 texColor = texture.sample(linearSampler, in.texCoords);\n"
                             "    return half4(in.color) * texColor;\n"
                             "}\n";

    id<MTLLibrary> library = [device newLibraryWithSource:shader_source options:nil error:&error];

    if (library == nil) {
        NSLog(@"Error: failed to create Metal library: %@", error);
        return false;
    }

    context->vertex_function = [library newFunctionWithName:@"vertex_main"];
    context->fragment_function = [library newFunctionWithName:@"fragment_main"];

    if (context->vertex_function == nil || context->fragment_function == nil) {
        NSLog(@"Error: failed to find Metal shader functions in library: %@", error);
        return false;
    }

    MTLVertexDescriptor* vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
    vertex_descriptor.attributes[0].offset = IM_OFFSETOF(ImDrawVert, pos);
    vertex_descriptor.attributes[0].format = MTLVertexFormatFloat2; // position
    vertex_descriptor.attributes[0].bufferIndex = 0;
    vertex_descriptor.attributes[1].offset = IM_OFFSETOF(ImDrawVert, uv);
    vertex_descriptor.attributes[1].format = MTLVertexFormatFloat2; // texCoords
    vertex_descriptor.attributes[1].bufferIndex = 0;
    vertex_descriptor.attributes[2].offset = IM_OFFSETOF(ImDrawVert, col);
    vertex_descriptor.attributes[2].format = MTLVertexFormatUChar4; // color
    vertex_descriptor.attributes[2].bufferIndex = 0;
    vertex_descriptor.layouts[0].stepRate = 1;
    vertex_descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    vertex_descriptor.layouts[0].stride = sizeof(ImDrawVert);

    [vertex_descriptor retain];

    context->vertex_descriptor = vertex_descriptor;

    return true;
}

bool metal_program_init(id <MTLDevice> device) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_metal";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    MTLDepthStencilDescriptor* stencil_descriptor = [MTLDepthStencilDescriptor new];
    stencil_descriptor.depthWriteEnabled = NO;
    stencil_descriptor.depthCompareFunction = MTLCompareFunctionAlways;

    stencil_state = [device newDepthStencilStateWithDescriptor:stencil_descriptor];

    [stencil_descriptor release];

    if (!fill_pipeline_context(&pipeline_context, device)) {
        panic("Failed to initialize pipeline context");
    }

    return true;
}

static void setup_render_state(ImDrawData* draw_data, id<MTLRenderCommandEncoder> cmd_encoder, id<MTLRenderPipelineState> pipeline) {
    [cmd_encoder setCullMode:MTLCullModeNone];
    [cmd_encoder setDepthStencilState:stencil_state];

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to
    // draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
    MTLViewport viewport =
            {
                    .originX = 0.0,
                    .originY = 0.0,
                    .width = (double)(draw_data->DisplaySize.x),// * draw_data->FramebufferScale.x),
                    .height = (double)(draw_data->DisplaySize.y),// * draw_data->FramebufferScale.y),
                    .znear = 0.0,
                    .zfar = 1.0
            };
    [cmd_encoder setViewport:viewport];

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    float N = viewport.znear;
    float F = viewport.zfar;
    const float ortho_projection[4][4] =
            {
                    { 2.0f/(R-L),   0.0f,           0.0f,   0.0f },
                    { 0.0f,         2.0f/(T-B),     0.0f,   0.0f },
                    { 0.0f,         0.0f,        1/(F-N),   0.0f },
                    { (R+L)/(L-R),  (T+B)/(B-T), N/(F-N),   1.0f },
            };
    [cmd_encoder setVertexBytes:&ortho_projection length:sizeof(ortho_projection) atIndex:1];

    [cmd_encoder setRenderPipelineState:pipeline];
}

static id<MTLBuffer> ensure_buffer_size_or_realloc(id<MTLDevice> device, id<MTLBuffer> buffer, size_t required_length) {
    if (!buffer || buffer.length < required_length) {
        size_t amortized_size = (size_t) ceil(required_length * 1.4);

        if (buffer) {
            [buffer release];
        }

        return [device newBufferWithLength:amortized_size options:MTLResourceStorageModeShared];
    }

    return buffer;
}

static u32 claim_or_create_buffer_of_size(id<MTLDevice> device, Array<Resizable_Buffer>& buffers, size_t required_length) {
    float max_claimed = 0;

    for (u32 buffer_index = 0; buffer_index < buffers.length; buffer_index++) {
        Resizable_Buffer* resizable = &buffers[buffer_index];
        if (!resizable->is_claimed) {
            resizable->buffer = ensure_buffer_size_or_realloc(device, resizable->buffer, required_length);
            resizable->is_claimed = true;
            resizable->claimed_at = platform_get_app_time_precise();

            return buffer_index;
        } else {
            max_claimed = MAX(max_claimed, platform_get_delta_time_ms(resizable->claimed_at));
        }
    }

    Resizable_Buffer new_buffer;
    new_buffer.is_claimed = true;
    new_buffer.buffer = ensure_buffer_size_or_realloc(device, nil, required_length);
    new_buffer.claimed_at = platform_get_app_time_precise();

    add_item_to_array(buffers, new_buffer);

    NSLog(@"Allocating buffer with size %li, num buffers: %u, max claim time: %fms", required_length, buffers.length, max_claimed);

    return buffers.length - 1;
}

void metal_render_frame(ImDrawData* draw_data, MTLRenderPassDescriptor* pass, id<MTLCommandBuffer> cmd_buffer, id<MTLRenderCommandEncoder> cmd_encoder) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x);// * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y);// * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || draw_data->CmdListsCount == 0) {
        return;
    }

    Framebuffer_Descriptor descriptor{0};
    descriptor.sample_count = pass.colorAttachments[0].texture.sampleCount;
    descriptor.color_pixel_format = pass.colorAttachments[0].texture.pixelFormat;
    descriptor.depth_pixel_format = pass.depthAttachment.texture.pixelFormat;
    descriptor.stencil_pixel_format = pass.stencilAttachment.texture.pixelFormat;

    id<MTLRenderPipelineState> pipeline = find_pipeline_state_by_framebuffer_descriptor_or_allocate_one(cmd_buffer.device, descriptor);

    size_t vertex_buffer_length = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    size_t index_buffer_length = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    u32 vertex_buffer_index = claim_or_create_buffer_of_size(cmd_buffer.device, vertex_buffers, vertex_buffer_length);
    u32 index_buffer_index = claim_or_create_buffer_of_size(cmd_buffer.device, index_buffers, index_buffer_length);

    id<MTLBuffer> vertex_buffer = vertex_buffers[vertex_buffer_index].buffer;
    id<MTLBuffer> index_buffer = index_buffers[index_buffer_index].buffer;

    setup_render_state(draw_data, cmd_encoder, pipeline);

    [cmd_encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = { 1, 1 };//draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    size_t vertex_buffer_offset = 0;
    size_t index_buffer_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];

        memcpy((char *) vertex_buffer.contents + vertex_buffer_offset, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy((char *) index_buffer.contents + index_buffer_offset, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));

        for (int command_index = 0; command_index < cmd_list->CmdBuffer.Size; command_index++) {
            const ImDrawCmd* draw_command = &cmd_list->CmdBuffer[command_index];

            if (draw_command->UserCallback) {
                // can add ImDrawCallback_ResetRenderState handling here
                draw_command->UserCallback(cmd_list, draw_command);
            } else {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clip_rect;
                clip_rect.x = (draw_command->ClipRect.x - clip_off.x) * clip_scale.x;
                clip_rect.y = (draw_command->ClipRect.y - clip_off.y) * clip_scale.y;
                clip_rect.z = (draw_command->ClipRect.z - clip_off.x) * clip_scale.x;
                clip_rect.w = (draw_command->ClipRect.w - clip_off.y) * clip_scale.y;

                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
                    MTLScissorRect scissor =
                            {
                                    .x = NSUInteger(clip_rect.x),
                                    .y = NSUInteger(clip_rect.y),
                                    .width = NSUInteger(clip_rect.z - clip_rect.x),
                                    .height = NSUInteger(clip_rect.w - clip_rect.y)
                            };
                    [cmd_encoder setScissorRect:scissor];

                    if (draw_command->TextureId != NULL) {
                        [cmd_encoder setFragmentTexture:(__bridge id <MTLTexture>) (draw_command->TextureId) atIndex:0];
                    }

                    [cmd_encoder setVertexBufferOffset:(vertex_buffer_offset + draw_command->VtxOffset * sizeof(ImDrawVert)) atIndex:0];
                    [cmd_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                            indexCount:draw_command->ElemCount
                                             indexType:sizeof(ImDrawIdx) == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                                           indexBuffer:index_buffer
                                     indexBufferOffset:index_buffer_offset + draw_command->IdxOffset * sizeof(ImDrawIdx)];
                }
            }
        }

        vertex_buffer_offset += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
        index_buffer_offset += cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);

        [cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer>)
        {
            dispatch_async(dispatch_get_main_queue(), ^{
                vertex_buffers[vertex_buffer_index].is_claimed = false;
                index_buffers[index_buffer_index].is_claimed = false;
            });
        }];
    }
}