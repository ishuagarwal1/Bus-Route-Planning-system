/* BACKEND LAYER: business logic (Dijkstra route planning, booking) */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include "backend.h"

static int ci_equal(const char *a, const char *b) {
    while (*a && *b) { if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0; }
    return *a == *b;
}

int find_stop(const char *name) {
    for (int i = 0; i < DB.nstops; i++) if (ci_equal(DB.stops[i].name, name)) return i;
    return -1;
}

static int edge_cost(const Edge *e, int metric) {
    return metric == BY_TIME ? e->time : metric == BY_FARE ? e->fare : e->dist;
}

int plan_route(int src, int dst, int metric, Plan *p) {
    long d[MAX_STOPS];
    int prev_e[MAX_STOPS], prev_n[MAX_STOPS], done[MAX_STOPS] = {0};
    int n = DB.nstops;
    for (int i = 0; i < n; i++) { d[i] = LONG_MAX; prev_e[i] = prev_n[i] = -1; }
    d[src] = 0;
    for (int it = 0; it < n; it++) {
        int u = -1;
        for (int i = 0; i < n; i++) if (!done[i] && d[i] != LONG_MAX && (u < 0 || d[i] < d[u])) u = i;
        if (u < 0) break;
        done[u] = 1;
        for (int k = 0; k < DB.nedges; k++) {
            const Edge *e = &DB.edges[k];
            int v = (e->from == u) ? e->to : (e->to == u) ? e->from : -1;   /* two-way routes */
            if (v < 0 || done[v]) continue;
            long nd = d[u] + edge_cost(e, metric);
            if (nd < d[v]) { d[v] = nd; prev_e[v] = k; prev_n[v] = u; }
        }
    }
    if (d[dst] == LONG_MAX) return -1;

    int tmp[MAX_STOPS], cnt = 0;
    for (int v = dst; v != src; v = prev_n[v]) tmp[cnt++] = prev_e[v];
    p->n = cnt; p->dist = p->time = p->fare = 0;
    for (int i = 0; i < cnt; i++) {
        int k = tmp[cnt - 1 - i];
        p->edges[i] = k;
        p->dist += DB.edges[k].dist; p->time += DB.edges[k].time; p->fare += DB.edges[k].fare;
    }
    return 0;
}

int add_stop(const char *name) {
    if (!name[0] || DB.nstops >= MAX_STOPS || find_stop(name) >= 0) return -1;
    strncpy(DB.stops[DB.nstops].name, name, NAME_LEN - 1);
    DB.nstops++;
    return db_save() == 0 ? DB.nstops - 1 : -1;
}

int add_route(int a, int b, int dist, int time, int fare, const char *bus) {
    if (a == b || a < 0 || b < 0 || a >= DB.nstops || b >= DB.nstops) return -1;
    if (dist <= 0 || time <= 0 || fare < 0 || DB.nedges >= MAX_EDGES) return -1;
    Edge *e = &DB.edges[DB.nedges++];
    e->from = a; e->to = b; e->dist = dist; e->time = time; e->fare = fare;
    strncpy(e->bus, bus, sizeof e->bus - 1); e->bus[sizeof e->bus - 1] = 0;
    return db_save();
}

int book_ticket(const char *passenger, int src, int dst, const Plan *p) {
    if (DB.nbookings >= MAX_BOOKINGS) return -1;
    Booking *b = &DB.bookings[DB.nbookings++];
    b->id = DB.next_booking_id++;
    strncpy(b->passenger, passenger, NAME_LEN - 1); b->passenger[NAME_LEN - 1] = 0;
    b->from = src; b->to = dst; b->dist = p->dist; b->time = p->time; b->fare = p->fare;
    time_t t = time(NULL);
    strftime(b->date, sizeof b->date, "%Y-%m-%d %H:%M", localtime(&t));
    if (db_save() != 0) return -1;
    return b->id;
}

int cancel_booking(int id) {
    for (int i = 0; i < DB.nbookings; i++) {
        if (DB.bookings[i].id == id) {
            memmove(&DB.bookings[i], &DB.bookings[i + 1], (DB.nbookings - i - 1) * sizeof(Booking));
            DB.nbookings--;
            return db_save();
        }
    }
    return -1;
}
