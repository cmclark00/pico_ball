/*
 * Raw-TCP HTTP/1.1 server implementing exactly the endpoints a Nintendo DS
 * needs to (a) pass the WFC connection test, (b) authenticate against NAS, and
 * (c) run a Gen 4 GTS deposit/withdraw. Everything resolves here because the
 * companion DNS server answers every A query with this board's IP.
 *
 * Endpoint constants are grounded in AltWFC (dwc_network_server_emulator) for
 * conntest/NAS and IR-GTS for the GTS responses.
 */
#include "wfc_http.h"
#include "gts.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Case-insensitive strstr (newlib has no strcasestr). */
static const char *ci_strstr(const char *hay, const char *needle) {
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return hay;
    }
    return NULL;
}

#define HTTP_PORT      80
#define CONN_BUF       2048
#define MAX_CONNS      4

typedef struct {
    struct tcp_pcb *pcb;
    int             in_use;
    size_t          len;
    char            buf[CONN_BUF];
} http_conn_t;

static http_conn_t conns[MAX_CONNS];

static http_conn_t *conn_alloc(struct tcp_pcb *pcb) {
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!conns[i].in_use) {
            conns[i].in_use = 1;
            conns[i].pcb = pcb;
            conns[i].len = 0;
            return &conns[i];
        }
    }
    return NULL;
}

static void conn_free(http_conn_t *c) {
    if (c) { c->in_use = 0; c->pcb = NULL; c->len = 0; }
}

/* ---- helpers ------------------------------------------------------------- */

/* Percent-decode %XX in place into out; copies other bytes verbatim. Does NOT
 * turn '+' into space (GTS base64 query values are not form-encoded). */
static size_t url_decode(const char *in, size_t inlen, char *out, size_t outcap) {
    size_t n = 0;
    for (size_t i = 0; i < inlen && n < outcap; i++) {
        if (in[i] == '%' && i + 2 < inlen) {
            char h[3] = { in[i + 1], in[i + 2], 0 };
            char *end;
            long v = strtol(h, &end, 16);
            if (end == h + 2) { out[n++] = (char)v; i += 2; continue; }
        }
        out[n++] = in[i];
    }
    return n;
}

/* Find "name=" in the request line's query and copy its value (up to & or
 * space) into out. Returns value length, or -1 if absent. */
static int query_param(const char *req, const char *name, char *out, size_t outcap) {
    char key[24];
    int kl = snprintf(key, sizeof key, "%s=", name);
    const char *line_end = strstr(req, "\r\n");
    if (!line_end) line_end = req + strlen(req);
    const char *p = req;
    while ((p = strstr(p, key)) && p < line_end) {
        if (p == req || p[-1] == '?' || p[-1] == '&') {
            const char *v = p + kl;
            const char *e = v;
            while (e < line_end && *e != '&' && *e != ' ') e++;
            return (int)url_decode(v, (size_t)(e - v), out, outcap);
        }
        p += kl;
    }
    return -1;
}

/* Send a full response and close the connection. */
static void send_response(http_conn_t *c, const char *status,
                          const char *extra_headers,
                          const char *content_type,
                          const uint8_t *body, size_t blen) {
    char head[512];
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 %s\r\n"
        "%s"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, extra_headers ? extra_headers : "",
        content_type, (unsigned)blen);

    struct tcp_pcb *pcb = c->pcb;
    tcp_write(pcb, head, (u16_t)hl, TCP_WRITE_FLAG_COPY);
    if (body && blen) tcp_write(pcb, body, (u16_t)blen, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    conn_free(c);
    tcp_close(pcb);
}

static void send_bytes(http_conn_t *c, const uint8_t *b, size_t n) {
    send_response(c, "200 OK", NULL, "application/octet-stream", b, n);
}

/* ---- WFC: connection test ------------------------------------------------ */

static void handle_conntest(http_conn_t *c) {
    static const char html[] =
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
        "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
        "<html><head><title>HTML Page</title></head>"
        "<body bgcolor=\"#FFFFFF\">This is test.html page</body></html>";
    /* The X-Organization: Nintendo header is what stops the DS from flagging a
     * captive portal and failing the test. */
    send_response(c, "200 OK", "X-Organization: Nintendo\r\n",
                  "text/html", (const uint8_t *)html, sizeof html - 1);
}

