#include "common.h"
#include "main.h"

#pragma once

enum Http_Method {
    Http_Get,
    Http_Put
};

void platform_early_init();
bool platform_init();
void platform_loop();
void platform_render_frame();

float platform_get_pixel_ratio();

u64 platform_get_app_time_precise();
float platform_get_delta_time_ms(u64 delta_to);

void platform_open_url(String& permalink);

void platform_api_request(Request_Id request_id, String url, Http_Method method, void* data = NULL);
void platform_load_remote_image(Request_Id request_id, String full_url);
void platform_local_storage_set(const char* key, String value); // TODO bad definition...
char* platform_local_storage_get(const char* key); // You own the memory!

void platform_load_png_async(Array<u8> in, Image_Load_Callback callback);

u32 platform_make_texture(u32 width, u32 height, u8* pixels);