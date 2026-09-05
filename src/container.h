#pragma once

#include <string>

// Hatch an isolated, memory-limited process running /bin/sh:
//   - private copy of the Alpine template as its rootfs
//   - new PID / mount / UTS namespaces
//   - a cgroup v2 with memory.max = mem_limit_mb (<= 0 means use the default)
// Blocks until the creature exits, then cleans up the cgroup and rootfs copy.
// Returns an ExitCode from errors.h (EXIT_OK on a normal run).
int hatch(const std::string& name, int mem_limit_mb);
