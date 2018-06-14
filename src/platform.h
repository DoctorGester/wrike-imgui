#include "common.h"
#include "main.h"

#pragma once

bool platform_init();
void platform_loop();

// TODO investigate if those 2 are superfluous and could be handled in platform_loop
void platform_begin_frame();
void platform_end_frame();

float platform_get_pixel_ratio();
float platform_get_app_time_ms();

void platform_open_in_wrike(String& permalink);

void platform_api_get(Request_Id& request_id, char* url);
void platform_get(Request_Id& request_id, char* full_url);
void platform_local_storage_set(const char* key, String& value); // TODO bad definition...
char* platform_local_storage_get(const char* key); // You own the memory!