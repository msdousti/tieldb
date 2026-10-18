/**
 * Handling of the packets a client sends before the normal message flow.
 */

#include "net/startup.h"

#include <errno.h>

static constexpr unsigned VERSION_MAJOR_SHIFT = 16;
static constexpr int32_t REQUEST_CODE_MAJOR = 1234;
static constexpr int32_t SSL_REQUEST_CODE = (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5679;
static constexpr int32_t GSS_ENCRYPTION_REQUEST_CODE =
    (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5680;
static constexpr int32_t CANCEL_REQUEST_CODE = (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5678;
static constexpr uint8_t ENCRYPTION_DECLINED = 'N';

typedef struct {
    const char *name;
    const char *value;
} server_parameter_t;

// psql reads server_version to decide which features to use, and warns when its major version
// differs from its own. TielDB's own version would always trigger that, so the value claims the
// Postgres version of the psql we develop against and names TielDB in parentheses
static const server_parameter_t SERVER_PARAMETERS[] = {
    {"server_version", "18.0 (TielDB 0.1.0)"},
    {"client_encoding", "UTF8"},
};

static int put_server_parameters(wire_writer_t *out)
{
    int status = 0;
    for (size_t i = 0; i < sizeof(SERVER_PARAMETERS) / sizeof(SERVER_PARAMETERS[0]) && status == 0;
         i++) {
        status = protocol_put_parameter_status(out, SERVER_PARAMETERS[i].name,
                                               SERVER_PARAMETERS[i].value);
    }
    return status;
}

static int accept_connection(wire_writer_t *out, startup_result_t *result)
{
    if (protocol_put_authentication_ok(out) != 0 || put_server_parameters(out) != 0 ||
        protocol_put_ready_for_query(out, TRANSACTION_IDLE) != 0) {
        return -1;
    }
    *result = STARTUP_DONE;
    return 0;
}

static int decline_encryption(wire_writer_t *out, startup_result_t *result)
{
    if (wire_writer_put_u8(out, ENCRYPTION_DECLINED) != 0) {
        return -1;
    }
    *result = STARTUP_CONTINUE;
    return 0;
}

static int reply_to_packet(int32_t code, wire_writer_t *out, startup_result_t *result)
{
    if (code == SSL_REQUEST_CODE || code == GSS_ENCRYPTION_REQUEST_CODE) {
        return decline_encryption(out, result);
    }
    if (code == CANCEL_REQUEST_CODE) {
        *result = STARTUP_CLOSE;
        return 0;
    }
    return accept_connection(out, result);
}

static int read_code(const void *packet, size_t size, int32_t *code)
{
    wire_reader_t *reader = wire_reader_create(packet, size);
    if (reader == nullptr) {
        return -1;
    }
    int status = wire_reader_get_i32(reader, code);
    wire_reader_destroy(reader);
    return status;
}

int startup_handle_packet(const void *packet, size_t size, wire_writer_t *out,
                          startup_result_t *result)
{
    int32_t code = 0;
    if (out == nullptr || result == nullptr) {
        errno = EINVAL;
        return -1;
    }
    if (read_code(packet, size, &code) != 0) {
        return -1;
    }
    size_t start = wire_writer_size(out);
    int status = reply_to_packet(code, out, result);
    if (status != 0) {
        wire_writer_truncate(out, start);
    }
    return status;
}
