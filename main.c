#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <webgpu/webgpu.h>
#include <cglm/cglm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Vertex {
    float position[4];
    float color[4];
} Vertex;

typedef struct UniformBuffer {
    mat4 mvp;
} UniformBuffer;

static Vertex vertices[3] = {
    {{1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
};

static uint32_t indices[3] = {0, 1, 2};

static const char* shader_source =
"struct Uniform {\n"
"    mvp: mat4x4<f32>,\n"
"};\n"
"\n"
"@group(0) @binding(0)\n"
"var<uniform> ubo: Uniform;\n"
"\n"
"struct VertexInput {\n"
"    @location(0) position: vec4<f32>,\n"
"    @location(1) color: vec4<f32>,\n"
"};\n"
"\n"
"struct VertexOutput {\n"
"    @builtin(position) position: vec4<f32>,\n"
"    @location(0) color: vec4<f32>,\n"
"};\n"
"\n"
"@vertex\n"
"fn vertex_main(vert: VertexInput) -> VertexOutput {\n"
"    var out: VertexOutput;\n"
"    out.color = vert.color;\n"
"    out.position = ubo.mvp * vert.position;\n"
"    return out;\n"
"}\n"
"\n"
"@fragment\n"
"fn fragment_main(in: VertexOutput) -> @location(0) vec4<f32> {\n"
"    return vec4<f32>(in.color);\n"
"}\n";

#define DEPTH_FORMAT WGPUTextureFormat_Depth32Float

typedef struct State {
    WGPUInstance instance;
    WGPUSurface surface;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurfaceConfiguration surface_config;
    WGPUTexture depth_texture;
    WGPUTextureView depth_view;
    WGPURenderPipeline pipeline;
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;
    WGPUBuffer uniform_buffer;
    WGPUBindGroup bind_group;
    WGPUBindGroupLayout bind_group_layout;
    mat4 model;
    uint32_t width;
    uint32_t height;
    bool initialized;
} State;

static void create_depth_texture(State* state);
static void create_buffers(State* state);
static void create_pipeline(State* state, WGPUTextureFormat surface_format);
static void on_adapter(State* state);
static void on_device(State* state);

static WGPUSurface create_surface_from_sdl(WGPUInstance instance, SDL_Window* window) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        fprintf(stderr, "Failed to get SDL window info: %s\n", SDL_GetError());
        return NULL;
    }

#ifdef _WIN32
    WGPUSurfaceSourceWindowsHWND surface_source = {
        .chain = {
            .next = NULL,
            .sType = WGPUSType_SurfaceSourceWindowsHWND,
        },
        .hwnd = info.info.win.window,
        .hinstance = info.info.win.hinstance,
    };
#elif defined(__linux__)
    WGPUSurfaceSourceXlibWindow surface_source = {
        .chain = {
            .next = NULL,
            .sType = WGPUSType_SurfaceSourceXlibWindow,
        },
        .display = info.info.x11.display,
        .window = info.info.x11.window,
    };
#elif defined(__APPLE__)
    SDL_MetalView metal_view = SDL_Metal_CreateView(window);
    void* metal_layer = SDL_Metal_GetLayer(metal_view);
    WGPUSurfaceSourceMetalLayer surface_source = {
        .chain = {
            .next = NULL,
            .sType = WGPUSType_SurfaceSourceMetalLayer,
        },
        .layer = metal_layer,
    };
#else
    #error "Unsupported platform"
#endif

    WGPUSurfaceDescriptor surface_descriptor = {
        .nextInChain = (const WGPUChainedStruct*)&surface_source,
        .label = {.data = "SDL Surface", .length = WGPU_STRLEN},
    };

    return wgpuInstanceCreateSurface(instance, &surface_descriptor);
}

static void adapter_request_callback(
    WGPURequestAdapterStatus status,
    WGPUAdapter adapter,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
) {
    (void)userdata2;
    State* state = (State*)userdata1;

    if (status != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "Failed to get adapter: %.*s\n", (int)message.length, message.data);
        return;
    }

    state->adapter = adapter;
    on_adapter(state);
}

static void device_request_callback(
    WGPURequestDeviceStatus status,
    WGPUDevice device,
    WGPUStringView message,
    void* userdata1,
    void* userdata2
) {
    (void)userdata2;
    State* state = (State*)userdata1;

    if (status != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "Failed to get device: %.*s\n", (int)message.length, message.data);
        return;
    }

    state->device = device;
    on_device(state);
}

