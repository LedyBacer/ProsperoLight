/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gs_http.h"
#include "gs_errors.h"
#include "gs_log.h"

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define HTTP_TIMEOUT_DEFAULT_MS 7000u
/* Box art is the largest response: a library's 600x900 PNG cover often passes 1 MiB. */
#define HTTP_MAX_RESPONSE (8u * 1024u * 1024u)
#define HTTP_REQUEST_CAPACITY 12288u

typedef struct
{
    int socket;
    int use_tls;
    int tls_initialized;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
} http_connection_t;

static client_identity_t *http_identity;
static unsigned http_timeout_ms = HTTP_TIMEOUT_DEFAULT_MS;
static int http_verbose;
static _Atomic int http_active_socket = -1;
static _Atomic int http_interrupted;
static char http_connect_error[160];

static void set_connect_error(const char *stage, int error)
{
    snprintf(http_connect_error, sizeof(http_connect_error),
             "Could not connect to Sunshine (%s: %d)", stage, error);
    LOGE("Sunshine TCP connection failed at %s: %d", stage, error);
}

void http_init(client_identity_t *identity, int verbose)
{
    http_identity = identity;
    http_verbose = verbose;
    http_timeout_ms = HTTP_TIMEOUT_DEFAULT_MS;
    atomic_store_explicit(&http_interrupted, 0, memory_order_relaxed);
}

void http_set_timeout_ms(unsigned milliseconds)
{
    http_timeout_ms = milliseconds ? milliseconds : HTTP_TIMEOUT_DEFAULT_MS;
}

void http_interrupt(void)
{
    int socket_id;

    atomic_store_explicit(&http_interrupted, 1, memory_order_relaxed);
    socket_id = atomic_load_explicit(&http_active_socket, memory_order_acquire);
    if (socket_id >= 0)
        (void)shutdown(socket_id, SHUT_RDWR);
}

void http_clear_interrupt(void)
{
    atomic_store_explicit(&http_interrupted, 0, memory_order_relaxed);
}

void http_response_free(http_response_t *response)
{
    free(response->body);
    response->body = NULL;
    response->length = 0;
}

static int wait_socket(int socket_id, int writable, unsigned timeout_ms)
{
    unsigned remaining = timeout_ms;

    for (;;)
    {
        fd_set descriptors;
        struct timeval timeout;
        unsigned slice = remaining && remaining < 50u ? remaining : 50u;
        int result;

        if (atomic_load_explicit(&http_interrupted, memory_order_relaxed))
            return -1;
        FD_ZERO(&descriptors);
        FD_SET(socket_id, &descriptors);
        timeout.tv_sec = 0;
        timeout.tv_usec = (int)slice * 1000;
        result = select(socket_id + 1, writable ? NULL : &descriptors,
                        writable ? &descriptors : NULL, NULL, &timeout);
        if (result != 0)
            return result;
        if (remaining)
        {
            if (remaining <= slice)
                return 0;
            remaining -= slice;
        }
    }
}

