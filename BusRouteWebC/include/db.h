#ifndef DB_H
#define DB_H
#define MAX_STOPS 100
#define MAX_EDGES 500
#define MAX_BOOKINGS 1000
#define NAME_LEN 40

typedef struct { char name[NAME_LEN]; } Stop;                 /* id = array index */
typedef struct { int from, to, dist, time, fare; char bus[12]; } Edge;
typedef struct { int id; char passenger[NAME_LEN]; int from, to, dist, time, fare; char date[20]; } Booking;

typedef struct {
    Stop stops[MAX_STOPS];          int nstops;
    Edge edges[MAX_EDGES];          int nedges;
    Booking bookings[MAX_BOOKINGS]; int nbookings;
    int next_booking_id;
} Database;

extern Database DB;
int db_load(const char *dir);   /* returns 0 on success */
int db_save(void);
#endif
