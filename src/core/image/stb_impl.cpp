/**
 * @file stb_impl.cpp
 * @brief STB 库的独立编译单元 — 避免在 Image.cpp 中定义 IMPLEMENTATION 宏
 *
 * 将 stb_image / stb_image_write 的实现集中在此文件，
 * 确保全局只有一份 STB 函数定义，消除 ODR 风险。
 */

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS (16384)

#include <stb_image.h>
#include <stb_image_write.h>
