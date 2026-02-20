#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/posix/poll.h>

#include <gateway_engine/gw_cloud.h>

#include "gw_sha256.h"

#define GW_CLOUD_HTTP_TIMEOUT_MS 5000
#define GW_CLOUD_MQTT_CONNECT_TIMEOUT_MS 5000
#define GW_CLOUD_MAX_HTTP_BODY 1024
#define GW_CLOUD_MAX_HTTP_RX 1024
#define GW_CLOUD_MAX_URL_HOST 96
#define GW_CLOUD_MAX_URL_PATH 128
#define GW_CLOUD_MAX_URL_BUF 192
#define GW_CLOUD_MAX_MSG_BUF 256
#define GW_CLOUD_MAX_SIG_HEX 65
#define GW_CLOUD_MAX_TIMESTAMP 32
#define GW_CLOUD_MQTT_RX_BUF 2048
#define GW_CLOUD_MQTT_TX_BUF 2048
#define GW_CLOUD_MQTT_WS_TMP_BUF 1024
#define GW_CLOUD_TOPIC_SLOT 0

typedef enum {
    GW_URL_SCHEME_HTTP = 0,
    GW_URL_SCHEME_HTTPS = 1,
    GW_URL_SCHEME_WS = 2,
    GW_URL_SCHEME_WSS = 3,
} gw_url_scheme_t;

typedef struct {
    gw_url_scheme_t scheme;
    char host[GW_CLOUD_MAX_URL_HOST];
    uint16_t port;
    char path[GW_CLOUD_MAX_URL_PATH];
} gw_url_t;

typedef struct {
    char body[GW_CLOUD_MAX_HTTP_BODY];
    size_t body_len;
    uint16_t status_code;
} gw_http_result_t;

typedef struct {
    struct mqtt_client mqtt;
    struct mqtt_utf8 mqtt_user_name;
    struct mqtt_utf8 mqtt_password;
    struct sockaddr_storage broker;
    struct pollfd fds[1];
    int nfds;
    bool mqtt_connected;
    bool connack_received;
    int connack_result;
    uint16_t last_message_id;
    sec_tag_t sec_tags[1];
    uint8_t mqtt_rx_buf[GW_CLOUD_MQTT_RX_BUF];
    uint8_t mqtt_tx_buf[GW_CLOUD_MQTT_TX_BUF];
#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
    uint8_t ws_tmp_buf[GW_CLOUD_MQTT_WS_TMP_BUF];
#endif
} gw_cloud_runtime_t;

static gw_cloud_runtime_t g_rt;

static size_t gw_strnlen_safe(const char *s, size_t max)
{
    size_t i = 0U;

    if (s == NULL) {
        return 0U;
    }

    while (i < max && s[i] != '\0') {
        ++i;
    }

    return i;
}

static int gw_snprintf_checked(char *dst, size_t dst_sz, const char *fmt, ...)
{
    int n;
    va_list args;

    if (dst == NULL || dst_sz == 0U || fmt == NULL) {
        return -EINVAL;
    }

    va_start(args, fmt);
    n = vsnprintf(dst, dst_sz, fmt, args);
    va_end(args);

    if (n < 0) {
        return -EIO;
    }

    if ((size_t)n >= dst_sz) {
        return -ENOSPC;
    }

    return 0;
}

static int gw_copy_string(char *dst, size_t dst_sz, const char *src)
{
    size_t n;

    if (dst == NULL || dst_sz == 0U || src == NULL) {
        return -EINVAL;
    }

    n = gw_strnlen_safe(src, dst_sz);
    if (n >= dst_sz) {
        return -ENOSPC;
    }

    (void)memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

static void gw_hex_encode(const uint8_t *in, size_t in_len, char *out_hex, size_t out_sz)
{
    static const char HEX[] = "0123456789abcdef";
    size_t i;

    if (out_hex == NULL || out_sz == 0U) {
        return;
    }

    if (in == NULL || out_sz < (in_len * 2U + 1U)) {
        out_hex[0] = '\0';
        return;
    }

    for (i = 0; i < in_len; ++i) {
        out_hex[i * 2U] = HEX[(in[i] >> 4) & 0x0FU];
        out_hex[i * 2U + 1U] = HEX[in[i] & 0x0FU];
    }

    out_hex[in_len * 2U] = '\0';
}

static int gw_make_rfc3339_now(char *out_ts, size_t out_ts_sz)
{
    time_t now;
    struct tm tm_now;

    if (out_ts == NULL || out_ts_sz == 0U) {
        return -EINVAL;
    }

    now = time(NULL);
    if (now <= 0) {
        return -ENODATA;
    }

    if (gmtime_r(&now, &tm_now) == NULL) {
        return -EIO;
    }

    return gw_snprintf_checked(
        out_ts,
        out_ts_sz,
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        tm_now.tm_year + 1900,
        tm_now.tm_mon + 1,
        tm_now.tm_mday,
        tm_now.tm_hour,
        tm_now.tm_min,
        tm_now.tm_sec);
}

static const char *gw_identity_key(const gw_cloud_config_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }

    if (cfg->hardware_id != NULL && cfg->hardware_id[0] != '\0') {
        return cfg->hardware_id;
    }

    if (cfg->device_id != NULL && cfg->device_id[0] != '\0') {
        return cfg->device_id;
    }

    if (cfg->identity_key != NULL && cfg->identity_key[0] != '\0') {
        return cfg->identity_key;
    }

    return NULL;
}

