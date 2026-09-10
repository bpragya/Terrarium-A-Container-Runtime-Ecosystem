#pragma once

#include <string>

// Hatch an isolated, memory-limited process:
//   - private copy of the Alpine template as its rootfs
//   - new PID / mount / UTS namespaces
//   - a cgroup v2 with memory.max = mem_limit_mb (<= 0 means use the default)
//   - runs `cmd` via `/bin/sh -c` inside the creature; empty cmd => interactive /bin/sh
// Blocks until the creature exits, then cleans up the cgroup and rootfs copy.
// Returns an ExitCode from errors.h (EXIT_OK on a normal run).
int hatch(const std::string& name, int mem_limit_mb, const std::string& cmd = "");
