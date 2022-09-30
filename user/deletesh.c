#include <inc/lib.h>

void
umain(int argc, char **argv) {

    if (argc < 2) { 
        cprintf("Not enough arguments for deleting snapshot\n");
        for(int i = 0; i < argc; ++i) {
            cprintf("       arg position %d argument:%s\n",i, argv[i]);
        }
        return;
    } else if (argc > 2) {
        cprintf("Too much arguments for deleting snapshot\n");
        for(int i = 0; i < argc; ++i) {
            cprintf("       arg position %d argument:%s\n",i, argv[i]);
        }
        return;
    }

    int res = delete_snapshot(argv[1]);

    if (res < 0) {
        cprintf("Snapshot is not deleted\n");
    }
    

    return;
}