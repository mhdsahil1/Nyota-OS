/* =============================================================================
 * Nyota OS — Userspace Environment Variables (env.c)
 * Standard getenv, setenv, unsetenv implementations for libnyota.
 * =========================================================================== */

#include "libnyota.h"

#define MAX_ENV_VARS 64
#define MAX_ENV_LEN  256

static char env_storage[MAX_ENV_VARS][MAX_ENV_LEN];
static char *env_ptrs[MAX_ENV_VARS + 1];
static bool env_initialized = false;

static void env_init(void) {
    if (env_initialized) return;
    env_initialized = true;

    int idx = 0;
    if (environ != NULL) {
        for (int i = 0; environ[i] != NULL && idx < MAX_ENV_VARS - 1; i++) {
            strncpy(env_storage[idx], environ[i], MAX_ENV_LEN - 1);
            env_storage[idx][MAX_ENV_LEN - 1] = '\0';
            env_ptrs[idx] = env_storage[idx];
            idx++;
        }
    }
    env_ptrs[idx] = NULL;
    environ = env_ptrs;
}

char *getenv(const char *name) {
    if (!name) return NULL;
    env_init();
    size_t nlen = strlen(name);

    if (environ == NULL) return NULL;
    for (int i = 0; environ[i] != NULL; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=') {
            return &environ[i][nlen + 1];
        }
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || name[0] == '\0' || strchr(name, '=') != NULL || !value) {
        return -1;
    }
    env_init();

    size_t nlen = strlen(name);
    int found_idx = -1;
    int count = 0;

    for (int i = 0; environ[i] != NULL; i++) {
        count++;
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=') {
            found_idx = i;
            break;
        }
    }

    if (found_idx != -1) {
        if (!overwrite) return 0;
        char buf[MAX_ENV_LEN];
        strncpy(buf, name, sizeof(buf) - 1);
        strcat(buf, "=");
        strncat(buf, value, sizeof(buf) - strlen(buf) - 1);
        strncpy(environ[found_idx], buf, MAX_ENV_LEN - 1);
        environ[found_idx][MAX_ENV_LEN - 1] = '\0';
        return 0;
    }

    if (count >= MAX_ENV_VARS - 1) return -1;

    char buf[MAX_ENV_LEN];
    strncpy(buf, name, sizeof(buf) - 1);
    strcat(buf, "=");
    strncat(buf, value, sizeof(buf) - strlen(buf) - 1);
    strncpy(env_storage[count], buf, MAX_ENV_LEN - 1);
    env_storage[count][MAX_ENV_LEN - 1] = '\0';
    env_ptrs[count] = env_storage[count];
    env_ptrs[count + 1] = NULL;
    environ = env_ptrs;

    return 0;
}

int unsetenv(const char *name) {
    if (!name || name[0] == '\0' || strchr(name, '=') != NULL) {
        return -1;
    }
    env_init();

    size_t nlen = strlen(name);
    for (int i = 0; environ[i] != NULL; i++) {
        if (strncmp(environ[i], name, nlen) == 0 && environ[i][nlen] == '=') {
            int j = i;
            while (environ[j] != NULL) {
                environ[j] = environ[j + 1];
                j++;
            }
            return 0;
        }
    }
    return 0;
}
