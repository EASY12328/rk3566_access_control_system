/* Compare two normalized or unnormalized 128-dimensional face features. */
#include <math.h>
#include <stdio.h>

#define FEATURE_SIZE 128

static int read_feature(const char *path, float *feature)
{
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return -1; }
    size_t count = fread(feature, sizeof(float), FEATURE_SIZE, file);
    int extra = fgetc(file);
    int error = ferror(file);
    fclose(file);
    if (count != FEATURE_SIZE || extra != EOF || error) {
        fprintf(stderr, "Expected 128 float32 values: %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s reference.f32 candidate.f32\n", argv[0]);
        return 1;
    }
    float reference[FEATURE_SIZE], candidate[FEATURE_SIZE];
    if (read_feature(argv[1], reference) || read_feature(argv[2], candidate)) return 1;
    double dot = 0, norm1 = 0, norm2 = 0;
    for (int i = 0; i < FEATURE_SIZE; ++i) {
        if (!isfinite(reference[i]) || !isfinite(candidate[i])) {
            fprintf(stderr, "Feature contains invalid values\n"); return 1;
        }
        dot += (double)reference[i] * candidate[i];
        norm1 += (double)reference[i] * reference[i];
        norm2 += (double)candidate[i] * candidate[i];
    }
    if (norm1 < 1e-24 || norm2 < 1e-24) {
        fprintf(stderr, "Feature vector is zero\n"); return 1;
    }
    printf("Cosine similarity: %.6f\n", dot / sqrt(norm1 * norm2));
    return 0;
}
