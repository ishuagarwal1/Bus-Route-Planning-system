#!/bin/sh
cd "$(dirname "$0")"
gcc -Wall -Iinclude src/main.c src/db.c src/backend.c src/server.c -o busroute && ./busroute
