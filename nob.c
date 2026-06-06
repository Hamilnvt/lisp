#define NOB_IMPLEMENTATION
#include "../../nob/nob-latest"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cflags(Nob_Cmd *cmd) { nob_cmd_append(cmd, "-Wall", "-Wextra"); }
void debugger(Nob_Cmd *cmd) { nob_cmd_append(cmd, "-ggdb"); }
void output(Nob_Cmd *cmd) { nob_cmd_append(cmd, "-o", "lisp"); }
void cfiles(Nob_Cmd *cmd) { nob_cmd_append(cmd, ""); }

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, "cc"); // compile
    cflags(&cmd);
    output(&cmd);
    debugger(&cmd);
    nob_cmd_append(&cmd, "main.c"); // main file
    //cfiles(&cmd);

    if (!nob_cmd_run_sync_and_reset(&cmd)) return 1;

    return 0;
}

