#include <stdio.h>
#include "db.h"
#include "server.h"

int main(void) {
    if (db_load("data") != 0) {
        printf("Cannot open data/ folder. Run the program from the project root folder.\n");
        return 1;
    }
    run_server(8080);
    return 0;
}
