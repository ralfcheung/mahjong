#include "game/Game.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <execinfo.h>

static void crashHandler(int sig) {
    const char* name = "UNKNOWN";
    if (sig == SIGSEGV) name = "SIGSEGV";
    else if (sig == SIGABRT) name = "SIGABRT";
    else if (sig == SIGBUS) name = "SIGBUS";
    else if (sig == SIGFPE) name = "SIGFPE";

    fprintf(stderr, "\n=== CRASH: signal %s (%d) ===\n", name, sig);

    void* frames[64];
    int count = backtrace(frames, 64);
    fprintf(stderr, "Backtrace (%d frames):\n", count);
    backtrace_symbols_fd(frames, count, 2); // write to stderr

    fprintf(stderr, "=== END BACKTRACE ===\n");
    _exit(1);
}

int main() {
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGBUS,  crashHandler);
    signal(SIGFPE,  crashHandler);
    Game game;

    if (!game.init()) {
        return 1;
    }

    game.run();
    game.shutdown();

    return 0;
}
