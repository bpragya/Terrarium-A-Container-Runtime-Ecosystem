#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "errors.h"

// Everything the cloned child needs, passed through clone()'s void* arg.
struct ChildArgs {
    std::string rootfs;
    int mem_limit_mb;   // unused until cgroups land (next step)
};

// Copy the pristine Alpine template into this creature's own rootfs dir.
static std::string spawn_rootfs(const std::string& name) {
    std::string dst = "/root/terrarium/creatures/" + name;

    mkdir("/root/terrarium", 0755); // ok if already there (EEXIST)
    mkdir("/root/terrarium/creatures", 0755);

    system(("rm -rf \"" + dst + "\"").c_str());  // clear any stale copy

    std::string cmd = "cp -a /root/terrarium/template \"" + dst + "\""; // copy the alpine filesystem
    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "rootfs copy failed (rc=%d)\n", rc);
        return "";
    }
    return dst;
}

static char stack[1024 * 1024];

// Runs as PID 1 inside the new PID, mount, UTS namespaces.
static int child_fn(void* arg) {
    ChildArgs* a = static_cast<ChildArgs*>(arg);

    // make the / the alpine dir
    if (chroot(a->rootfs.c_str()) == -1)
    {
        perror("chroot");
        _exit(EXIT_CHILD_SETUP);
    }
    if (chdir("/") == -1)
    {
        perror("chdir");
        _exit(EXIT_CHILD_SETUP);
    }

    // mount filesystem onto process'es directory
    if (mount("proc", "/proc", "proc", 0, nullptr) == -1) {
        perror("mount /proc");
        _exit(EXIT_CHILD_SETUP);
    }

    execlp("/bin/sh", "sh", (char*)nullptr); // running the shell
    perror("execlp");   // only reached if exec failed
    _exit(EXIT_EXEC);
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "hatch") {
        fprintf(stderr, "usage: %s hatch <name> [mem_limit_mb]\n", argv[0]);
        return EXIT_USAGE;
    }
    std::string name = argv[2]; //get the name of the creature
    int mem_limit_mb = (argc > 3) ? atoi(argv[3]) : 0; // get the memory limit

    std::string dst = spawn_rootfs(name); // Alpine fs copy
    if (dst.empty())
    {
        return EXIT_ROOTFS_COPY;
    }

    ChildArgs args{dst, mem_limit_mb};

    pid_t pid = clone(child_fn, stack + sizeof(stack),
                      CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD,
                      &args); 
    if (pid == -1) 
    { 
        perror("clone"); 
        return EXIT_CLONE; 
    }

    waitpid(pid, nullptr, 0);   // parent blocks until the creature exits

    system(("rm -rf \"" + dst + "\"").c_str());   // cleanup after the creature exits
    return EXIT_OK;
}
