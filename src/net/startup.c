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
static constexpr uint32_t PROTOCOL_MAJOR_VERSION = 3;
static constexpr size_t ENCRYPTION_REQUEST_SIZE = sizeof(int32_t);
static constexpr size_t CANCEL_REQUEST_SIZE = 3 * sizeof(int32_t);
static constexpr uint8_t ENCRYPTION_DECLINED = 'N';

static int accept_connection(int32_t code, wire_writer_t *out, startup_result_t *result)
{
    if (((uint32_t) code >> VERSION_MAJOR_SHIFT) != PROTOCOL_MAJOR_VERSION) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (protocol_put_authentication_ok(out) != 0 ||
        protocol_put_ready_for_query(out, TRANSACTION_IDLE) != 0) {
        return -1;
    }
    *result = STARTUP_DONE;
    return 0;
}

static int decline_encryption(size_t size, wire_writer_t *out, startup_result_t *result)
{
    if (size != ENCRYPTION_REQUEST_SIZE) {
        errno = EBADMSG;
        return -1;
    }
    if (wire_writer_put_u8(out, ENCRYPTION_DECLINED) != 0) {
        return -1;
    }
    *result = STARTUP_CONTINUE;
    return 0;
}

static int close_for_cancel(size_t size, startup_result_t *result)
{
    if (size != CANCEL_REQUEST_SIZE) {
        errno = EBADMSG;
        return -1;
    }
    *result = STARTUP_CLOSE;
    return 0;
}

static int reply_to_packet(int32_t code, size_t size, wire_writer_t *out, startup_result_t *result)
{
    if (code == SSL_REQUEST_CODE || code == GSS_ENCRYPTION_REQUEST_CODE) {
        return decline_encryption(size, out, result);
    }
    if (code == CANCEL_REQUEST_CODE) {
        return close_for_cancel(size, result);
    }
    return accept_connection(code, out, result);
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
    int status = reply_to_packet(code, size, out, result);
    if (status != 0) {
        wire_writer_truncate(out, start);
    }
    return status;
}
