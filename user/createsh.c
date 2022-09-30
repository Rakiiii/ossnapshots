#include <inc/lib.h>

void
umain(int argc, char **argv) {

    if (argc < 3) { 
        cprintf("Not enough arguments for creating snapshot\n");
        for(int i = 0; i < argc; ++i) {
            cprintf("       arg position %d argument:%s\n",i, argv[i]);
        }
        return;
    } else if (argc > 3) {
        cprintf("Too much arguments for creating snapshot\n");
        for(int i = 0; i < argc; ++i) {
            cprintf("       arg position %d argument:%s\n",i, argv[i]);
        }
        return;
    }
    
    int i;
    struct Argstate args;

    char *comment = NULL;
    char *name = NULL;

    argstart(&argc, argv, &args);
    while ((i = argnext(&args)) >= 0) {
        switch (i) {
        case 'c':
            comment = argnextvalue(&args);
            comment++;
            break;
        case 'n':
            name = argnextvalue(&args);
            name++;
            break;
        default:
            cprintf("Invalid arguments for creating snapshot\n");
            return;
        }
    }

    int res = create_snapshot(comment, name);

    if (res < 0) {
        cprintf("Snapshot is not created\n");
    }
    

    return;
}