static int transport_send(void *context, const unsigned char *buffer, size_t length)
{
    int socket_id = *(int *)context;
    ssize_t result = send(socket_id, buffer, length, 0);
    if (result >= 0)
        return (int)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int transport_receive_timeout(void *context, unsigned char *buffer, size_t length,
                                     uint32_t timeout_ms)
{
    int socket_id = *(int *)context;
    int ready = wait_socket(socket_id, 0, timeout_ms);
    ssize_t result;

    if (ready == 0)
        return MBEDTLS_ERR_SSL_TIMEOUT;
    if (ready < 0)
    {
        if (atomic_load_explicit(&http_interrupted, memory_order_relaxed))
            return MBEDTLS_ERR_SSL_TIMEOUT;
        return errno == EINTR ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    }
    result = recv(socket_id, buffer, length, 0);
    if (result >= 0)
        return (int)result;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (errno == EPIPE || errno == ECONNRESET)
        return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int open_tcp_socket(const char *host, unsigned short port)
{
    struct addrinfo hints = {0};
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char service[8];
    int socket_id = -1;
    int address_result;

    http_connect_error[0] = '\0';
    snprintf(service, sizeof(service), "%u", port);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    address_result = getaddrinfo(host, service, &hints, &addresses);
    if (address_result != 0)
    {
        set_connect_error("address", address_result);
        return -1;
    }
    for (address = addresses; address; address = address->ai_next)
    {
        int nonblocking = 1;
        int connected = 0;

        if (atomic_load_explicit(&http_interrupted, memory_order_relaxed))
            break;
        socket_id = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        atomic_store_explicit(&http_active_socket, socket_id, memory_order_release);
        if (socket_id < 0)
        {
            set_connect_error("socket", errno);
        }
        else if (ioctl(socket_id, FIONBIO, &nonblocking) != 0)
        {
            set_connect_error("nonblocking", errno);
        }
        else
        {
            if (connect(socket_id, address->ai_addr, address->ai_addrlen) == 0)
                connected = 1;
            else if (errno == EINPROGRESS || errno == EWOULDBLOCK)
            {
                int ready = wait_socket(socket_id, 1, http_timeout_ms);
                int socket_error = 0;
                socklen_t error_size = sizeof(socket_error);

                if (ready == 0)
                    set_connect_error("timeout", 0);
                else if (ready < 0)
                    set_connect_error("wait", errno);
                else if (getsockopt(socket_id, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) !=
                         0)
                    set_connect_error("status", errno);
                else if (socket_error != 0)
                    set_connect_error("connect", socket_error);
                else
                    connected = 1;
            }
            else
            {
                set_connect_error("connect", errno);
            }
            nonblocking = 0;
            if (connected && ioctl(socket_id, FIONBIO, &nonblocking) != 0)
            {
                set_connect_error("blocking", errno);
                connected = 0;
            }
        }
        if (connected)
            break;
        if (socket_id >= 0)
            close(socket_id);
        atomic_store_explicit(&http_active_socket, -1, memory_order_release);
        socket_id = -1;
    }
    freeaddrinfo(addresses);
    return socket_id;
}

static void connection_close(http_connection_t *connection)
{
    if (connection->tls_initialized)
    {
        (void)mbedtls_ssl_close_notify(&connection->ssl);
        mbedtls_ssl_free(&connection->ssl);
        mbedtls_ssl_config_free(&connection->config);
        connection->tls_initialized = 0;
    }
    if (connection->socket >= 0)
    {
        atomic_store_explicit(&http_active_socket, -1, memory_order_release);
        (void)shutdown(connection->socket, SHUT_RDWR);
        (void)close(connection->socket);
        connection->socket = -1;
    }
}

static int connection_open(http_connection_t *connection, const char *host, unsigned short port,
                           int use_tls)
{
    int result;

    memset(connection, 0, sizeof(*connection));
    connection->socket = open_tcp_socket(host, port);
    connection->use_tls = use_tls;
    if (connection->socket < 0)
    {
        gs_error = http_connect_error[0] ? http_connect_error : "Could not connect to Sunshine";
        return GS_IO_ERROR;
    }
    if (!use_tls)
        return GS_OK;
    if (!http_identity)
    {
        gs_error = "HTTPS client identity is unavailable";
        connection_close(connection);
        return GS_WRONG_STATE;
    }

    mbedtls_ssl_init(&connection->ssl);
    mbedtls_ssl_config_init(&connection->config);
    connection->tls_initialized = 1;
    result = mbedtls_ssl_config_defaults(&connection->config, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result == 0)
    {
        mbedtls_ssl_conf_authmode(&connection->config, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&connection->config, mbedtls_ctr_drbg_random, &http_identity->rng);
        mbedtls_ssl_conf_read_timeout(&connection->config, http_timeout_ms);
        result = mbedtls_ssl_conf_own_cert(&connection->config, &http_identity->cert,
                                           &http_identity->key);
    }
    if (result == 0)
        result = mbedtls_ssl_setup(&connection->ssl, &connection->config);
    if (result == 0)
        result = mbedtls_ssl_set_hostname(&connection->ssl, host);
    if (result == 0)
    {
        mbedtls_ssl_set_bio(&connection->ssl, &connection->socket, transport_send, NULL,
                            transport_receive_timeout);
        do
        {
            result = mbedtls_ssl_handshake(&connection->ssl);
        } while (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE);
    }
    if (result != 0)
    {
        if (http_verbose)
        {
            char error[160];
            mbedtls_strerror(result, error, sizeof(error));
            LOGE("TLS handshake failed: -0x%04x %s", (unsigned)-result, error);
        }
        connection_close(connection);
        gs_error = "TLS handshake with Sunshine failed";
        return GS_IO_ERROR;
    }
    return GS_OK;
}

static int connection_read(http_connection_t *connection, unsigned char *buffer, size_t length)
{
    int result;
    do
    {
        result = connection->use_tls ? mbedtls_ssl_read(&connection->ssl, buffer, length)
                                     : transport_receive_timeout(&connection->socket, buffer,
                                                                 length, http_timeout_ms);
    } while (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE);
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return result;
}

static int connection_write_all(http_connection_t *connection, const unsigned char *buffer,
                                size_t length)
{
    size_t offset = 0;
    while (offset < length)
    {
        int result = connection->use_tls
                         ? mbedtls_ssl_write(&connection->ssl, buffer + offset, length - offset)
                         : transport_send(&connection->socket, buffer + offset, length - offset);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE)
            continue;
        if (result <= 0)
            return -1;
        offset += (size_t)result;
    }
    return 0;
}

static int read_exact(http_connection_t *connection, unsigned char *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length)
    {
        int result = connection_read(connection, buffer + offset, length - offset);
        if (result <= 0)
            return -1;
        offset += (size_t)result;
    }
    return 0;
}

