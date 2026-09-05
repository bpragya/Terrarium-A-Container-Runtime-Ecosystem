#pragma once

// Process exit codes for the terrarium runtime.
// 0 = success. Keep values in 1..125 (126+ are reserved by the shell).
// Unscoped enum so values convert straight to int in `return`.
enum ExitCode {
    EXIT_OK              = 0,
    EXIT_ERROR           = 1,   // generic failure
    EXIT_USAGE           = 2,   // bad command-line arguments
    EXIT_ROOTFS_COPY     = 3,   // failed to copy the alpine template
    EXIT_CLONE           = 4,   // clone() failed
    EXIT_CHILD_SETUP     = 5,   // chroot / chdir / mount failed in the child
    EXIT_EXEC            = 6,   // execlp() failed
};
