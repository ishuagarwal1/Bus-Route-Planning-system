/* DATABASE LAYER: simple text-file storage (data/stops.txt, routes.txt, bookings.txt) */
#include <stdio.h>
#include <string.h>
#include "db.h"

Database DB;
static char g_dir[200] = "data";

static void path(char *out, const char *file) { snprintf(out, 300, "%s/%s", g_dir, file); }

int db_load(const char *dir) {
    char p[300], line[256];
    FILE *f;
    strncpy(g_dir, dir, sizeof g_dir - 1);
    memset(&DB, 0, sizeof DB);
    DB.next_booking_id = 1;

    path(p, "stops.txt");
    if (!(f = fopen(p, "r"))) return -1;
    while (DB.nstops < MAX_STOPS && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) strncpy(DB.stops[DB.nstops++].name, line, NAME_LEN - 1);
    }
    fclose(f);

    path(p, "routes.txt");
    if (!(f = fopen(p, "r"))) return -1;
    while (DB.nedges < MAX_EDGES && fgets(line, sizeof line, f)) {
        Edge e;
        if (sscanf(line, "%d|%d|%d|%d|%d|%11[^|\r\n]", &e.from, &e.to, &e.dist, &e.time, &e.fare, e.bus) == 6)
            DB.edges[DB.nedges++] = e;
    }
    fclose(f);

    path(p, "bookings.txt");
    if ((f = fopen(p, "r"))) {
        while (DB.nbookings < MAX_BOOKINGS && fgets(line, sizeof line, f)) {
            Booking b;
            if (sscanf(line, "%d|%39[^|]|%d|%d|%d|%d|%d|%19[^|\r\n]", &b.id, b.passenger, &b.from, &b.to,
                       &b.dist, &b.time, &b.fare, b.date) == 8) {
                DB.bookings[DB.nbookings++] = b;
                if (b.id >= DB.next_booking_id) DB.next_booking_id = b.id + 1;
            }
        }
        fclose(f);
    }
    return 0;
}

int db_save(void) {
    char p[300];
    FILE *f;
    int i;
    path(p, "stops.txt");
    if (!(f = fopen(p, "w"))) return -1;
    for (i = 0; i < DB.nstops; i++) fprintf(f, "%s\n", DB.stops[i].name);
    fclose(f);

    path(p, "routes.txt");
    if (!(f = fopen(p, "w"))) return -1;
    for (i = 0; i < DB.nedges; i++) {
        Edge *e = &DB.edges[i];
        fprintf(f, "%d|%d|%d|%d|%d|%s\n", e->from, e->to, e->dist, e->time, e->fare, e->bus);
    }
    fclose(f);

    path(p, "bookings.txt");
    if (!(f = fopen(p, "w"))) return -1;
    for (i = 0; i < DB.nbookings; i++) {
        Booking *b = &DB.bookings[i];
        fprintf(f, "%d|%s|%d|%d|%d|%d|%d|%s\n", b->id, b->passenger, b->from, b->to, b->dist, b->time, b->fare, b->date);
    }
    fclose(f);
    return 0;
}