/* ---- WFC: NAS authentication (/ac) --------------------------------------- *
 * Response values are base64-encoded with '=' replaced by '*', joined as
 * key=value pairs and terminated with CRLF; Content-Type text/plain plus the
 * NODE: wifiappe1 header. (dwc_network_server_emulator/nas_server.py) */
static void nas_add(char *out, size_t cap, size_t *n,
                    const char *key, const char *val) {
    char enc[160];
    int el = gts_b64_encode((const uint8_t *)val, strlen(val), enc, sizeof enc);
    for (int i = 0; i < el; i++) if (enc[i] == '=') enc[i] = '*';
    *n += snprintf(out + *n, cap - *n, "%s%s=%.*s",
                   *n ? "&" : "", key, el, enc);
}

static void handle_nas(http_conn_t *c, const char *body) {
    char out[512];
    size_t n = 0;
    /* action is base64('login')="bG9naW4*" / base64('acctcreate') starts
     * "YWNjdGNyZWF0". Detect without decoding the whole body. */
    int acctcreate = body && strstr(body, "YWNjdGNyZWF0") != NULL;

    if (acctcreate) {
        nas_add(out, sizeof out, &n, "retry",    "0");
        nas_add(out, sizeof out, &n, "returncd", "002");
        nas_add(out, sizeof out, &n, "userid",   "0000000000001");
        nas_add(out, sizeof out, &n, "datetime", "20260101000000");
    } else {
        nas_add(out, sizeof out, &n, "retry",    "0");
        nas_add(out, sizeof out, &n, "returncd", "001");
        nas_add(out, sizeof out, &n, "locator",  "gamespy.com");
        nas_add(out, sizeof out, &n, "challenge","00000000");
        nas_add(out, sizeof out, &n, "token",    "pokeballpokeballpokeball");
        nas_add(out, sizeof out, &n, "datetime", "20260101000000");
    }
    n += snprintf(out + n, sizeof out - n, "\r\n");
    printf("[NAS] %s -> %.*s", acctcreate ? "acctcreate" : "login", (int)n, out);
    send_response(c, "200 OK", "NODE: wifiappe1\r\n",
                  "text/plain", (const uint8_t *)out, n);
}

/* ---- GTS ----------------------------------------------------------------- */

static int path_ends(const char *path, size_t plen, const char *suffix) {
    size_t sl = strlen(suffix);
    return plen >= sl && memcmp(path + plen - sl, suffix, sl) == 0;
}

static void handle_gts(http_conn_t *c, const char *path, size_t plen,
                       const char *req) {
    if (path_ends(path, plen, "info.asp")) {
        printf("[GTS] info\n");
        const uint8_t r[] = { 0x01, 0x00 };
        send_bytes(c, r, sizeof r);
    } else if (path_ends(path, plen, "setProfile.asp")) {
        const uint8_t r[8] = { 0 };
        send_bytes(c, r, sizeof r);
    } else if (path_ends(path, plen, "post.asp")) {
        /* A deposit: the game uploads a Pokemon. Capture it. */
        char data[800];
        int dl = query_param(req, "data", data, sizeof data);
        if (dl > 0) {
            uint8_t blob[400];
            int bl = gts_b64_decode(data, (size_t)dl, blob, sizeof blob);
            uint8_t mon[GTS_PARTY_LEN];
            if (bl >= GTS_BLOB_LEN && gts_decrypt_pokemon(blob, bl, mon) == 0) {
                gts_on_deposit(mon);
            } else {
                printf("[GTS] post: bad blob (b64=%d dec=%d)\n", dl, bl);
            }
        }
        const uint8_t r[] = { 0x0c, 0x00 };
        send_bytes(c, r, sizeof r);
    } else if (path_ends(path, plen, "post_finish.asp")) {
        const uint8_t r[] = { 0x01, 0x00 };
        send_bytes(c, r, sizeof r);
    } else if (path_ends(path, plen, "search.asp")) {
        send_bytes(c, NULL, 0);
    } else if (path_ends(path, plen, "result.asp")) {
        /* The game polls for a completed trade. If a Pokemon is staged for
         * inject, hand it back here (UNVALIDATED path). Otherwise 05 00 =
         * "nothing waiting". */
        if (gts_inject_valid) {
            uint8_t pkt[GTS_BLOB_LEN];
            gts_encrypt_pokemon(gts_inject_mon, pkt);
            printf("[GTS] result: delivering staged inject\n");
            send_bytes(c, pkt, sizeof pkt);
        } else {
            const uint8_t r[] = { 0x05, 0x00 };
            send_bytes(c, r, sizeof r);
        }
    } else if (path_ends(path, plen, "delete.asp")) {
        const uint8_t r[] = { 0x01, 0x00 };
        send_bytes(c, r, sizeof r);
    } else {
        printf("[GTS] unhandled: %.*s\n", (int)plen, path);
        send_bytes(c, NULL, 0);
    }
}