static int gw_make_signature(
    const gw_cloud_config_t *cfg,
    const char *identity_key,
    const char *timestamp,
    char *out_sig_hex,
    size_t out_sig_hex_sz)
{
    char msg[GW_CLOUD_MAX_MSG_BUF];
    uint8_t mac[32];

    if (cfg == NULL || identity_key == NULL || timestamp == NULL || out_sig_hex == NULL) {
        return -EINVAL;
    }

    if (cfg->manufacturing_key == NULL || cfg->manufacturing_key[0] == '\0') {
        return -EINVAL;
    }

    if (gw_snprintf_checked(msg, sizeof(msg), "%s:%s", identity_key, timestamp) != 0) {
        return -ENOSPC;
    }

    gw_hmac_sha256(
        (const uint8_t *)cfg->manufacturing_key,
        strlen(cfg->manufacturing_key),
        (const uint8_t *)msg,
        strlen(msg),
        mac);

    gw_hex_encode(mac, sizeof(mac), out_sig_hex, out_sig_hex_sz);
    if (out_sig_hex[0] == '\0') {
        return -ENOSPC;
    }

    return 0;
}

static int gw_parse_url(
    const char *url,
    gw_url_scheme_t default_scheme,
    bool default_path_mqtt,
    gw_url_t *out)
{
    const char *scheme_end;
    const char *authority;
    const char *path_in_url;
    const char *path;
    const char *host_begin;
    const char *host_end;
    const char *authority_end;
    const char *port_sep = NULL;
    size_t host_len;
    size_t path_len;

    if (url == NULL || out == NULL) {
        return -EINVAL;
    }

    (void)memset(out, 0, sizeof(*out));

    scheme_end = strstr(url, "://");
    if (scheme_end != NULL) {
        size_t scheme_len = (size_t)(scheme_end - url);
        authority = scheme_end + 3;

        if (scheme_len == 4U && strncmp(url, "http", 4U) == 0) {
            out->scheme = GW_URL_SCHEME_HTTP;
        } else if (scheme_len == 5U && strncmp(url, "https", 5U) == 0) {
            out->scheme = GW_URL_SCHEME_HTTPS;
        } else if (scheme_len == 2U && strncmp(url, "ws", 2U) == 0) {
            out->scheme = GW_URL_SCHEME_WS;
        } else if (scheme_len == 3U && strncmp(url, "wss", 3U) == 0) {
            out->scheme = GW_URL_SCHEME_WSS;
        } else {
            return -EPROTONOSUPPORT;
        }
    } else {
        authority = url;
        out->scheme = default_scheme;
    }

    path_in_url = strchr(authority, '/');
    if (path_in_url == NULL) {
        host_end = authority + strlen(authority);
        if (default_path_mqtt) {
            path = "/mqtt";
        } else {
            path = "/";
        }
    } else {
        path = path_in_url;
        host_end = path_in_url;
    }

    host_begin = authority;
    authority_end = host_end;

    if (*host_begin == '[') {
        const char *bracket_end = strchr(host_begin, ']');
        if (bracket_end == NULL || bracket_end >= host_end) {
            return -EINVAL;
        }
        host_begin += 1;
        host_end = bracket_end;
        if (bracket_end + 1 < authority_end && bracket_end[1] == ':') {
            port_sep = bracket_end + 1;
        }
    } else {
        const char *tmp = host_end;
        while (tmp > host_begin) {
            --tmp;
            if (*tmp == ':') {
                port_sep = tmp;
                host_end = tmp;
                break;
            }
        }
    }

    host_len = (size_t)(host_end - host_begin);
    if (host_len == 0U || host_len >= sizeof(out->host)) {
        return -ENOSPC;
    }

    (void)memcpy(out->host, host_begin, host_len);
    out->host[host_len] = '\0';

    if (port_sep != NULL) {
        long port_val = strtol(port_sep + 1, NULL, 10);
        if (port_val <= 0 || port_val > 65535) {
            return -EINVAL;
        }
        out->port = (uint16_t)port_val;
    } else {
        switch (out->scheme) {
        case GW_URL_SCHEME_HTTP:
        case GW_URL_SCHEME_WS:
            out->port = 80U;
            break;
        case GW_URL_SCHEME_HTTPS:
        case GW_URL_SCHEME_WSS:
            out->port = 443U;
            break;
        default:
            return -EINVAL;
        }
    }

    path_len = gw_strnlen_safe(path, sizeof(out->path));
    if (path_len >= sizeof(out->path)) {
        return -ENOSPC;
    }

    (void)memcpy(out->path, path, path_len);
    out->path[path_len] = '\0';
    return 0;
}

