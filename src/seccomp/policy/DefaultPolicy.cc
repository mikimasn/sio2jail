#include "DefaultPolicy.h"
#include "seccomp/action/ActionKill.h"
#include "seccomp/action/ActionErrno.h"
#include "seccomp/action/ActionTrace.h"
#include "seccomp/action/ActionAllow.h"
#include "seccomp/filter/LibSeccompFilter.h"

#include <fcntl.h>

namespace s2j {
namespace seccomp {
namespace policy {

DefaultPolicy::DefaultPolicy() {
    addExecutionControlRules();
    addMemoryManagementRules();
    addSystemInformationRules();
    addFileSystemAccessRules();
    addInputOutputRules();
}

const std::vector<SeccompRule>& DefaultPolicy::getRules() const {
    return rules_;
}

void DefaultPolicy::addExecutionControlRules(bool allowFork) {
    // Some syscalls must be enabled
    allowSyscalls({
            "restart_syscall",
            "getpriority",
            "setpriority",
            "sigaction",
            "sigaltstack",
            "rt_sigaction",
            "rt_sigprocmask",
            "futex",
            "set_tid_address",
            "set_robust_list",
            "getpid",
            "getrandom",
            "sigaltstack",
            "sigsuspend"
            });

    rules_.emplace_back(SeccompRule(
                "set_thread_area",
                action::ActionTrace([](auto& /* tracee */) {
                    // Allow syscall, let sio2jail detect syscall architecture
                    return tracer::TraceAction::CONTINUE;
                })));

    rules_.emplace_back(SeccompRule(
                "execve",
                action::ActionTrace([executed = false](auto& tracee) mutable {
                    if (executed)
                        return tracer::TraceAction::KILL;
                    executed = true;
                    return tracer::TraceAction::CONTINUE;
                })));

    for (const auto& syscall: {
            "kill",
            "tkill"}) {
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionTrace([](auto& tracee) {
                        if (isSignalValid(tracee.getSyscallArgument(1)))
                            return tracer::TraceAction::CONTINUE;
                        else
                            return tracer::TraceAction::KILL;
                    })));
    }

    rules_.emplace_back(SeccompRule(
                "tgkill",
                action::ActionTrace([](auto& tracee) {
                    if (isSignalValid(tracee.getSyscallArgument(2)))
                        return tracer::TraceAction::CONTINUE;
                    else
                        return tracer::TraceAction::KILL;
                })));
    for (const auto& syscall: {
            "exit",
            "exit_group",
            }) {
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionTrace()));
    }

    if (allowFork) {
        allowSyscalls({"fork"});
    }

    // Others may be always unaccessible
    for (const auto& syscall: {
            "prlimit64"
            }) {
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionErrno(EPERM)));
    }
}

void DefaultPolicy::addMemoryManagementRules() {
    allowSyscalls({
            "brk",
            "mmap",
            "mmap2",
            "munmap",
            "mremap",
            "mprotect",
            "arch_prctl"
            });
}

void DefaultPolicy::addSystemInformationRules() {
    allowSyscalls({
            "getuid",
            "getgid",
            "geteuid",
            "getegid",
            "getrlimit",
            "ugetrlimit",
            "getcpu",
            "gettid",
            "uname",
            "olduname",
            "oldolduname",
            "sysinfo",
            "clock_gettime",
            "gettimeofday",
            "time"
            });
}

void DefaultPolicy::addInputOutputRules() {
    // Allow writing to stdout and stderr
    for (const auto& syscall: {
            "write",
            "writev"
            }) {
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionAllow(),
                    filter::SyscallArg(0) > 0));
    }

    rules_.emplace_back(SeccompRule(
                "dup2",
                action::ActionAllow(),
                filter::SyscallArg(1) >= 3));

    // Allow reading from any file descriptor
    allowSyscalls({
            "read",
            "readv",
            "dup",
            "fcntl",
            "fcntl64"
            });

    rules_.emplace_back(SeccompRule(
                "ioctl",
                action::ActionErrno(ENOTTY)));

    // Allow seeking any file other than stdin/stdou/stderr
    for (const auto& syscall: {
            "lseek",
            "_llseek"
            }) {
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionErrno(ESPIPE),
                    filter::SyscallArg(0) <= 2));
        rules_.emplace_back(SeccompRule(
                    syscall,
                    action::ActionAllow(),
                    filter::SyscallArg(0) >= 3));
    }
}

void DefaultPolicy::addFileSystemAccessRules(bool readOnly) {
    // Allow any informations about file system
    allowSyscalls({
            "stat",
            "stat64",
            "fstat",
            "fstat64",
            "lstat",
            "lstat64",
            "listxattr",
            "llistxattr",
            "flistxattr",
            "readlink",
            "access",
            "getdents",
            });

    rules_.emplace_back(SeccompRule(
                "close",
                action::ActionAllow(),
                filter::SyscallArg(0) >= 3));

    if (readOnly) {
        rules_.emplace_back(SeccompRule(
                    "open",
                    action::ActionAllow(),
                    (filter::SyscallArg(1) & O_RDWR) == 0));

        for (const auto& syscall: {
                "unlink",
                "unlinkat",
                "symlink",
                "mkdir",
                "fsetxattr"
                }) {
            rules_.emplace_back(SeccompRule(
                        syscall,
                        action::ActionErrno(EPERM)));
        }
    }
    else {
        allowSyscalls({
                "open",
                "unlink",
                "unlinkat",
                "symlink",
                "mkdir"
                });
    }
}

void DefaultPolicy::allowSyscalls(
        std::initializer_list<std::string> syscalls) {
    for (auto syscall: syscalls) {
        rules_.emplace_back(SeccompRule(syscall, action::ActionAllow()));
    }
}

}
}
}