static int read_line(http_connection_t *connection, char *buffer, size_t capacity)
{
    size_t offset = 0;
    while (offset + 1 < capacity)
    {
        unsigned char value;
        int result = connection_read(connection, &value, 1);
        if (result <= 0)
            return -1;
        buffer[offset++] = (char)value;
        if (value == '\n')
            break;
    }
    buffer[offset] = '\0';
    return (int)offset;
}

static int grow_response(http_response_t *response, size_t *capacity, size_t required)
{
    char *replacement;
    size_t next = *capacity;
    while (next < required)
    {
        if (next >= HTTP_MAX_RESPONSE)
            return -1;
        next *= 2;
        if (next > HTTP_MAX_RESPONSE)
            next = HTTP_MAX_RESPONSE;
    }
    replacement = realloc(response->body, next);
    if (!replacement)
        return -1;
    response->body = replacement;
    *capacity = next;
    return 0;
}

static int parse_http_status(const char *line, int *status)
{
    const char *separator;
    char *end;
    long value;

    if (strncmp(line, "HTTP/", 5) != 0)
        return -1;
    separator = strchr(line, ' ');
    if (!separator)
        return -1;
    value = strtol(separator + 1, &end, 10);
    if (end == separator + 1 || value < 100 || value > 999)
        return -1;
    *status = (int)value;
    return 0;
}