static bool gw_url_is_tls(const gw_url_t *url)
{
    return url->scheme == GW_URL_SCHEME_HTTPS || url->scheme == GW_URL_SCHEME_WSS;
}

static int gw_socket_connect(const gw_url_t *url, int tls_sec_tag, int *out_sock)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it;
    char port_buf[8];
    int sock = -1;
    int rc;

    if (url == NULL || out_sock == NULL) {
        return -EINVAL;
    }

    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = gw_url_is_tls(url) ? IPPROTO_TLS_1_2 : IPPROTO_TCP;

    rc = gw_snprintf_checked(port_buf, sizeof(port_buf), "%u", url->port);
    if (rc != 0) {
        return rc;
    }

    rc = getaddrinfo(url->host, port_buf, &hints, &res);
    if (rc != 0 || res == NULL) {
        return -EHOSTUNREACH;
    }

    for (it = res; it != NULL; it = it->ai_next) {
        sock = socket(it->ai_family, it->ai_socktype, hints.ai_protocol);
        if (sock < 0) {
            continue;
        }

        if (gw_url_is_tls(url)) {
            sec_tag_t sec_tags[1];

            if (tls_sec_tag < 0) {
                close(sock);
                sock = -1;
                continue;
            }

            sec_tags[0] = (sec_tag_t)tls_sec_tag;

            rc = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));
            if (rc < 0) {
                close(sock);
                sock = -1;
                continue;
            }

            rc = setsockopt(sock, SOL_TLS, TLS_HOSTNAME, url->host, strlen(url->host) + 1U);
            if (rc < 0) {
                close(sock);
                sock = -1;
                continue;
            }
        }

        rc = connect(sock, it->ai_addr, it->ai_addrlen);
        if (rc == 0) {
            break;
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);

    if (sock < 0) {
        return -ECONNREFUSED;
    }

    *out_sock = sock;
    return 0;
}

static int gw_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    gw_http_result_t *result = (gw_http_result_t *)user_data;

    ARG_UNUSED(final_data);

    if (rsp == NULL || result == NULL) {
        return -EINVAL;
    }

    result->status_code = rsp->http_status_code;

    if (rsp->body_frag_start != NULL && rsp->body_frag_len > 0U) {
        size_t copy_len = rsp->body_frag_len;

        if (result->body_len + copy_len >= sizeof(result->body)) {
            copy_len = sizeof(result->body) - result->body_len - 1U;
        }

        if (copy_len > 0U) {
            (void)memcpy(&result->body[result->body_len], rsp->body_frag_start, copy_len);
            result->body_len += copy_len;
            result->body[result->body_len] = '\0';
        }
    }

    return 0;
}

static int gw_http_post_json(
    const gw_url_t *url,
    int tls_sec_tag,
    const char *payload,
    uint32_t timeout_ms,
    gw_http_result_t *out_result)
{
    int sock = -1;
    int rc;
    struct http_request req;
    static const char *const HDRS[] = {
        "Accept: application/json\r\n",
        "Connection: close\r\n",
        NULL,
    };
    char recv_buf[GW_CLOUD_MAX_HTTP_RX];
    char port_buf[8];

    if (url == NULL || payload == NULL || out_result == NULL) {
        return -EINVAL;
    }

    (void)memset(out_result, 0, sizeof(*out_result));

    rc = gw_socket_connect(url, tls_sec_tag, &sock);
    if (rc != 0) {
        return rc;
    }

    (void)memset(&req, 0, sizeof(req));
    rc = gw_snprintf_checked(port_buf, sizeof(port_buf), "%u", url->port);
    if (rc != 0) {
        close(sock);
        return rc;
    }

    req.method = HTTP_POST;
    req.url = url->path;
    req.host = url->host;
    req.port = port_buf;
    req.protocol = "HTTP/1.1";
    req.header_fields = HDRS;
    req.content_type_value = "application/json";
    req.payload = payload;
    req.payload_len = strlen(payload);
    req.response = gw_http_response_cb;
    req.recv_buf = (uint8_t *)recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    rc = http_client_req(sock, &req, (int32_t)timeout_ms, out_result);
    close(sock);

    if (rc < 0) {
        return rc;
    }

    return 0;
}

static const char *gw_json_find_key(const char *json, const char *key)
{
    char pattern[48];

    if (json == NULL || key == NULL) {
        return NULL;
    }

    if (gw_snprintf_checked(pattern, sizeof(pattern), "\"%s\"", key) != 0) {
        return NULL;
    }

    return strstr(json, pattern);
}

