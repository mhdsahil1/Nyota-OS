/* =============================================================================
 * Nyota OS SDK — Example CLI Application (hello_cli.c)
 * Demonstrates basic Nyota C standard library APIs: formatted I/O and malloc.
 * =========================================================================== */

#include <libnyota.h>

int main(int argc, char **argv) {
    printf("=========================================\n");
    printf("  Hello from Nyota OS Native CLI App!   \n");
    printf("=========================================\n\n");

    printf("Command-line arguments (%d):\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }

    /* Demonstrate heap allocation */
    char *buffer = (char *)malloc(128);
    if (!buffer) {
        printf("[ERROR] Memory allocation failed!\n");
        return 1;
    }

    snprintf(buffer, 128, "Dynamic buffer allocated at %p", buffer);
    printf("\n%s\n", buffer);
    free(buffer);

    printf("\nProcess PID: %d\n", getpid());
    return 0;
}
