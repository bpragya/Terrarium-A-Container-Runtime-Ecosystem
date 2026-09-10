// terrarium — CLI entry point.
//   usage: sudo ./isolate hatch <name> [mem_limit_mb] [cmd...]
//   (cmd omitted => interactive /bin/sh; multiple args are joined with spaces
//   and run via /bin/sh -c, so quote the whole command as one shell argument
//   if it contains its own spaces, e.g. hatch pig 20 "x=x; while true; do x=\$x\$x; done")

#include <cstdio>
#include <cstdlib>
#include <string>

#include "container.h"
#include "errors.h"
#include "creatures.h"

int main(int argc, char* argv[]) {

    if (argc >= 2 && std::string(argv[1]) == "list") {
        if (!db_open("/root/terrarium/terrarium.db")) 
            return EXIT_DB;
        for (const auto& c : creature_list())
            printf("%-12s %-9s pid=%-6d %dMB\n",
                c.name.c_str(), c.status.c_str(), c.pid, c.mem_limit_mb);
        db_close();
        return EXIT_OK;
    }

    if (argc < 3 || std::string(argv[1]) != "hatch") {
        fprintf(stderr, "usage: %s hatch <name> [mem_limit_mb] [cmd...]\n", argv[0]);
        return EXIT_USAGE;
    }

    std::string name = argv[2];
    int mem_limit_mb = (argc > 3) ? atoi(argv[3]) : 0;   // 0 => container default

    std::string cmd;                       // remaining args joined => command to run
    for (int i = 4; i < argc; ++i) {
        if (i > 4) cmd += " ";
        cmd += argv[i];
    }

    return hatch(name, mem_limit_mb, cmd);
}