static void init_wgpu(State* state, SDL_Window* window) {
    state->instance = wgpuCreateInstance(NULL);
    if (state->instance == NULL) {
        fprintf(stderr, "Failed to create wgpu instance\n");
        return;
    }

    state->surface = create_surface_from_sdl(state->instance, window);
    if (state->surface == NULL) {
        fprintf(stderr, "Failed to create surface\n");
        return;
    }

    WGPURequestAdapterOptions adapter_options = {
        .compatibleSurface = state->surface,
        .powerPreference = WGPUPowerPreference_HighPerformance,
    };

    WGPURequestAdapterCallbackInfo callback_info = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = adapter_request_callback,
        .userdata1 = state,
        .userdata2 = NULL,
    };

    wgpuInstanceRequestAdapter(state->instance, &adapter_options, callback_info);
}

static void on_adapter(State* state) {
    WGPURequestDeviceCallbackInfo callback_info = {
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = device_request_callback,
        .userdata1 = state,
        .userdata2 = NULL,
    };

    wgpuAdapterRequestDevice(state->adapter, NULL, callback_info);
}

static void on_device(State* state) {
    state->queue = wgpuDeviceGetQueue(state->device);

    WGPUSurfaceCapabilities caps = {0};
    WGPUStatus caps_status = wgpuSurfaceGetCapabilities(state->surface, state->adapter, &caps);
    if (caps_status != WGPUStatus_Success) {
        fprintf(stderr, "Failed to get surface capabilities\n");
        return;
    }

    WGPUTextureFormat surface_format = caps.formats[0];

    state->surface_config = (WGPUSurfaceConfiguration){
        .device = state->device,
        .usage = WGPUTextureUsage_RenderAttachment,
        .format = surface_format,
        .width = state->width,
        .height = state->height,
        .presentMode = WGPUPresentMode_Fifo,
        .alphaMode = caps.alphaModes[0],
    };
    wgpuSurfaceConfigure(state->surface, &state->surface_config);

    create_depth_texture(state);
    create_buffers(state);
    create_pipeline(state, surface_format);

    glm_mat4_identity(state->model);
    state->initialized = true;

    wgpuSurfaceCapabilitiesFreeMembers(caps);
}

static void create_depth_texture(State* state) {
    if (state->depth_texture != NULL) {
        wgpuTextureDestroy(state->depth_texture);
        wgpuTextureRelease(state->depth_texture);
    }
    if (state->depth_view != NULL) {
        wgpuTextureViewRelease(state->depth_view);
    }

    WGPUTextureDescriptor depth_texture_desc = {
        .size = {state->width, state->height, 1},
        .mipLevelCount = 1,
        .sampleCount = 1,
        .dimension = WGPUTextureDimension_2D,
        .format = DEPTH_FORMAT,
        .usage = WGPUTextureUsage_RenderAttachment,
    };

    state->depth_texture = wgpuDeviceCreateTexture(state->device, &depth_texture_desc);
    state->depth_view = wgpuTextureCreateView(state->depth_texture, NULL);
}

