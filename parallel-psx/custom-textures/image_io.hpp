#pragma once

#include <memory>
#include <stdint.h>

struct RGBAImage {
    uint8_t* data;
    int width;
    int height;

    ~RGBAImage();
};

std::unique_ptr<RGBAImage> load_image(const char *path);
bool write_image(const char *path, int width, int height, const void *data);