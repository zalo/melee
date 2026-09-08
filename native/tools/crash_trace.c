// Optional test-runner instrumentation, never included in the app bundle.
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

static void crash_stack(int signal_number)
{
    static const char message[] = "[native-crash] fatal signal; stack follows\n";
    void* frames[64];
    write(STDERR_FILENO, message, sizeof(message) - 1);
    backtrace_symbols_fd(frames, backtrace(frames, 64), STDERR_FILENO);
    // SA_RESETHAND restores the default action. Preserve the signal exit status.
    kill(getpid(), signal_number);
}

__attribute__((constructor)) static void install_crash_stack(void)
{
    struct sigaction action = {0};
    action.sa_handler = crash_stack;
    action.sa_flags = SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    sigaction(SIGBUS, &action, 0);
    sigaction(SIGSEGV, &action, 0);
    sigaction(SIGILL, &action, 0);
}
