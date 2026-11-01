/* HTTP SERVER + REST API in C (sockets). Serves the website in public/ and the JSON API under /api */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#ifdef _WIN32
  #include <winsock2.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <sys/time.h>
  #include <unistd.h>
  #include <signal.h>
  typedef int sock_t;
  #define CLOSESOCK close
  #define INVALID_SOCKET (-1)
#endif
#include "backend.h"
#include "server.h"

#define ADMIN_KEY "admin123"
#define REQ_MAX 16384

/* ---------- growable output buffer + JSON helpers ---------- */
typedef struct { char *p; size_t n, cap; } Buf;

static void bappend(Buf *b, const char *s, size_t len) {
    if (b->n + len + 1 > b->cap) { b->cap = (b->n + len + 1) * 2; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, s, len); b->n += len; b->p[b->n] = 0;
}
static void bcat(Buf *b, const char *fmt, ...) {
    char tmp[512]; va_list ap;
    va_start(ap, fmt); int len = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (len > 0) bappend(b, tmp, len >= (int)sizeof tmp ? sizeof tmp - 1 : (size_t)len);
}
static void jstr(Buf *b, const char *s) {
    bappend(b, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { bappend(b, "\\", 1); bappend(b, s, 1); }
        else if ((unsigned char)*s >= 32) bappend(b, s, 1);
    }
    bappend(b, "\"", 1);
}
static int err(Buf *o, int code, const char *msg) { o->n = 0; bcat(o, "{\"error\":"); jstr(o, msg); bcat(o, "}"); return code; }

/* read "key":"value" or "key":123 from a flat JSON body */
static int jget(const char *body, const char *key, char *out, size_t size) {
    char pat[48]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat); out[0] = 0;
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    int quoted = (*p == '"'); if (quoted) p++;
    size_t i = 0;
    for (; *p && i + 1 < size; p++) {
        if (quoted ? *p == '"' : (*p == ',' || *p == '}' || *p == ' ')) break;
        if (*p != '\\' && *p != '|' && (unsigned char)*p >= 32) out[i++] = *p;
    }
    out[i] = 0; return 1;
}
static int qget(const char *q, const char *key, char *out, size_t size) {   /* query string: a=1&b=2 */
    size_t kl = strlen(key); out[0] = 0;
    for (const char *p = q; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            size_t i = 0; p += kl + 1;
            while (*p && *p != '&' && i + 1 < size) out[i++] = *p++;
            out[i] = 0; return 1;
        }
        p = strchr(p, '&'); if (p) p++;
    }
    return 0;
}
static int parse_id(const char *s) { int v = atoi(s); return (v >= 1 && v <= DB.nstops) ? v - 1 : -1; }
static int metric_of(const char *s) { return !strcmp(s, "time") ? BY_TIME : !strcmp(s, "fare") ? BY_FARE : BY_DISTANCE; }

static void write_plan(Buf *o, int src, const Plan *p, int booking_id) {
    int cur = src;
    bcat(o, "{");
    if (booking_id > 0) bcat(o, "\"id\":%d,", booking_id);
    bcat(o, "\"steps\":[");
    for (int i = 0; i < p->n; i++) {
        const Edge *e = &DB.edges[p->edges[i]];
        int next = (e->from == cur) ? e->to : e->from;
        if (i) bcat(o, ",");
        bcat(o, "{\"from\":"); jstr(o, DB.stops[cur].name);
        bcat(o, ",\"to\":");   jstr(o, DB.stops[next].name);
        bcat(o, ",\"bus\":");  jstr(o, e->bus);
        bcat(o, ",\"dist\":%d,\"time\":%d,\"fare\":%d}", e->dist, e->time, e->fare);
        cur = next;
    }
    bcat(o, "],\"dist\":%d,\"time\":%d,\"fare\":%d}", p->dist, p->time, p->fare);
}

