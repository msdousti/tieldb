/**
 * Handling of the packets a client sends before the normal message flow.
 */

#include "net/startup.h"

#include <errno.h>

static constexpr int32_t SSL_REQUEST_CODE = 80877103;
static constexpr int32_t GSS_ENCRYPTION_REQUEST_CODE = 80877104;
static constexpr int32_t CANCEL_REQUEST_CODE = 80877102;
static constexpr int32_t PROTOCOL_MAJOR_VERSION = 3;
static constexpr unsigned VERSION_MAJOR_SHIFT = 16;
static constexpr uint8_t ENCRYPTION_DECLINED = 'N';

static int accept_connection(wire_writer_t *out)
{
    size_t start = wire_writer_size(out);
    if (protocol_put_authentication_ok(out) != 0 ||
        protocol_put_ready_for_query(out, TRANSACTION_IDLE) != 0) {
        wire_writer_truncate(out, start);
        return -1;
    }
    return 0;
}

static int reply_to_code(int32_t code, wire_writer_t *out, startup_result_t *result)
{
    if (code == SSL_REQUEST_CODE || code == GSS_ENCRYPTION_REQUEST_CODE) {
        *result = STARTUP_CONTINUE;
        return wire_writer_put_u8(out, ENCRYPTION_DECLINED);
    }
    if (code == CANCEL_REQUEST_CODE) {
        *result = STARTUP_CLOSE;
        return 0;
    }
    if ((code >> VERSION_MAJOR_SHIFT) != PROTOCOL_MAJOR_VERSION) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    *result = STARTUP_DONE;
    return accept_connection(out);
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
    return reply_to_code(code, out, result);
}
