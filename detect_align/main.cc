#include <cstdio>
#include <cstdlib>
#include <cstring>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#include "stb_image.h"
#include "retinaface.h"
#include "image_utils.h"

static int load_photo(const char *path, image_buffer_t *image)
{
    const char *extension = strrchr(path, '.');
    if (extension && (strcmp(extension, ".bmp") == 0 || strcmp(extension, ".BMP") == 0)) {
        int width = 0, height = 0, channels = 0;
        unsigned char *pixels = stbi_load(path, &width, &height, &channels, 3);
        if (!pixels || width <= 0 || height <= 0 || width > 8192 || height > 8192) {
            fprintf(stderr, "BMP decode failed: %s\n", stbi_failure_reason());
            stbi_image_free(pixels);
            return -1;
        }
        image->width = width;
        image->height = height;
        image->width_stride = width;
        image->height_stride = height;
        image->format = IMAGE_FORMAT_RGB888;
        image->virt_addr = pixels;
        image->size = width * height * 3;
        return 0;
    }
    return read_image(path, image);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: %s detector.rknn image.bmp|image.jpg\n", argv[0]); return 1; }
    rknn_app_context_t context = {};
    image_buffer_t image = {};
    retinaface_result result = {};
    int status = 1;
    if (init_retinaface_model(argv[1], &context) != 0) return 1;
    if (load_photo(argv[2], &image) != 0 || !image.virt_addr || image.format != IMAGE_FORMAT_RGB888) {
        fprintf(stderr, "Input must be a readable three-channel RGB image\n"); goto cleanup;
    }
    if (inference_retinaface_model(&context, &image, &result) != 0) {
        fprintf(stderr, "Face detection failed\n"); goto cleanup;
    }
    if (result.count != 1) { fprintf(stderr, "Expected one face, found %d\n", result.count); goto cleanup; }
    {
        const retinaface_object_t &face = result.object[0];
        printf("Face score: %.4f; image: %d x %d\n", face.score, image.width, image.height);
        FILE *points = fopen("detected_points.txt", "w");
        if (!points) { perror("detected_points.txt"); goto cleanup; }
        for (int i = 0; i < 5; ++i) {
            printf("point %d: %d %d\n", i, face.ponit[i].x, face.ponit[i].y);
            fprintf(points, "%d %d\n", face.ponit[i].x, face.ponit[i].y);
        }
        if (fclose(points) != 0) { perror("close points"); goto cleanup; }
        FILE *rgb = fopen("detected_input.rgb", "wb");
        if (!rgb) { perror("detected_input.rgb"); goto cleanup; }
        size_t bytes = (size_t)image.width * image.height * 3;
        size_t written = fwrite(image.virt_addr, 1, bytes, rgb);
        int close_result = fclose(rgb);
        if (written != bytes || close_result != 0) { fprintf(stderr, "RGB write failed\n"); goto cleanup; }
        printf("Saved detected_input.rgb and detected_points.txt\n"); status = 0;
    }
cleanup:
    free(image.virt_addr);
    release_retinaface_model(&context);
    return status;
}
