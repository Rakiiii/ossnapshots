#include <inc/lib.h>

void
print_free() {
    int res = free_space_bytes();

    if (res < 0) {
        cprintf("[Errorr] Incorrect free space size!\n");
    } else if (res == 0) {
        cprintf("No free space on disk\n");
    } else {
        cprintf("free %d bytes on disk\n", res);
    }
}

void 
print_busy() {
    int res = busy_space_bytes();

    if (res < 0) {
        cprintf("[Errorr] Incorrect busy space size!\n");
    } else if (res == 0) {
        cprintf("No data on disk\n");
    } else {
        cprintf("busy %d bytes on disk\n", res);
    }
}

void
umain(int argc, char **argv) {

    if (argc != 2) {
        cprintf("Invalid amount of arguments for df\n");
        return;
    }
    
    struct Argstate args;

    argstart(&argc, argv, &args);

    int arg = argnext(&args);

    if (arg == 'f') {
        print_free();
    } else if (arg == 'b') {
        print_busy();
    } else {
        cprintf("Incorrect flag for df:'%c'\n", arg);
    }
    
    return;
}