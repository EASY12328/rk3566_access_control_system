/* Extract a normalized SFace feature from one aligned RGB face. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rknn_api.h"

#define RGB_BYTES (112 * 112 * 3)
#define FEATURE_SIZE 128

static int read_exact(const char *path, void *buffer, size_t bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return -1; }
    size_t count = fread(buffer, 1, bytes, file);
    int extra = fgetc(file);
    int failed = count != bytes || extra != EOF || ferror(file);
    fclose(file);
    if (failed) fprintf(stderr, "%s: expected exactly %zu bytes\n", path, bytes);
    return failed ? -1 : 0;
}

static int normalize(float *feature)
{
    double squared = 0;
    for (int i = 0; i < FEATURE_SIZE; ++i) {
        if (!isfinite(feature[i])) return -1;
        squared += (double)feature[i] * feature[i];
    }
    if (squared < 1e-24) return -1;
    double norm = sqrt(squared);
    for (int i = 0; i < FEATURE_SIZE; ++i) feature[i] /= norm;
    return 0;
}

static int infer(rknn_context context, unsigned char *rgb, float *feature)
{
    rknn_input input = {0};
    input.index = 0;
    input.buf = rgb;
    input.size = RGB_BYTES;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    int result = rknn_inputs_set(context, 1, &input);
    if (result != RKNN_SUCC) { fprintf(stderr, "inputs_set: %d\n", result); return -1; }
    result = rknn_run(context, NULL);
    if (result != RKNN_SUCC) { fprintf(stderr, "rknn_run: %d\n", result); return -1; }
    rknn_output output = {0};
    output.index = 0;
    output.want_float = 1;
    result = rknn_outputs_get(context, 1, &output, NULL);
    if (result != RKNN_SUCC) { fprintf(stderr, "outputs_get: %d\n", result); return -1; }
    int valid = output.buf && output.size == FEATURE_SIZE * sizeof(float);
    if (valid) memcpy(feature, output.buf, FEATURE_SIZE * sizeof(float));
    result = rknn_outputs_release(context, 1, &output);
    if (!valid || result != RKNN_SUCC) {
        fprintf(stderr, "Invalid output size or outputs_release failed: %d\n", result);
        return -1;
    }
    return normalize(feature);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s model.rknn aligned.rgb feature.f32\n", argv[0]);
        return 1;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) { perror(argv[1]); return 1; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 1; }
    long length = ftell(file);
    fclose(file);
    if (length <= 0 || (unsigned long)length > UINT32_MAX) {
        fprintf(stderr, "Invalid model size\n"); return 1;
    }
    void *model = malloc((size_t)length);
    if (!model) { perror("malloc model"); return 1; }
    if (read_exact(argv[1], model, (size_t)length) != 0) { free(model); return 1; }
    unsigned char rgb[RGB_BYTES];
    if (read_exact(argv[2], rgb, sizeof(rgb)) != 0) { free(model); return 1; }
    rknn_context context = 0;
    int ret = rknn_init(&context, model, (uint32_t)length, 0, NULL);
    if (ret != RKNN_SUCC) { fprintf(stderr, "rknn_init: %d\n", ret); free(model); return 1; }
    int status = 1;
    rknn_input_output_num count = {0};
    rknn_tensor_attr input = {0}, output = {0};
    if (rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &count, sizeof(count)) != RKNN_SUCC ||
        count.n_input != 1 || count.n_output != 1 ||
        rknn_query(context, RKNN_QUERY_INPUT_ATTR, &input, sizeof(input)) != RKNN_SUCC ||
        rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &output, sizeof(output)) != RKNN_SUCC ||
        input.n_elems != RGB_BYTES || output.n_elems != FEATURE_SIZE) {
        fprintf(stderr, "Unexpected model layout\n"); goto cleanup;
    }
    {
        float feature[FEATURE_SIZE];
        if (infer(context, rgb, feature) != 0) { fprintf(stderr, "Inference failed\n"); goto cleanup; }
        file = fopen(argv[3], "wb");
        if (!file) { perror(argv[3]); goto cleanup; }
        size_t written = fwrite(feature, sizeof(float), FEATURE_SIZE, file);
        int close_result = fclose(file);
        if (written != FEATURE_SIZE || close_result != 0) { fprintf(stderr, "Feature write failed\n"); goto cleanup; }
        printf("Saved normalized 128-dimensional feature: %s\n", argv[3]);
        status = 0;
    }
cleanup:
    rknn_destroy(context);
    free(model);
    return status;
}
