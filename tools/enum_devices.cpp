#include <stdio.h>
#include <string.h>
#include "llama.h"
#include "ggml-backend.h"

int main() {
    llama_backend_init();
    size_t count = ggml_backend_dev_count();
    printf("Found %zu ggml devices:\n", count);
    for (size_t i = 0; i < count; ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const char* name = ggml_backend_dev_name(dev);
        const char* desc = ggml_backend_dev_description(dev);
        enum ggml_backend_dev_type type = ggml_backend_dev_type(dev);
        size_t free_mem = 0, total_mem = 0;
        ggml_backend_dev_memory(dev, &free_mem, &total_mem);
        printf("  Device %zu:\n", i);
        printf("    Name: %s\n", name ? name : "null");
        printf("    Desc: %s\n", desc ? desc : "null");
        printf("    Type: %d (0=CPU, 1=GPU, 2=IGPU, 3=ACCEL)\n", (int)type);
        printf("    Memory: %zu MB free / %zu MB total\n", free_mem / (1024 * 1024), total_mem / (1024 * 1024));
    }
    return 0;
}