static int gw_json_get_string(const char *json, const char *key, char *out, size_t out_sz)
{
    const char *p;
    const char *q;
    size_t len;

    if (json == NULL || key == NULL || out == NULL || out_sz == 0U) {
        return -EINVAL;
    }

    p = gw_json_find_key(json, key);
    if (p == NULL) {
        return -ENOENT;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return -EBADMSG;
    }
    ++p;

    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    if (*p != '"') {
        return -EBADMSG;
    }
    ++p;

    q = strchr(p, '"');
    if (q == NULL) {
        return -EBADMSG;
    }

    len = (size_t)(q - p);
    if (len >= out_sz) {
        return -ENOSPC;
    }

    (void)memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

static int gw_json_get_uint(const char *json, const char *key, uint32_t *out)
{
    const char *p;
    long value;

    if (json == NULL || key == NULL || out == NULL) {
        return -EINVAL;
    }

    p = gw_json_find_key(json, key);
    if (p == NULL) {
        return -ENOENT;
    }

    p = strchr(p, ':');
    if (p == NULL) {
        return -EBADMSG;
    }
    ++p;

    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    value = strtol(p, NULL, 10);
    if (value < 0L) {
        return -ERANGE;
    }

    *out = (uint32_t)value;
    return 0;
}

static gw_cloud_status_t gw_status_from_string(const char *status)
{
    if (status == NULL) {
        return GW_CLOUD_STATUS_UNKNOWN;
    }

    if (strcmp(status, "not_provisioned") == 0) {
        return GW_CLOUD_STATUS_NOT_PROVISIONED;
    }
    if (strcmp(status, "unclaimed") == 0) {
        return GW_CLOUD_STATUS_UNCLAIMED;
    }
    if (strcmp(status, "claimed") == 0) {
        return GW_CLOUD_STATUS_CLAIMED;
    }
    if (strcmp(status, "active") == 0) {
        return GW_CLOUD_STATUS_ACTIVE;
    }
    if (strcmp(status, "suspended") == 0) {
        return GW_CLOUD_STATUS_SUSPENDED;
    }
    if (strcmp(status, "revoked") == 0) {
        return GW_CLOUD_STATUS_REVOKED;
    }

    return GW_CLOUD_STATUS_UNKNOWN;
}

static int gw_build_api_url(
    const char *explicit_url,
    const char *api_base_url,
    const char *path_suffix,
    char *out,
    size_t out_sz)
{
    size_t base_len;

    if (out == NULL || out_sz == 0U) {
        return -EINVAL;
    }

    if (explicit_url != NULL && explicit_url[0] != '\0') {
        return gw_copy_string(out, out_sz, explicit_url);
    }

    if (api_base_url == NULL || api_base_url[0] == '\0' || path_suffix == NULL) {
        return -EINVAL;
    }

    base_len = strlen(api_base_url);
    if (base_len > 0U && api_base_url[base_len - 1U] == '/') {
        return gw_snprintf_checked(out, out_sz, "%s%s", api_base_url, path_suffix + 1);
    }

    return gw_snprintf_checked(out, out_sz, "%s%s", api_base_url, path_suffix);
}

static int gw_derive_secret_from_bootstrap(const char *bootstrap_url, char *out, size_t out_sz)
{
    static const char SUFFIX_BOOTSTRAP[] = "/bootstrap";
    const char *found;
    size_t prefix_len;

    if (bootstrap_url == NULL || out == NULL || out_sz == 0U) {
        return -EINVAL;
    }

    found = strstr(bootstrap_url, SUFFIX_BOOTSTRAP);
    if (found == NULL) {
        return -EINVAL;
    }

    prefix_len = (size_t)(found - bootstrap_url);
    if (prefix_len + strlen("/secret") + 1U > out_sz) {
        return -ENOSPC;
    }

    (void)memcpy(out, bootstrap_url, prefix_len);
    (void)memcpy(&out[prefix_len], "/secret", strlen("/secret") + 1U);
    return 0;
}

static int gw_build_auth_payload(const gw_cloud_config_t *cfg, char *out, size_t out_sz)
{
    char ts[GW_CLOUD_MAX_TIMESTAMP];
    char sig[GW_CLOUD_MAX_SIG_HEX];
    const char *identity;
    int rc;

    if (cfg == NULL || out == NULL || out_sz == 0U) {
        return -EINVAL;
    }

    identity = gw_identity_key(cfg);
    if (identity == NULL || identity[0] == '\0') {
        return -EINVAL;
    }

    rc = gw_make_rfc3339_now(ts, sizeof(ts));
    if (rc != 0) {
        return rc;
    }

    rc = gw_make_signature(cfg, identity, ts, sig, sizeof(sig));
    if (rc != 0) {
        return rc;
    }

    if (cfg->hardware_id != NULL && cfg->hardware_id[0] != '\0') {
        return gw_snprintf_checked(
            out,
            out_sz,
            "{\"hardware_id\":\"%s\",\"timestamp\":\"%s\",\"signature\":\"%s\"}",
            cfg->hardware_id,
            ts,
            sig);
    } else if (cfg->device_id != NULL && cfg->device_id[0] != '\0') {
        return gw_snprintf_checked(
            out,
            out_sz,
            "{\"device_id\":\"%s\",\"timestamp\":\"%s\",\"signature\":\"%s\"}",
            cfg->device_id,
            ts,
            sig);
    } else {
        return -EINVAL;
    }
}

static int gw_cloud_do_bootstrap(gw_cloud_client_t *client)
{
    char url_buf[GW_CLOUD_MAX_URL_BUF];
    char payload[GW_CLOUD_MAX_MSG_BUF];
    char status_buf[32];
    gw_url_t url;
    gw_http_result_t result;
    uint32_t poll_interval;
    int rc;

    rc = gw_build_api_url(
        client->config.bootstrap_url,
        client->config.api_base_url,
        "/api/v1/devices/bootstrap",
        url_buf,
        sizeof(url_buf));
    if (rc != 0) {
        return rc;
    }

    rc = gw_parse_url(url_buf, GW_URL_SCHEME_HTTPS, false, &url);
    if (rc != 0) {
        return rc;
    }

    rc = gw_build_auth_payload(&client->config, payload, sizeof(payload));
    if (rc != 0) {
        return rc;
    }

    rc = gw_http_post_json(
        &url,
        client->config.tls_sec_tag,
        payload,
        client->config.bootstrap_timeout_ms,
        &result);
    if (rc != 0) {
        return rc;
    }

    if (result.status_code != 200U) {
        return -EACCES;
    }

    rc = gw_json_get_string(result.body, "status", status_buf, sizeof(status_buf));
    if (rc != 0) {
        return rc;
    }

    client->status = gw_status_from_string(status_buf);

    if (gw_json_get_string(result.body, "device_id", client->resolved_device_id, sizeof(client->resolved_device_id)) !=
        0) {
        if (client->config.device_id != NULL) {
            (void)gw_copy_string(client->resolved_device_id, sizeof(client->resolved_device_id), client->config.device_id);
        }
    }

    if (gw_json_get_string(
            result.body,
            "hardware_id",
            client->resolved_hardware_id,
            sizeof(client->resolved_hardware_id)) != 0) {
        if (client->config.hardware_id != NULL) {
            (void)gw_copy_string(
                client->resolved_hardware_id,
                sizeof(client->resolved_hardware_id),
                client->config.hardware_id);
        }
    }

    poll_interval = 0U;
    if (gw_json_get_uint(result.body, "poll_interval", &poll_interval) == 0) {
        client->poll_interval_s = poll_interval;
    }

    return 0;
}

static int gw_cloud_do_secret(gw_cloud_client_t *client)
{
    char url_buf[GW_CLOUD_MAX_URL_BUF];
    char payload[GW_CLOUD_MAX_MSG_BUF];
    gw_url_t url;
    gw_http_result_t result;
    int rc;

    rc = gw_build_api_url(
        client->config.secret_url,
        client->config.api_base_url,
        "/api/v1/devices/secret",
        url_buf,
        sizeof(url_buf));
    if (rc != 0 && client->config.secret_url == NULL && client->config.api_base_url == NULL &&
        client->config.bootstrap_url != NULL) {
        rc = gw_derive_secret_from_bootstrap(client->config.bootstrap_url, url_buf, sizeof(url_buf));
    }
    if (rc != 0) {
        return rc;
    }

    rc = gw_parse_url(url_buf, GW_URL_SCHEME_HTTPS, false, &url);
    if (rc != 0) {
        return rc;
    }

    rc = gw_build_auth_payload(&client->config, payload, sizeof(payload));
    if (rc != 0) {
        return rc;
    }

    rc = gw_http_post_json(
        &url,
        client->config.tls_sec_tag,
        payload,
        client->config.bootstrap_timeout_ms,
        &result);
    if (rc != 0) {
        return rc;
    }

    if (result.status_code != 200U) {
        return -EACCES;
    }

    rc = gw_json_get_string(
        result.body,
        "device_secret",
        client->resolved_device_secret,
        sizeof(client->resolved_device_secret));
    if (rc != 0) {
        return rc;
    }

    rc = gw_json_get_string(
        result.body,
        "mqtt_username",
        client->resolved_mqtt_username,
        sizeof(client->resolved_mqtt_username));
    if (rc != 0) {
        return rc;
    }

    rc = gw_json_get_string(result.body, "broker", client->resolved_broker, sizeof(client->resolved_broker));
    if (rc != 0) {
        return rc;
    }

    rc = gw_json_get_string(
        result.body,
        "topic_prefix",
        client->resolved_topic_prefix,
        sizeof(client->resolved_topic_prefix));
    if (rc != 0) {
        return rc;
    }

    client->credentials_ready = true;
    return 0;
}

static void gw_mqtt_evt_handler(struct mqtt_client *mqtt, const struct mqtt_evt *evt)
{
    gw_cloud_client_t *client;

    if (mqtt == NULL || evt == NULL) {
        return;
    }

    client = (gw_cloud_client_t *)mqtt->user_data;
    if (client == NULL) {
        return;
    }

    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        g_rt.connack_received = true;
        g_rt.connack_result = evt->result;
        if (evt->result == 0 && evt->param.connack.return_code == MQTT_CONNECTION_ACCEPTED) {
            g_rt.mqtt_connected = true;
            client->connected = true;
        } else {
            g_rt.mqtt_connected = false;
            client->connected = false;
        }
        break;

    case MQTT_EVT_DISCONNECT:
        g_rt.mqtt_connected = false;
        client->connected = false;
        break;

    default:
        break;
    }
}

