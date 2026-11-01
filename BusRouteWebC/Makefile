SRC = src/main.c src/db.c src/backend.c src/server.c
busroute: $(SRC)
	gcc -Wall -Iinclude $(SRC) -o busroute
run: busroute
	./busroute
