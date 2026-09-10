#include "container.h"

#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "errors.h"
#include "creatures.h"
#include "states.h"


static constexpr int kDefaultMemLimitMb = 64;

// Run a shell command; warn (don't abort) if it fails. Used for best-effort cleanup.
static void run_quiet(const std::string& cmd) {
    if (system(cmd.c_str()) != 0)
        fprintf(stderr, "warning: command failed: %s\n", cmd.c_str());
}

// Everything the cloned child needs, passed through clone()'s void* arg.
struct ChildArgs {
    std::string rootfs;
    std::string cmd;   // empty => interactive /bin/sh; else run via /bin/sh -c
};

// Copy the pristine Alpine template into this creature's own rootfs dir.
// Returns the new path, or "" on failure.
static std::string spawn_rootfs(const std::string& name) {
    std::string dst = "/root/terrarium/creatures/" + name;

    mkdir("/root/terrarium", 0755);              // ok if already there (EEXIST)
    mkdir("/root/terrarium/creatures", 0755);

    run_quiet("rm -rf \"" + dst + "\"");  // clear any stale copy

    std::string cmd = "cp -a /root/terrarium/template \"" + dst + "\""; // copy the alpine filesystem
    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "rootfs copy failed (rc=%d)\n", rc);
        return "";
    }
    return dst;
}

// Write text to a cgroup pseudo-file in a single write() call.
// fopen/fprintf can split the write and the kernel rejects partial values.
static bool write_file(const std::string& path, const std::string& text) {
    int fd = open(path.c_str(), O_WRONLY);
    if (fd == -1) {
        perror(("open " + path).c_str());
        return false;
    }
    ssize_t n = write(fd, text.c_str(), text.size());
    close(fd);
    if (n != (ssize_t)text.size()) {
        perror(("write " + path).c_str());
        return false;
    }
    return true;
}

// Create a cgroup for this creature, cap its memory, move the child in.
// Returns the cgroup dir path, or "" on failure.
static std::string setup_cgroup(pid_t pid, int mem_limit_mb) {
    std::string dir = "/sys/fs/cgroup/terrarium-" + std::to_string(pid);

    if (mkdir(dir.c_str(), 0755) == -1) {
        perror("mkdir cgroup");
        return "";
    }

    if (!write_file(dir + "/memory.max", std::to_string(mem_limit_mb) + "M\n"))
        return "";

    // Moving the PID into cgroup.procs also moves its threads/children.
    if (!write_file(dir + "/cgroup.procs", std::to_string(pid) + "\n"))
        return "";

    return dir;
}

// Read the `oom_kill` counter from a cgroup's memory.events.
// >0 means the kernel OOM-killed at least one process in this cgroup.
// This is how we tell "died because it blew its memory limit" from
// "died any other way" — the exit signal can't tell them apart, since a
// cgroup OOM and an outside `kill -9` both arrive as SIGKILL.
static long read_oom_kill_count(const std::string& cgdir) {
    FILE* f = fopen((cgdir + "/memory.events").c_str(), "r");
    if (!f)
        return 0;
    char key[64];
    long val = 0, oom = 0;
    while (fscanf(f, "%63s %ld", key, &val) == 2) {
        if (std::string(key) == "oom_kill") {
            oom = val;
            break;
        }
    }
    fclose(f);
    return oom;
}

static char stack[1024 * 1024];

// Runs as PID 1 inside the new PID / mount / UTS namespaces.
static int child_fn(void* arg) {
    ChildArgs* a = static_cast<ChildArgs*>(arg);

    // make the / the alpine dir
    if (chroot(a->rootfs.c_str()) == -1) {
        perror("chroot");
        _exit(EXIT_CHILD_SETUP);
    }
    if (chdir("/") == -1) {
        perror("chdir");
        _exit(EXIT_CHILD_SETUP);
    }

    // mount filesystem onto process'es directory
    if (mount("proc", "/proc", "proc", 0, nullptr) == -1) {
        perror("mount /proc");
        _exit(EXIT_CHILD_SETUP);
    }

    if (a->cmd.empty())
        execlp("/bin/sh", "sh", (char*)nullptr);            // interactive shell
    else
        execlp("/bin/sh", "sh", "-c", a->cmd.c_str(), (char*)nullptr); // run the given command
    perror("execlp");   // only reached if exec failed
    _exit(EXIT_EXEC);
}

int hatch(const std::string& name, int mem_limit_mb, const std::string& cmd) {
    std::string dst = spawn_rootfs(name);
    if (dst.empty())
        return EXIT_ROOTFS_COPY;

    ChildArgs args{dst, cmd};

    pid_t pid = clone(child_fn, stack + sizeof(stack),
                      CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | SIGCHLD,
                      &args);
    if (pid == -1) {
        perror("clone");
        run_quiet("rm -rf \"" + dst + "\"");
        return EXIT_CLONE;
    }

    int limit = (mem_limit_mb > 0) ? mem_limit_mb : kDefaultMemLimitMb;
    std::string cg = setup_cgroup(pid, limit); // create a cgroup with memory limit
    if (cg.empty()) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        run_quiet("rm -rf \"" + dst + "\"");
        return EXIT_CGROUP;
    }

    if (!db_open("/root/terrarium/terrarium.db")) {
        run_quiet("rm -rf \"" + dst + "\"");
        return EXIT_DB;
    }

    int cid = creature_insert(name, limit);
    if (cid < 0) {                           // e.g. duplicate name — abandon the hatch
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        rmdir(cg.c_str());
        db_close();
        run_quiet("rm -rf \"" + dst + "\"");
        return EXIT_DB;
    }
    creature_set_pid(cid, pid);              // fill in the pid column

    int status;
    waitpid(pid, &status, 0);   // parent blocks until the creature exits

    // Was it the memory limit that killed it? Ask the cgroup, not the signal:
    // a cgroup OOM-kill and an outside `kill -9` (how `release` stops a creature,
    // since SIGTERM to a PID-namespace init is silently dropped) both show up as
    // SIGKILL. oom_kill > 0 => it blew its limit => SLEEPING; otherwise RELEASED.
    const char* final_status =
        (read_oom_kill_count(cg) > 0) ? state::SLEEPING : state::RELEASED;

    if (WIFSIGNALED(status))
        fprintf(stderr, "creature stopped (signal %d) -> %s\n",
                WTERMSIG(status), final_status);
    else if (WIFEXITED(status))
        fprintf(stderr, "creature exited (status %d) -> %s\n",
                WEXITSTATUS(status), final_status);

    creature_set_status(cid, final_status);
    db_close();

    rmdir(cg.c_str());                       // cgroup is empty now the child is gone
    run_quiet("rm -rf \"" + dst + "\"");     // cleanup the rootfs copy
    return EXIT_OK;
}