static int gw_prepare_broker_sockaddr(const gw_url_t *url)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    char port_buf[8];
    int rc;

    if (url == NULL) {
        return -EINVAL;
    }

    (void)memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = gw_snprintf_checked(port_buf, sizeof(port_buf), "%u", url->port);
    if (rc != 0) {
        return rc;
    }

    rc = getaddrinfo(url->host, port_buf, &hints, &res);
    if (rc != 0 || res == NULL) {
        return -EHOSTUNREACH;
    }

    if (res->ai_addrlen > sizeof(g_rt.broker)) {
        freeaddrinfo(res);
        return -ENOBUFS;
    }

    (void)memcpy(&g_rt.broker, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    return 0;
}

static void gw_prepare_fds(struct mqtt_client *mqtt)
{
    g_rt.nfds = 0;

    if (mqtt == NULL) {
        return;
    }

    switch (mqtt->transport.type) {
    case MQTT_TRANSPORT_NON_SECURE:
        g_rt.fds[0].fd = mqtt->transport.tcp.sock;
        break;
#if defined(CONFIG_MQTT_LIB_TLS)
    case MQTT_TRANSPORT_SECURE:
        g_rt.fds[0].fd = mqtt->transport.tls.sock;
        break;
#endif
#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
    case MQTT_TRANSPORT_NON_SECURE_WEBSOCKET:
#if defined(CONFIG_MQTT_LIB_TLS)
    case MQTT_TRANSPORT_SECURE_WEBSOCKET:
#endif
        g_rt.fds[0].fd = mqtt->transport.websocket.sock;
        break;
#endif
    default:
        return;
    }

    g_rt.fds[0].events = POLLIN;
    g_rt.nfds = 1;
}