int http_get(const char *host, unsigned short port, int use_tls, const char *path,
             http_response_t *response)
{
    http_connection_t connection;
    char request[HTTP_REQUEST_CAPACITY];
    char line[1024];
    long content_length = -1;
    int chunked = 0;
    int status = 0;
    int result;

    response->body = NULL;
    response->length = 0;
    LOGI("GET %s://%s:%u path_bytes=%zu", use_tls ? "https" : "http", host, port, strlen(path));
    result = connection_open(&connection, host, port, use_tls);
    if (result != GS_OK)
    {
        if (atomic_load_explicit(&http_interrupted, memory_order_relaxed))
            gs_error = "Connection cancelled";
        return result;
    }
    result = snprintf(request, sizeof(request),
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s:%u\r\n"
                      "User-Agent: Moonlight-PS5/0.1\r\n"
                      "Accept: */*\r\n"
                      "Connection: close\r\n\r\n",
                      path, host, port);
    if (result <= 0 || result >= (int)sizeof(request))
    {
        gs_error = "NVHTTP request URL is too long";
        result = GS_INVALID;
        goto done;
    }
    if (connection_write_all(&connection, (const unsigned char *)request, (size_t)result) != 0)
    {
        gs_error = "Could not send NVHTTP request";
        result = GS_IO_ERROR;
        goto done;
    }
    result = read_line(&connection, line, sizeof(line));
    if (result <= 0 || parse_http_status(line, &status) != 0)
    {
        if (result <= 0)
        {
            LOGE("HTTP status line read failed: rc=%d", result);
        }
        else
        {
            const unsigned char *bytes = (const unsigned char *)line;
            LOGE("Invalid HTTP status line: bytes=%d prefix=%02x%02x%02x%02x%02x%02x%02x%02x",
                 result, result > 0 ? bytes[0] : 0, result > 1 ? bytes[1] : 0,
                 result > 2 ? bytes[2] : 0, result > 3 ? bytes[3] : 0, result > 4 ? bytes[4] : 0,
                 result > 5 ? bytes[5] : 0, result > 6 ? bytes[6] : 0, result > 7 ? bytes[7] : 0);
        }
        gs_error = "Sunshine returned an invalid HTTP status line";
        result = GS_IO_ERROR;
        goto done;
    }
    for (;;)
    {
        if (read_line(&connection, line, sizeof(line)) <= 0)
        {
            gs_error = "Sunshine returned truncated HTTP headers";
            result = GS_IO_ERROR;
            goto done;
        }
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n"))
            break;
        if (!strncasecmp(line, "Content-Length:", 15))
            content_length = strtol(line + 15, NULL, 10);
        else if (!strncasecmp(line, "Transfer-Encoding:", 18) && strstr(line, "chunked"))
            chunked = 1;
    }

    if (chunked)
    {
        size_t capacity = 8192;
        response->body = malloc(capacity);
        if (!response->body)
        {
            result = GS_OUT_OF_MEMORY;
            goto done;
        }
        for (;;)
        {
            long chunk;
            if (read_line(&connection, line, sizeof(line)) <= 0)
            {
                result = GS_IO_ERROR;
                goto done;
            }
            chunk = strtol(line, NULL, 16);
            if (chunk < 0 || (size_t)chunk > HTTP_MAX_RESPONSE ||
                response->length + (size_t)chunk > HTTP_MAX_RESPONSE)
            {
                result = GS_IO_ERROR;
                goto done;
            }
            if (chunk == 0)
            {
                (void)read_line(&connection, line, sizeof(line));
                break;
            }
            if (grow_response(response, &capacity, response->length + (size_t)chunk + 1) != 0)
            {
                result = GS_OUT_OF_MEMORY;
                goto done;
            }
            if (read_exact(&connection, (unsigned char *)response->body + response->length,
                           (size_t)chunk) != 0)
            {
                result = GS_IO_ERROR;
                goto done;
            }
            response->length += (size_t)chunk;
            (void)read_line(&connection, line, sizeof(line));
        }
    }
    else if (content_length >= 0)
    {
        if ((unsigned long)content_length > HTTP_MAX_RESPONSE)
        {
            result = GS_IO_ERROR;
            goto done;
        }
        response->body = malloc((size_t)content_length + 1);
        if (!response->body)
        {
            result = GS_OUT_OF_MEMORY;
            goto done;
        }
        if (read_exact(&connection, (unsigned char *)response->body, (size_t)content_length) != 0)
        {
            result = GS_IO_ERROR;
            goto done;
        }
        response->length = (size_t)content_length;
    }
    else
    {
        size_t capacity = 8192;
        response->body = malloc(capacity);
        if (!response->body)
        {
            result = GS_OUT_OF_MEMORY;
            goto done;
        }
        for (;;)
        {
            int count;
            if (grow_response(response, &capacity, response->length + 2049) != 0)
            {
                result = GS_OUT_OF_MEMORY;
                goto done;
            }
            count = connection_read(&connection, (unsigned char *)response->body + response->length,
                                    2048);
            if (count < 0 && response->length == 0)
            {
                result = GS_IO_ERROR;
                goto done;
            }
            if (count <= 0)
                break;
            response->length += (size_t)count;
        }
    }
    response->body[response->length] = '\0';
    if (status != 200)
    {
        gs_error = "Sunshine returned an HTTP error";
        result = GS_FAILED;
        goto done;
    }
    LOGI("HTTP %d response_bytes=%zu", status, response->length);
    result = GS_OK;

done:
    if (atomic_load_explicit(&http_interrupted, memory_order_relaxed))
    {
        gs_error = "Connection cancelled";
        result = GS_IO_ERROR;
    }
    if (result != GS_OK)
        http_response_free(response);
    connection_close(&connection);
    return result;
}
