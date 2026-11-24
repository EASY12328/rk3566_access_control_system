/* Five-point similarity alignment for an interleaved RGB image. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define OUTPUT_SIZE 112
static const double target[5][2] = {
    {38.2946, 51.6963}, {73.5318, 51.5014}, {56.0252, 71.7366},
    {41.5493, 92.3655}, {70.7299, 92.2041}
};

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "Usage: %s input.rgb width height points.txt output.rgb\n", argv[0]);
        return 1;
    }
    char *end;
    long width = strtol(argv[2], &end, 10);
    if (*end || width < 1 || width > 8192) return 1;
    long height = strtol(argv[3], &end, 10);
    if (*end || height < 1 || height > 8192) return 1;
    size_t bytes = (size_t)width * (size_t)height * 3;
    unsigned char *image = malloc(bytes);
    if (!image) { perror("malloc"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); free(image); return 1; }
    size_t count = fread(image, 1, bytes, f);
    int extra = fgetc(f);
    int error = ferror(f);
    fclose(f);
    if (count != bytes || extra != EOF || error) {
        fprintf(stderr, "Input RGB must have exactly %zu bytes\n", bytes);
        free(image); return 1;
    }
    double points[5][2], sx = 0, sy = 0, tx = 0, ty = 0;
    f = fopen(argv[4], "r");
    if (!f) { perror(argv[4]); free(image); return 1; }
    for (int i = 0; i < 5; ++i) {
        if (fscanf(f, "%lf %lf", &points[i][0], &points[i][1]) != 2 ||
            !isfinite(points[i][0]) || !isfinite(points[i][1]) ||
            points[i][0] < 0 || points[i][0] >= width ||
            points[i][1] < 0 || points[i][1] >= height) {
            fprintf(stderr, "Invalid landmark %d\n", i); fclose(f); free(image); return 1;
        }
        sx += points[i][0]; sy += points[i][1];
        tx += target[i][0]; ty += target[i][1];
    }
    fclose(f);
    sx /= 5; sy /= 5; tx /= 5; ty /= 5;
    double dot = 0, cross = 0, variance = 0;
    for (int i = 0; i < 5; ++i) {
        double x = points[i][0] - sx, y = points[i][1] - sy;
        double u = target[i][0] - tx, v = target[i][1] - ty;
        dot += x * u + y * v;
        cross += x * v - y * u;
        variance += x * x + y * y;
    }
    if (variance < 1e-8) { fprintf(stderr, "Degenerate landmarks\n"); free(image); return 1; }
    double a = dot / variance, b = cross / variance;
    double c = tx - a * sx + b * sy, d = ty - b * sx - a * sy;
    double determinant = a * a + b * b;
    if (determinant < 1e-12) { fprintf(stderr, "Invalid transform\n"); free(image); return 1; }
    unsigned char output[OUTPUT_SIZE * OUTPUT_SIZE * 3];
    for (int y = 0; y < OUTPUT_SIZE; ++y) {
        for (int x = 0; x < OUTPUT_SIZE; ++x) {
            double px = (a * (x - c) + b * (y - d)) / determinant;
            double py = (-b * (x - c) + a * (y - d)) / determinant;
            int x0 = (int)floor(px), y0 = (int)floor(py);
            double dx = px - x0, dy = py - y0;
            for (int channel = 0; channel < 3; ++channel) {
                double value = 0;
                for (int row = 0; row < 2; ++row) {
                    for (int col = 0; col < 2; ++col) {
                        int ix = x0 + col, iy = y0 + row;
                        if (ix >= 0 && ix < width && iy >= 0 && iy < height) {
                            double weight = (col ? dx : 1 - dx) * (row ? dy : 1 - dy);
                            value += weight * image[((size_t)iy * width + ix) * 3 + channel];
                        }
                    }
                }
                output[(y * OUTPUT_SIZE + x) * 3 + channel] = (unsigned char)lround(value);
            }
        }
    }
    free(image);
    f = fopen(argv[5], "wb");
    if (!f) { perror(argv[5]); return 1; }
    size_t written = fwrite(output, 1, sizeof(output), f);
    int close_result = fclose(f);
    if (written != sizeof(output) || close_result != 0) { fprintf(stderr, "Output write failed\n"); return 1; }
    printf("Aligned RGB image saved: %s (%dx%d)\n", argv[5], OUTPUT_SIZE, OUTPUT_SIZE);
    return 0;
}