/* ---- request dispatch ---------------------------------------------------- */

static void process(http_conn_t *c) {
    c->buf[c->len] = '\0';
    char *req = c->buf;

    /* Parse "METHOD SP path SP HTTP/1.1" */
    int is_post = strncmp(req, "POST ", 5) == 0;
    int is_get  = strncmp(req, "GET ", 4) == 0;
    if (!is_post && !is_get) { send_response(c, "400 Bad Request", NULL,
                                             "text/plain", NULL, 0); return; }

    const char *path = req + (is_post ? 5 : 4);
    const char *sp = strchr(path, ' ');
    size_t full_plen = sp ? (size_t)(sp - path) : strlen(path);
    /* path without query, for suffix matching */
    const char *q = memchr(path, '?', full_plen);
    size_t plen = q ? (size_t)(q - path) : full_plen;

    if (is_post && (strncmp(path, "/ac", 3) == 0 || strncmp(path, "/pr", 3) == 0)) {
        const char *body = strstr(req, "\r\n\r\n");
        handle_nas(c, body ? body + 4 : "");
        return;
    }
    if (plen == 1 && path[0] == '/') { handle_conntest(c); return; }
    if (strstr(path, "/pokemondpds/") || path_ends(path, plen, ".asp")) {
        handle_gts(c, path, plen, req);
        return;
    }
    /* Anything else a DS pings during setup: keep it happy with 200/empty. */
    send_bytes(c, NULL, 0);
}

/* Have we received a complete request? */
static int request_complete(http_conn_t *c) {
    c->buf[c->len < CONN_BUF ? c->len : CONN_BUF - 1] = '\0';
    char *he = strstr(c->buf, "\r\n\r\n");
    if (!he) return 0;
    if (strncmp(c->buf, "POST ", 5) != 0) return 1;   /* GET: headers are all */
    /* POST: need the body too. */
    const char *cl = ci_strstr(c->buf, "Content-Length:");
    if (!cl) return 1;
    long need = strtol(cl + 15, NULL, 10);
    size_t body_have = c->len - (size_t)(he + 4 - c->buf);
    return body_have >= (size_t)need;
}

/* ---- lwIP callbacks ------------------------------------------------------ */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_conn_t *c = (http_conn_t *)arg;
    if (!p) {                       /* remote closed */
        conn_free(c);
        tcp_arg(pcb, NULL);
        tcp_close(pcb);
        return ERR_OK;
    }
    if (err == ERR_OK && c) {
        for (struct pbuf *q = p; q; q = q->next) {
            size_t room = CONN_BUF - 1 - c->len;
            size_t take = q->len < room ? q->len : room;
            memcpy(c->buf + c->len, q->payload, take);
            c->len += take;
        }
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);

    if (c && c->in_use && request_complete(c)) process(c);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)err;
    conn_free((http_conn_t *)arg);
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    http_conn_t *c = conn_alloc(pcb);
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    return ERR_OK;
}

int wfc_http_init(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return -1;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) return -2;
    pcb = tcp_listen_with_backlog(pcb, MAX_CONNS);
    if (!pcb) return -3;
    tcp_accept(pcb, on_accept);
    printf("[HTTP] listening on :%d\n", HTTP_PORT);
    return 0;
}
