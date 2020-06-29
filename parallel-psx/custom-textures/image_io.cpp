#include "image_io.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb/stb_image.h"

RGBAImage::~RGBAImage() {
    if (this->data != nullptr) {
        stbi_image_free(this->data);
    }
}

std::unique_ptr<RGBAImage> load_image(const char *path) {
    auto ptr = std::unique_ptr<RGBAImage>(new RGBAImage);
    int channels;
    ptr->data = stbi_load(path, &ptr->width, &ptr->height, &channels, 4);
    return ptr;
}
bool write_image(const char *path, int width, int height, const void *data) {
    return stbi_write_png(path, width, height, 4, data, 4 * width);
}