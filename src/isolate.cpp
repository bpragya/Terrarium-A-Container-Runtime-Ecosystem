// terrarium — CLI entry point.
//   usage: sudo ./isolate hatch <name> [mem_limit_mb]

#include <cstdio>
#include <cstdlib>
#include <string>

#include "container.h"
#include "errors.h"

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "hatch") {
        fprintf(stderr, "usage: %s hatch <name> [mem_limit_mb]\n", argv[0]);
        return EXIT_USAGE;
    }

    std::string name = argv[2];
    int mem_limit_mb = (argc > 3) ? atoi(argv[3]) : 0;   // 0 => container default

    return hatch(name, mem_limit_mb);
}