static int gw_cloud_mqtt_configure(gw_cloud_client_t *client)
{
    gw_url_t broker_url;
    const char *client_id;
    int rc;

    if (client == NULL) {
        return -EINVAL;
    }

    if (client->resolved_broker[0] == '\0') {
        if (client->config.broker_url == NULL || client->config.broker_url[0] == '\0') {
            return -EINVAL;
        }
        rc = gw_copy_string(client->resolved_broker, sizeof(client->resolved_broker), client->config.broker_url);
        if (rc != 0) {
            return rc;
        }
    }

    rc = gw_parse_url(client->resolved_broker, GW_URL_SCHEME_WSS, true, &broker_url);
    if (rc != 0) {
        return rc;
    }

    (void)memset(&g_rt, 0, sizeof(g_rt));

    rc = gw_prepare_broker_sockaddr(&broker_url);
    if (rc != 0) {
        return rc;
    }

    mqtt_client_init(&g_rt.mqtt);

    g_rt.mqtt.broker = &g_rt.broker;
    g_rt.mqtt.evt_cb = gw_mqtt_evt_handler;
    g_rt.mqtt.user_data = client;
    g_rt.mqtt.rx_buf = g_rt.mqtt_rx_buf;
    g_rt.mqtt.rx_buf_size = sizeof(g_rt.mqtt_rx_buf);
    g_rt.mqtt.tx_buf = g_rt.mqtt_tx_buf;
    g_rt.mqtt.tx_buf_size = sizeof(g_rt.mqtt_tx_buf);
    g_rt.mqtt.protocol_version = MQTT_VERSION_3_1_1;

    client_id = client->config.mqtt_client_id;
    if (client_id == NULL || client_id[0] == '\0') {
        client_id = (client->resolved_device_id[0] != '\0') ? client->resolved_device_id : client->config.device_id;
    }
    if (client_id == NULL || client_id[0] == '\0') {
        return -EINVAL;
    }

    g_rt.mqtt.client_id.utf8 = (const uint8_t *)client_id;
    g_rt.mqtt.client_id.size = strlen(client_id);

    g_rt.mqtt_user_name.utf8 = (const uint8_t *)client->resolved_mqtt_username;
    g_rt.mqtt_user_name.size = strlen(client->resolved_mqtt_username);
    g_rt.mqtt_password.utf8 = (const uint8_t *)client->resolved_device_secret;
    g_rt.mqtt_password.size = strlen(client->resolved_device_secret);

    g_rt.mqtt.user_name = &g_rt.mqtt_user_name;
    g_rt.mqtt.password = &g_rt.mqtt_password;

    g_rt.mqtt.keepalive = (client->config.mqtt_keepalive_sec == 0U) ? 60U : client->config.mqtt_keepalive_sec;

    switch (broker_url.scheme) {
    case GW_URL_SCHEME_WSS:
#if defined(CONFIG_MQTT_LIB_TLS) && defined(CONFIG_MQTT_LIB_WEBSOCKET)
        if (client->config.tls_sec_tag < 0) {
            return -EINVAL;
        }
        g_rt.mqtt.transport.type = MQTT_TRANSPORT_SECURE_WEBSOCKET;
        g_rt.sec_tags[0] = (sec_tag_t)client->config.tls_sec_tag;
        g_rt.mqtt.transport.tls.config.peer_verify = TLS_PEER_VERIFY_REQUIRED;
        g_rt.mqtt.transport.tls.config.sec_tag_list = g_rt.sec_tags;
        g_rt.mqtt.transport.tls.config.sec_tag_count = 1U;
        g_rt.mqtt.transport.tls.config.hostname = broker_url.host;

        g_rt.mqtt.transport.websocket.config.host = broker_url.host;
        g_rt.mqtt.transport.websocket.config.url = broker_url.path;
        g_rt.mqtt.transport.websocket.config.tmp_buf = g_rt.ws_tmp_buf;
        g_rt.mqtt.transport.websocket.config.tmp_buf_len = sizeof(g_rt.ws_tmp_buf);
        g_rt.mqtt.transport.websocket.timeout = (int32_t)client->config.mqtt_connect_timeout_ms;
#else
        return -ENOTSUP;
#endif
        break;

    case GW_URL_SCHEME_WS:
#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
        g_rt.mqtt.transport.type = MQTT_TRANSPORT_NON_SECURE_WEBSOCKET;
        g_rt.mqtt.transport.websocket.config.host = broker_url.host;
        g_rt.mqtt.transport.websocket.config.url = broker_url.path;
        g_rt.mqtt.transport.websocket.config.tmp_buf = g_rt.ws_tmp_buf;
        g_rt.mqtt.transport.websocket.config.tmp_buf_len = sizeof(g_rt.ws_tmp_buf);
        g_rt.mqtt.transport.websocket.timeout = (int32_t)client->config.mqtt_connect_timeout_ms;
#else
        return -ENOTSUP;
#endif
        break;

    case GW_URL_SCHEME_HTTPS:
#if defined(CONFIG_MQTT_LIB_TLS)
        if (client->config.tls_sec_tag < 0) {
            return -EINVAL;
        }
        g_rt.mqtt.transport.type = MQTT_TRANSPORT_SECURE;
        g_rt.sec_tags[0] = (sec_tag_t)client->config.tls_sec_tag;
        g_rt.mqtt.transport.tls.config.peer_verify = TLS_PEER_VERIFY_REQUIRED;
        g_rt.mqtt.transport.tls.config.sec_tag_list = g_rt.sec_tags;
        g_rt.mqtt.transport.tls.config.sec_tag_count = 1U;
        g_rt.mqtt.transport.tls.config.hostname = broker_url.host;
#else
        return -ENOTSUP;
#endif
        break;

    case GW_URL_SCHEME_HTTP:
        g_rt.mqtt.transport.type = MQTT_TRANSPORT_NON_SECURE;
        break;

    default:
        return -EPROTONOSUPPORT;
    }

    return 0;
}

