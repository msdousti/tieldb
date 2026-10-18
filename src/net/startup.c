/**
 * Handling of the packets a client sends before the normal message flow.
 */

#include "net/startup.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static constexpr unsigned VERSION_MAJOR_SHIFT = 16;
static constexpr int32_t REQUEST_CODE_MAJOR = 1234;
static constexpr int32_t SSL_REQUEST_CODE = (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5679;
static constexpr int32_t GSS_ENCRYPTION_REQUEST_CODE =
    (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5680;
static constexpr int32_t CANCEL_REQUEST_CODE = (REQUEST_CODE_MAJOR << VERSION_MAJOR_SHIFT) | 5678;
static constexpr uint8_t ENCRYPTION_DECLINED = 'N';
static constexpr char USER_PARAMETER[] = "user";
static constexpr char DATABASE_PARAMETER[] = "database";

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

static void copy_name(char *destination, const char *source)
{
    (void) snprintf(destination, SESSION_NAME_CAPACITY, "%s", source);
}

static void apply_parameter(session_info_t *session, const char *name, const char *value)
{
    if (strcmp(name, USER_PARAMETER) == 0) {
        copy_name(session->user, value);
    }
    if (strcmp(name, DATABASE_PARAMETER) == 0) {
        copy_name(session->database, value);
    }
}

// Returns 1 after reading a parameter, 0 at the end of the list, -1 if the packet is cut short
static int read_parameter(wire_reader_t *reader, session_info_t *session)
{
    const char *name = nullptr;
    const char *value = nullptr;
    if (wire_reader_get_cstring(reader, &name) != 0) {
        return -1;
    }
    if (name[0] == '\0') {
        return 0;
    }
    if (wire_reader_get_cstring(reader, &value) != 0) {
        return -1;
    }
    apply_parameter(session, name, value);
    return 1;
}

static int read_session(wire_reader_t *reader, session_info_t *session)
{
    int status = 0;
    do {
        status = read_parameter(reader, session);
    } while (status > 0);
    if (session->database[0] == '\0') {
        copy_name(session->database, session->user);
    }
    return status;
}

static int accept_connection(wire_reader_t *reader, wire_writer_t *out, startup_outcome_t *outcome)
{
    session_info_t session = {};
    if (read_session(reader, &session) != 0 || protocol_put_authentication_ok(out) != 0 ||
        put_server_parameters(out) != 0 ||
        protocol_put_ready_for_query(out, TRANSACTION_IDLE) != 0) {
        return -1;
    }
    outcome->session = session;
    outcome->result = STARTUP_DONE;
    return 0;
}

static int decline_encryption(wire_writer_t *out, startup_outcome_t *outcome)
{
    if (wire_writer_put_u8(out, ENCRYPTION_DECLINED) != 0) {
        return -1;
    }
    outcome->result = STARTUP_CONTINUE;
    return 0;
}

static int reply_to_packet(int32_t code, wire_reader_t *reader, wire_writer_t *out,
                           startup_outcome_t *outcome)
{
    if (code == SSL_REQUEST_CODE || code == GSS_ENCRYPTION_REQUEST_CODE) {
        return decline_encryption(out, outcome);
    }
    if (code == CANCEL_REQUEST_CODE) {
        outcome->result = STARTUP_CLOSE;
        return 0;
    }
    return accept_connection(reader, out, outcome);
}

static int reply_or_roll_back(int32_t code, wire_reader_t *reader, wire_writer_t *out,
                              startup_outcome_t *outcome)
{
    size_t start = wire_writer_size(out);
    int status = reply_to_packet(code, reader, out, outcome);
    if (status != 0) {
        wire_writer_truncate(out, start);
    }
    return status;
}

int startup_handle_packet(const void *packet, size_t size, wire_writer_t *out,
                          startup_outcome_t *outcome)
{
    int32_t code = 0;
    if (out == nullptr || outcome == nullptr) {
        errno = EINVAL;
        return -1;
    }
    wire_reader_t *reader = wire_reader_create(packet, size);
    if (reader == nullptr) {
        return -1;
    }
    int status = wire_reader_get_i32(reader, &code);
    if (status == 0) {
        status = reply_or_roll_back(code, reader, out, outcome);
    }
    wire_reader_destroy(reader);
    return status;
}