static void create_buffers(State* state) {
    WGPUBufferDescriptor vertex_buffer_desc = {
        .label = {.data = "Vertex Buffer", .length = WGPU_STRLEN},
        .size = sizeof(vertices),
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
        .mappedAtCreation = true,
    };
    state->vertex_buffer = wgpuDeviceCreateBuffer(state->device, &vertex_buffer_desc);
    void* vertex_data = wgpuBufferGetMappedRange(state->vertex_buffer, 0, sizeof(vertices));
    memcpy(vertex_data, vertices, sizeof(vertices));
    wgpuBufferUnmap(state->vertex_buffer);

    WGPUBufferDescriptor index_buffer_desc = {
        .label = {.data = "Index Buffer", .length = WGPU_STRLEN},
        .size = sizeof(indices),
        .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
        .mappedAtCreation = true,
    };
    state->index_buffer = wgpuDeviceCreateBuffer(state->device, &index_buffer_desc);
    void* index_data = wgpuBufferGetMappedRange(state->index_buffer, 0, sizeof(indices));
    memcpy(index_data, indices, sizeof(indices));
    wgpuBufferUnmap(state->index_buffer);

    WGPUBufferDescriptor uniform_buffer_desc = {
        .label = {.data = "Uniform Buffer", .length = WGPU_STRLEN},
        .size = sizeof(UniformBuffer),
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
    };
    state->uniform_buffer = wgpuDeviceCreateBuffer(state->device, &uniform_buffer_desc);

    WGPUBindGroupLayoutEntry bind_group_layout_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
        },
    };

    WGPUBindGroupLayoutDescriptor bind_group_layout_desc = {
        .entryCount = 1,
        .entries = &bind_group_layout_entry,
    };
    state->bind_group_layout = wgpuDeviceCreateBindGroupLayout(state->device, &bind_group_layout_desc);

    WGPUBindGroupEntry bind_group_entry = {
        .binding = 0,
        .buffer = state->uniform_buffer,
        .size = sizeof(UniformBuffer),
    };

    WGPUBindGroupDescriptor bind_group_desc = {
        .layout = state->bind_group_layout,
        .entryCount = 1,
        .entries = &bind_group_entry,
    };
    state->bind_group = wgpuDeviceCreateBindGroup(state->device, &bind_group_desc);
}

static void create_pipeline(State* state, WGPUTextureFormat surface_format) {
    WGPUShaderSourceWGSL wgsl_source = {
        .chain = {
            .next = NULL,
            .sType = WGPUSType_ShaderSourceWGSL,
        },
        .code = {.data = shader_source, .length = WGPU_STRLEN},
    };

    WGPUShaderModuleDescriptor shader_module_desc = {
        .nextInChain = (const WGPUChainedStruct*)&wgsl_source,
    };
    WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule(state->device, &shader_module_desc);

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &state->bind_group_layout,
    };
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(state->device, &pipeline_layout_desc);

    WGPUVertexAttribute vertex_attributes[2] = {
        {
            .format = WGPUVertexFormat_Float32x4,
            .offset = 0,
            .shaderLocation = 0,
        },
        {
            .format = WGPUVertexFormat_Float32x4,
            .offset = sizeof(float) * 4,
            .shaderLocation = 1,
        },
    };

    WGPUVertexBufferLayout vertex_buffer_layout = {
        .arrayStride = sizeof(Vertex),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 2,
        .attributes = vertex_attributes,
    };

    WGPUBlendState blend_state = {
        .color = {
            .srcFactor = WGPUBlendFactor_SrcAlpha,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            .operation = WGPUBlendOperation_Add,
        },
        .alpha = {
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
            .operation = WGPUBlendOperation_Add,
        },
    };

    WGPUColorTargetState color_target = {
        .format = surface_format,
        .blend = &blend_state,
        .writeMask = WGPUColorWriteMask_All,
    };

    WGPUFragmentState fragment_state = {
        .module = shader_module,
        .entryPoint = {.data = "fragment_main", .length = WGPU_STRLEN},
        .targetCount = 1,
        .targets = &color_target,
    };

    WGPUDepthStencilState depth_stencil = {
        .format = DEPTH_FORMAT,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = {
            .module = shader_module,
            .entryPoint = {.data = "vertex_main", .length = WGPU_STRLEN},
            .bufferCount = 1,
            .buffers = &vertex_buffer_layout,
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .frontFace = WGPUFrontFace_CW,
            .cullMode = WGPUCullMode_None,
        },
        .depthStencil = &depth_stencil,
        .multisample = {
            .count = 1,
            .mask = 0xFFFFFFFF,
        },
        .fragment = &fragment_state,
    };

    state->pipeline = wgpuDeviceCreateRenderPipeline(state->device, &pipeline_desc);

    wgpuShaderModuleRelease(shader_module);
    wgpuPipelineLayoutRelease(pipeline_layout);
}

static void resize(State* state, uint32_t width, uint32_t height) {
    if (!state->initialized) {
        return;
    }
    state->width = width;
    state->height = height;
    state->surface_config.width = width;
    state->surface_config.height = height;
    wgpuSurfaceConfigure(state->surface, &state->surface_config);
    create_depth_texture(state);
}

