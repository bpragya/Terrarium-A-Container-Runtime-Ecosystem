#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static char stack[1024 * 1024];

static int child_fn(void *arg)
{
    return 0;
}

int main(int argc, char* argv[]) {

   pid_t pid = clone(child_fn, stack + sizeof(stack), CLONE_NEWPID | CLONE_NEWNS | SIGCHLD , nullptr);
   if (pid == -1)
   {
    perror("clone");
    return 1;
   }
   waitpid(pid, nullptr, 0); //parent blocks until child process exits
   return 0;
}