/* ---------- REST API ---------- */
static int api(const char *method, const char *path, const char *query, const char *body, int is_admin, Buf *o) {
    char a[64], b[64], c[64], d[64], e[64], f[64];
    int get = !strcmp(method, "GET"), post = !strcmp(method, "POST");

    if (get && !strcmp(path, "/api/stops")) {
        bcat(o, "[");
        for (int i = 0; i < DB.nstops; i++) { if (i) bcat(o, ","); bcat(o, "{\"id\":%d,\"name\":", i + 1); jstr(o, DB.stops[i].name); bcat(o, "}"); }
        bcat(o, "]"); return 200;
    }
    if (get && !strcmp(path, "/api/routes")) {
        bcat(o, "[");
        for (int i = 0; i < DB.nedges; i++) {
            const Edge *r = &DB.edges[i];
            if (i) bcat(o, ",");
            bcat(o, "{\"id\":%d,\"bus\":", i + 1); jstr(o, r->bus);
            bcat(o, ",\"from\":"); jstr(o, DB.stops[r->from].name);
            bcat(o, ",\"to\":");   jstr(o, DB.stops[r->to].name);
            bcat(o, ",\"dist\":%d,\"time\":%d,\"fare\":%d}", r->dist, r->time, r->fare);
        }
        bcat(o, "]"); return 200;
    }
    if (get && !strcmp(path, "/api/plan")) {
        qget(query, "from", a, sizeof a); qget(query, "to", b, sizeof b); qget(query, "by", c, sizeof c);
        int s = parse_id(a), t = parse_id(b); Plan p;
        if (s < 0 || t < 0 || s == t) return err(o, 400, "Choose two different stops");
        if (plan_route(s, t, metric_of(c), &p) != 0) return err(o, 404, "No route available");
        write_plan(o, s, &p, 0); return 200;
    }
    if (get && !strcmp(path, "/api/bookings")) {
        bcat(o, "[");
        for (int i = DB.nbookings - 1; i >= 0; i--) {
            const Booking *k = &DB.bookings[i];
            bcat(o, "{\"id\":%d,\"passenger\":", k->id); jstr(o, k->passenger);
            bcat(o, ",\"from\":"); jstr(o, DB.stops[k->from].name);
            bcat(o, ",\"to\":");   jstr(o, DB.stops[k->to].name);
            bcat(o, ",\"dist\":%d,\"time\":%d,\"fare\":%d,\"created\":", k->dist, k->time, k->fare); jstr(o, k->date);
            bcat(o, "}"); if (i) bcat(o, ",");
        }
        if (DB.nbookings && o->p[o->n - 1] == ',') o->p[--o->n] = 0;   /* (safety, no trailing comma) */
        bcat(o, "]"); return 200;
    }
    if (post && !strcmp(path, "/api/bookings")) {
        jget(body, "passenger", a, sizeof a); jget(body, "from", b, sizeof b);
        jget(body, "to", c, sizeof c);        jget(body, "by", d, sizeof d);
        int s = parse_id(b), t = parse_id(c); Plan p;
        if (!a[0] || s < 0 || t < 0 || s == t) return err(o, 400, "Passenger name and two different stops required");
        if (plan_route(s, t, metric_of(d), &p) != 0) return err(o, 404, "No route available");
        int id = book_ticket(a, s, t, &p);
        if (id < 0) return err(o, 500, "Could not save booking");
        write_plan(o, s, &p, id); return 201;
    }
    if (!strcmp(method, "DELETE") && !strncmp(path, "/api/bookings/", 14)) {
        if (cancel_booking(atoi(path + 14)) != 0) return err(o, 404, "Booking not found");
        bcat(o, "{\"ok\":true}"); return 200;
    }
    if (post && (!strcmp(path, "/api/stops") || !strcmp(path, "/api/routes"))) {
        if (!is_admin) return err(o, 401, "Wrong admin password");
        if (!strcmp(path, "/api/stops")) {
            jget(body, "name", a, sizeof a);
            if (!a[0]) return err(o, 400, "Name required");
            if (add_stop(a) < 0) return err(o, 409, "Already exists or list is full");
        } else {
            jget(body, "from", a, sizeof a); jget(body, "to", b, sizeof b); jget(body, "dist", c, sizeof c);
            jget(body, "time", d, sizeof d); jget(body, "fare", e, sizeof e); jget(body, "bus", f, sizeof f);
            if (!f[0] || add_route(parse_id(a), parse_id(b), atoi(c), atoi(d), atoi(e), f) != 0) return err(o, 400, "Invalid route data");
        }
        bcat(o, "{\"ok\":true}"); return 201;
    }
    return err(o, 404, "Unknown API endpoint");
}