static int gw_cloud_wait_connack(gw_cloud_client_t *client)
{
    int64_t deadline;

    if (client == NULL) {
        return -EINVAL;
    }

    deadline = k_uptime_get() + (int64_t)client->config.mqtt_connect_timeout_ms;

    while (k_uptime_get() < deadline) {
        int timeout = (int)(deadline - k_uptime_get());
        int rc;

        if (timeout > 250) {
            timeout = 250;
        }

        rc = (g_rt.nfds > 0) ? poll(g_rt.fds, g_rt.nfds, timeout) : 0;
        if (rc > 0) {
            rc = mqtt_input(&g_rt.mqtt);
            if (rc != 0) {
                return rc;
            }
        }

        if (g_rt.connack_received) {
            return g_rt.mqtt_connected ? 0 : -ECONNREFUSED;
        }
    }

    return -ETIMEDOUT;
}

int gw_cloud_init(gw_cloud_client_t *client, const gw_cloud_config_t *cfg)
{
    if (client == NULL || cfg == NULL) {
        return -EINVAL;
    }

    if (cfg->manufacturing_key == NULL || cfg->manufacturing_key[0] == '\0') {
        return -EINVAL;
    }

    if (gw_identity_key(cfg) == NULL) {
        return -EINVAL;
    }

    (void)memset(client, 0, sizeof(*client));
    client->config = *cfg;

    if (client->config.bootstrap_timeout_ms == 0U) {
        client->config.bootstrap_timeout_ms = GW_CLOUD_HTTP_TIMEOUT_MS;
    }
    if (client->config.mqtt_connect_timeout_ms == 0U) {
        client->config.mqtt_connect_timeout_ms = GW_CLOUD_MQTT_CONNECT_TIMEOUT_MS;
    }

    if (cfg->device_id != NULL) {
        (void)gw_copy_string(client->resolved_device_id, sizeof(client->resolved_device_id), cfg->device_id);
    }
    if (cfg->hardware_id != NULL) {
        (void)gw_copy_string(client->resolved_hardware_id, sizeof(client->resolved_hardware_id), cfg->hardware_id);
    }

    if (cfg->broker_url != NULL) {
        (void)gw_copy_string(client->resolved_broker, sizeof(client->resolved_broker), cfg->broker_url);
    }
    if (cfg->mqtt_username != NULL) {
        (void)gw_copy_string(
            client->resolved_mqtt_username,
            sizeof(client->resolved_mqtt_username),
            cfg->mqtt_username);
    }
    if (cfg->device_secret != NULL) {
        (void)gw_copy_string(
            client->resolved_device_secret,
            sizeof(client->resolved_device_secret),
            cfg->device_secret);
    }
    if (cfg->topic_prefix != NULL) {
        (void)gw_copy_string(
            client->resolved_topic_prefix,
            sizeof(client->resolved_topic_prefix),
            cfg->topic_prefix);
    }

    if (client->resolved_mqtt_username[0] != '\0' && client->resolved_device_secret[0] != '\0' &&
        client->resolved_topic_prefix[0] != '\0') {
        client->credentials_ready = true;
    }

    client->initialized = true;
    return 0;
}

