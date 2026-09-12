// Crash tracer with fault address and argument registers (AArch64 Linux).
// Build: $CC -shared -fPIC -g crash_trace_regs.c -o flip-crash-trace.so; run with LD_PRELOAD.
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void crash_stack(int signal_number, siginfo_t* info, void* context)
{
    char line[256];
    void* frames[64];
    ucontext_t* uc = context;
    int n = snprintf(line, sizeof line, "[native-crash] signal %d fault-address %p\n", signal_number, info->si_addr);
    write(STDERR_FILENO, line, n);
#ifdef __aarch64__
    n = snprintf(line, sizeof line, "[native-crash] pc=%#llx lr=%#llx sp=%#llx x0=%#llx x1=%#llx x2=%#llx x19=%#llx x21=%#llx\n",
                 (unsigned long long) uc->uc_mcontext.pc, (unsigned long long) uc->uc_mcontext.regs[30],
                 (unsigned long long) uc->uc_mcontext.sp, (unsigned long long) uc->uc_mcontext.regs[0],
                 (unsigned long long) uc->uc_mcontext.regs[1], (unsigned long long) uc->uc_mcontext.regs[2],
                 (unsigned long long) uc->uc_mcontext.regs[19], (unsigned long long) uc->uc_mcontext.regs[21]);
    write(STDERR_FILENO, line, n);
#endif
    backtrace_symbols_fd(frames, backtrace(frames, 64), STDERR_FILENO);
    kill(getpid(), signal_number);
}

__attribute__((constructor)) static void install_crash_stack(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = crash_stack;
    action.sa_flags = SA_RESETHAND | SA_SIGINFO;
    sigaction(SIGBUS, &action, 0);
    sigaction(SIGSEGV, &action, 0);
    sigaction(SIGILL, &action, 0);
}
