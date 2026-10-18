/**
 * Command-line options of the tieldb server.
 */

#include "options.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static constexpr uint16_t DEFAULT_PORT = 5432;
static constexpr int DECIMAL_BASE = 10;
static constexpr char OPTION_PORT[] = "--port";
static constexpr char OPTION_ONCE[] = "--once";

static int parse_port(const char *text, uint16_t *port)
{
    char *end = nullptr;
    long value = strtol(text, &end, DECIMAL_BASE);
    if (end == text || *end != '\0' || value < 0 || value > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    *port = (uint16_t) value;
    return 0;
}

static int parse_option(int argc, char *const *argv, int *index, options_t *options)
{
    const char *argument = argv[*index];
    if (strcmp(argument, OPTION_ONCE) == 0) {
        options->max_connections = 1;
        return 0;
    }
    if (strcmp(argument, OPTION_PORT) == 0 && *index + 1 < argc) {
        *index += 1;
        return parse_port(argv[*index], &options->port);
    }
    errno = EINVAL;
    return -1;
}

int options_parse(int argc, char *const *argv, options_t *options)
{
    *options = (options_t){.port = DEFAULT_PORT, .max_connections = SIZE_MAX};
    int status = 0;
    for (int i = 1; i < argc && status == 0; i++) {
        status = parse_option(argc, argv, &i, options);
    }
    return status;
}
