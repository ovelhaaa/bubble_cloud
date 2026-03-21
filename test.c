#include <stdio.h>
#include <string.h>

const char* FindJsonKey(const char* json_str, const char* key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    size_t key_len = strlen(search_key);

    const char* pos = json_str;
    while ((pos = strstr(pos, search_key)) != NULL) {
        // Look ahead to ensure the matched string is used as a JSON key (followed by ':')
        const char* check_pos = pos + key_len;
        while (*check_pos == ' ' || *check_pos == '\t' || *check_pos == '\r' || *check_pos == '\n') {
            check_pos++;
        }
        if (*check_pos == ':') {
            return check_pos + 1; // Return the position immediately after the colon
        }
        pos += key_len; // Continue searching
    }
    return NULL;
}

int main() {
    const char* json = "{\n  \"comment\": \"the parameter 'density_burst' is important\",\n  \"density_burst\": 10.0\n}";
    const char* found = FindJsonKey(json, "density_burst");
    if (found) {
        printf("Found density_burst: '%s'\n", found);
    } else {
        printf("Not found\n");
    }
    return 0;
}
