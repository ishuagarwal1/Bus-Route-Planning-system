#ifndef BACKEND_H
#define BACKEND_H
#include "db.h"
enum { BY_DISTANCE = 0, BY_TIME = 1, BY_FARE = 2 };
typedef struct { int edges[MAX_STOPS]; int n; int dist, time, fare; } Plan;

int find_stop(const char *name);                       /* -1 if not found */
int plan_route(int src, int dst, int metric, Plan *p); /* Dijkstra; 0 ok, -1 no route */
int add_stop(const char *name);                        /* returns new id or -1 */
int add_route(int a, int b, int dist, int time, int fare, const char *bus);
int book_ticket(const char *passenger, int src, int dst, const Plan *p);
int cancel_booking(int id);
#endif
