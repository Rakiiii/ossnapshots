#include <inc/lib.h>

void
umain(int argc, char **argv) {

    int res = print_snapshot_list();

    if (res < 0) {
        cprintf("Snapshot list cannot be printed\n");
    }
    
    return;
}