int gw_cloud_connect(gw_cloud_client_t *client)
{
    int rc;

    if (client == NULL || !client->initialized) {
        return -EINVAL;
    }

    if (client->connected) {
        return 0;
    }

    rc = gw_cloud_do_bootstrap(client);
    if (rc != 0) {
        return rc;
    }

    if (client->status != GW_CLOUD_STATUS_CLAIMED && client->status != GW_CLOUD_STATUS_ACTIVE) {
        return -EAGAIN;
    }

    if (!client->credentials_ready) {
        rc = gw_cloud_do_secret(client);
        if (rc != 0) {
            return rc;
        }
    }

    rc = gw_cloud_mqtt_configure(client);
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_connect(&g_rt.mqtt);
    if (rc != 0) {
        return rc;
    }

    gw_prepare_fds(&g_rt.mqtt);

    rc = gw_cloud_wait_connack(client);
    if (rc != 0) {
        (void)mqtt_abort(&g_rt.mqtt);
        client->connected = false;
        return rc;
    }

    client->connected = true;
    return 0;
}

int gw_cloud_publish_telemetry(gw_cloud_client_t *client, const uint8_t *payload, size_t payload_len)
{
    struct mqtt_publish_param param;
    char topic[256];
    int rc;

    if (client == NULL || !client->connected) {
        return -ENOTCONN;
    }

    if (payload == NULL || payload_len == 0U) {
        return -EINVAL;
    }

    if (client->resolved_topic_prefix[0] == '\0') {
        return -ENODATA;
    }

    rc = gw_snprintf_checked(topic, sizeof(topic), "%s/slot/%d", client->resolved_topic_prefix, GW_CLOUD_TOPIC_SLOT);
    if (rc != 0) {
        return rc;
    }

    (void)memset(&param, 0, sizeof(param));
    param.message.topic.topic.utf8 = (const uint8_t *)topic;
    param.message.topic.topic.size = strlen(topic);
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = payload_len;
    param.message_id = ++g_rt.last_message_id;
    param.dup_flag = 0U;
    param.retain_flag = 0U;

    rc = mqtt_publish(&g_rt.mqtt, &param);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int gw_cloud_pump(gw_cloud_client_t *client)
{
    int rc;

    if (client == NULL || !client->connected) {
        return -ENOTCONN;
    }

    if (g_rt.nfds > 0) {
        rc = poll(g_rt.fds, g_rt.nfds, 0);
        if (rc > 0) {
            rc = mqtt_input(&g_rt.mqtt);
            if (rc != 0) {
                client->connected = false;
                return rc;
            }
        }
    }

    rc = mqtt_live(&g_rt.mqtt);
    if (rc != 0 && rc != -EAGAIN) {
        client->connected = false;
        return rc;
    }

    if (rc == 0) {
        rc = mqtt_input(&g_rt.mqtt);
        if (rc != 0) {
            client->connected = false;
            return rc;
        }
    }

    return client->connected ? 0 : -ENOTCONN;
}

int gw_cloud_disconnect(gw_cloud_client_t *client)
{
    if (client == NULL || !client->initialized) {
        return -EINVAL;
    }

    if (client->connected) {
        (void)mqtt_disconnect(&g_rt.mqtt, NULL);
    }

    (void)memset(&g_rt, 0, sizeof(g_rt));
    client->connected = false;

    return 0;
}