/* ---------- HTTP plumbing ---------- */
static const char *status_text(int c) {
    switch (c) { case 200: return "OK"; case 201: return "Created"; case 400: return "Bad Request"; case 401: return "Unauthorized";
                 case 403: return "Forbidden"; case 404: return "Not Found"; case 409: return "Conflict"; default: return "Server Error"; }
}
static void send_all(sock_t s, const char *d, size_t n) {
    while (n > 0) { int r = send(s, d, (int)n, 0); if (r <= 0) return; d += r; n -= r; }
}
static void respond(sock_t s, int code, const char *type, const char *data, size_t len) {
    char h[256];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
                     code, status_text(code), type, (unsigned long)len);
    send_all(s, h, n); send_all(s, data, len);
}
static int ci_prefix(const char *s, const char *pre) {
    while (*pre) { if (tolower((unsigned char)*s++) != tolower((unsigned char)*pre++)) return 0; }
    return 1;
}
static const char *find_header(const char *req, const char *name) {
    const char *p = strstr(req, "\r\n");
    while (p && p[0] == '\r' && p[1] == '\n' && !(p[2] == '\r')) {
        p += 2;
        if (ci_prefix(p, name)) { p += strlen(name); while (*p == ' ') p++; return p; }
        p = strstr(p, "\r\n");
    }
    return NULL;
}
static const char *mime_of(const char *path) {
    const char *x = strrchr(path, '.');
    if (!x) return "application/octet-stream";
    if (!strcmp(x, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(x, ".css")) return "text/css";
    if (!strcmp(x, ".js")) return "text/javascript";
    if (!strcmp(x, ".svg")) return "image/svg+xml";
    return "application/octet-stream";
}
static void serve_file(sock_t s, const char *url) {
    char file[300];
    if (strstr(url, "..") || strlen(url) > 200) { respond(s, 403, "text/plain", "Forbidden", 9); return; }
    snprintf(file, sizeof file, "public%s%s", url, strcmp(url, "/") ? "" : "index.html");
    FILE *f = fopen(file, "rb");
    if (!f) { respond(s, 404, "text/plain", "Not found", 9); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *data = malloc(n + 1);
    size_t got = fread(data, 1, n, f); fclose(f);
    respond(s, 200, mime_of(file), data, got);
    free(data);
}
static void handle_client(sock_t c) {
    static char req[REQ_MAX];
    int n = 0, hdr_end = -1, need = 0;
    while (n < REQ_MAX - 1) {
        int r = recv(c, req + n, REQ_MAX - 1 - n, 0);
        if (r <= 0) break;
        n += r; req[n] = 0;
        if (hdr_end < 0) {
            char *e = strstr(req, "\r\n\r\n");
            if (e) { hdr_end = (int)(e - req) + 4; const char *cl = find_header(req, "content-length:"); need = cl ? atoi(cl) : 0; }
        }
        if (hdr_end >= 0 && n >= hdr_end + need) break;
    }
    if (hdr_end < 0) return;

    char method[8], target[1024];
    if (sscanf(req, "%7s %1023s", method, target) != 2) return;
    char *query = strchr(target, '?');
    if (query) *query++ = 0; else query = target + strlen(target);

    if (!strncmp(target, "/api/", 5)) {
        const char *key = find_header(req, "x-admin-key:");
        int is_admin = key && !strncmp(key, ADMIN_KEY, strlen(ADMIN_KEY)) && (key[strlen(ADMIN_KEY)] == '\r');
        Buf o = {0};
        int code = api(method, target, query, req + hdr_end, is_admin, &o);
        respond(c, code, "application/json", o.p ? o.p : "{}", o.n);
        free(o.p);
    } else if (!strcmp(method, "GET")) serve_file(c, target);
    else respond(c, 400, "text/plain", "Bad request", 11);
}

void run_server(int port) {
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
#ifndef _WIN32
    int yes = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
#endif
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(0x7F000001); addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 16) != 0) {
        printf("Cannot start server on port %d (is it already running?)\n", port); return;
    }
    printf("Bus Route Planner (C server) running at http://localhost:%d\nPress Ctrl+C to stop.\n", port);
    fflush(stdout);
    for (;;) {
        sock_t c = accept(srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
#ifdef _WIN32
        DWORD tv = 2000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
#else
        struct timeval tv = {2, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
#endif
        handle_client(c);
        CLOSESOCK(c);
    }
}
