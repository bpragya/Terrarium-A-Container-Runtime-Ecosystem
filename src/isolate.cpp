// terrarium — CLI entry point.
//   usage: sudo ./isolate hatch <name> [mem_limit_mb]

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
        fprintf(stderr, "usage: %s hatch <name> [mem_limit_mb]\n", argv[0]);
        return EXIT_USAGE;
    }



    std::string name = argv[2];
    int mem_limit_mb = (argc > 3) ? atoi(argv[3]) : 0;   // 0 => container default

    return hatch(name, mem_limit_mb);
}