static void update(State* state, float delta_time) {
    float aspect = (float)state->width / (float)(state->height > 0 ? state->height : 1);

    mat4 projection;
    glm_perspective(glm_rad(80.0f), aspect, 0.1f, 1000.0f, projection);

    mat4 view;
    vec3 eye = {0.0f, 0.0f, 3.0f};
    vec3 center = {0.0f, 0.0f, 0.0f};
    vec3 up = {0.0f, 1.0f, 0.0f};
    glm_lookat(eye, center, up, view);

    mat4 rotation;
    vec3 axis = {0.0f, 1.0f, 0.0f};
    glm_rotate_make(rotation, glm_rad(30.0f) * delta_time, axis);
    glm_mat4_mul(rotation, state->model, state->model);

    UniformBuffer uniform;
    mat4 view_model;
    glm_mat4_mul(view, state->model, view_model);
    glm_mat4_mul(projection, view_model, uniform.mvp);

    wgpuQueueWriteBuffer(state->queue, state->uniform_buffer, 0, &uniform, sizeof(UniformBuffer));
}

static void render(State* state) {
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(state->surface, &surface_texture);

    if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return;
    }

    WGPUTextureView surface_view = wgpuTextureCreateView(surface_texture.texture, NULL);

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(state->device, NULL);

    WGPURenderPassColorAttachment color_attachment = {
        .view = surface_view,
        .loadOp = WGPULoadOp_Clear,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = {0.19, 0.24, 0.42, 1.0},
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
    };

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = state->depth_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
    };

    WGPURenderPassDescriptor render_pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment,
    };

    WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder, &render_pass_desc);

    wgpuRenderPassEncoderSetPipeline(render_pass, state->pipeline);
    wgpuRenderPassEncoderSetBindGroup(render_pass, 0, state->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, state->vertex_buffer, 0, sizeof(vertices));
    wgpuRenderPassEncoderSetIndexBuffer(render_pass, state->index_buffer, WGPUIndexFormat_Uint32, 0, sizeof(indices));
    wgpuRenderPassEncoderDrawIndexed(render_pass, 3, 1, 0, 0, 0);

    wgpuRenderPassEncoderEnd(render_pass);
    wgpuRenderPassEncoderRelease(render_pass);

    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, NULL);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(state->queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);

    wgpuSurfacePresent(state->surface);
    wgpuTextureViewRelease(surface_view);
    wgpuTextureRelease(surface_texture.texture);
}

static void cleanup(State* state) {
    if (state->pipeline) wgpuRenderPipelineRelease(state->pipeline);
    if (state->bind_group) wgpuBindGroupRelease(state->bind_group);
    if (state->bind_group_layout) wgpuBindGroupLayoutRelease(state->bind_group_layout);
    if (state->uniform_buffer) wgpuBufferRelease(state->uniform_buffer);
    if (state->index_buffer) wgpuBufferRelease(state->index_buffer);
    if (state->vertex_buffer) wgpuBufferRelease(state->vertex_buffer);
    if (state->depth_view) wgpuTextureViewRelease(state->depth_view);
    if (state->depth_texture) {
        wgpuTextureDestroy(state->depth_texture);
        wgpuTextureRelease(state->depth_texture);
    }
    if (state->queue) wgpuQueueRelease(state->queue);
    if (state->device) wgpuDeviceRelease(state->device);
    if (state->adapter) wgpuAdapterRelease(state->adapter);
    if (state->surface) wgpuSurfaceRelease(state->surface);
    if (state->instance) wgpuInstanceRelease(state->instance);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "C/WGPU Triangle",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        800,
        600,
        SDL_WINDOW_RESIZABLE
    );

    if (window == NULL) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    State state = {0};
    state.width = 800;
    state.height = 600;
    init_wgpu(&state, window);

    Uint64 last_time = SDL_GetPerformanceCounter();
    Uint64 frequency = SDL_GetPerformanceFrequency();
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        running = false;
                    }
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        uint32_t new_width = (uint32_t)event.window.data1;
                        uint32_t new_height = (uint32_t)event.window.data2;
                        if (new_width > 0 && new_height > 0) {
                            resize(&state, new_width, new_height);
                        }
                    }
                    break;
            }
        }

        if (!state.initialized) {
            continue;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float delta_time = (float)(now - last_time) / (float)frequency;
        last_time = now;

        update(&state, delta_time);
        render(&state);
    }

    cleanup(&state);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
