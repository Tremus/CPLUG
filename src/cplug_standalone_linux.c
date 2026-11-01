/* Released into the public domain by Tré Dudman - 2026
 * For licensing and more info see https://github.com/Tremus/CPLUG */

// This is a stub so my builds compile. Piece by piece, things will be unstubbed. Wish me luck
// TODO:
// - X11 GUI
// - Jack Audio
// - Jack MIDI

#include <cplug.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

static struct
{
    void*                 plugin;
    volatile sig_atomic_t quit;

    // An unused slot is -1. A timer's ID is its index
    int timerFds[CPLUG_MAX_TIMERS];
    struct
    {
        int      fd;
        uint32_t flags;
    } fds[CPLUG_MAX_FDS];
} g_app;

void Cplug_HostContext_SendParamEvent(CplugHostContext* ctx, const CplugEvent* e) {}
void Cplug_HostContext_Rescan(CplugHostContext* ctx, uint32_t flags) {}
bool Cplug_HostContext_GetHostName(CplugHostContext* ctx, char* buf, size_t buflen)
{
    snprintf(buf, buflen, "CPLUG Standalone Linux");
    return true;
}
bool Cplug_HostContext_RequestResize(CplugHostContext* ctx, uint32_t width, uint32_t height) { return false; }

bool Cplug_HostContext_RegisterTimer(CplugHostContext* ctx, uint32_t periodMs, uint32_t* timerId)
{
    CPLUG_LOG_ASSERT_RETURN(periodMs > 0, false);
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
    {
        if (g_app.timerFds[i] != -1)
            continue;

        int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        CPLUG_LOG_ASSERT_RETURN(fd != -1, false);

        struct itimerspec spec;
        spec.it_interval.tv_sec  = periodMs / 1000;
        spec.it_interval.tv_nsec = (long)(periodMs % 1000) * 1000000;
        spec.it_value            = spec.it_interval;
        if (timerfd_settime(fd, 0, &spec, NULL) != 0)
        {
            close(fd);
            return false;
        }

        g_app.timerFds[i] = fd;
        *timerId          = (uint32_t)i;
        return true;
    }
    CPLUG_LOG_ASSERT(false && "Out of timer slots. Increase CPLUG_MAX_TIMERS");
    return false;
}

void Cplug_HostContext_UnregisterTimer(CplugHostContext* ctx, uint32_t timerId)
{
    CPLUG_LOG_ASSERT_RETURN(timerId < CPLUG_ARRLEN(g_app.timerFds) && g_app.timerFds[timerId] != -1, );
    close(g_app.timerFds[timerId]);
    g_app.timerFds[timerId] = -1;
}

bool Cplug_HostContext_RegisterFd(CplugHostContext* ctx, int fd, uint32_t flags)
{
    CPLUG_LOG_ASSERT_RETURN(fd >= 0, false);
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
    {
        if (g_app.fds[i].fd != -1)
            continue;
        g_app.fds[i].fd    = fd;
        g_app.fds[i].flags = flags;
        return true;
    }
    CPLUG_LOG_ASSERT(false && "Out of fd slots. Increase CPLUG_MAX_FDS");
    return false;
}

void Cplug_HostContext_UnregisterFd(CplugHostContext* ctx, int fd)
{
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
    {
        if (g_app.fds[i].fd == fd)
        {
            g_app.fds[i].fd = -1;
            return;
        }
    }
    CPLUG_LOG_ASSERT(false && "fd was never registered");
}

static_assert(sizeof(CplugHostContext) == 72, "You may need to add support for new methods");

static void handle_signal(int sig) { g_app.quit = 1; }

static void run_loop()
{
    // Each pollfd is either a timer or a user fd. The low bit of 'kind' says which, the rest is the slot index
    struct pollfd pfds[CPLUG_ARRLEN(g_app.timerFds) + CPLUG_ARRLEN(g_app.fds)];
    int           kinds[CPLUG_ARRLEN(pfds)];

    while (!g_app.quit)
    {
        nfds_t n = 0;
        for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        {
            if (g_app.timerFds[i] == -1)
                continue;
            pfds[n]  = (struct pollfd){.fd = g_app.timerFds[i], .events = POLLIN};
            kinds[n] = i << 1;
            n++;
        }
        for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
        {
            if (g_app.fds[i].fd == -1)
                continue;
            short events = 0;
            if (g_app.fds[i].flags & CPLUG_FD_READ)
                events |= POLLIN;
            if (g_app.fds[i].flags & CPLUG_FD_WRITE)
                events |= POLLOUT;
            pfds[n]  = (struct pollfd){.fd = g_app.fds[i].fd, .events = events};
            kinds[n] = (i << 1) | 1;
            n++;
        }

        // SIGINT & SIGTERM interrupt this with EINTR
        int ret = poll(pfds, n, -1);
        if (ret < 0)
        {
            CPLUG_LOG_ASSERT(errno == EINTR);
            continue;
        }

        for (nfds_t i = 0; i < n && !g_app.quit; i++)
        {
            if (pfds[i].revents == 0)
                continue;
            int slot = kinds[i] >> 1;
            // Skip slots that an earlier callback in this loop unregistered
            if ((kinds[i] & 1) == 0)
            {
                if (g_app.timerFds[slot] != pfds[i].fd)
                    continue;
                // Missed ticks are dropped. One callback per wakeup
                uint64_t expirations;
                if (read(pfds[i].fd, &expirations, sizeof(expirations)) == sizeof(expirations))
                    cplug_onTimer(g_app.plugin, (uint32_t)slot);
            }
            else
            {
                if (g_app.fds[slot].fd != pfds[i].fd)
                    continue;
                uint32_t flags = 0;
                if (pfds[i].revents & POLLIN)
                    flags |= CPLUG_FD_READ;
                if (pfds[i].revents & POLLOUT)
                    flags |= CPLUG_FD_WRITE;
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                    flags |= CPLUG_FD_ERROR;
                cplug_onFd(g_app.plugin, pfds[i].fd, flags);
            }
        }
    }
}

int main(int argc, char** argv)
{
    CplugHostContext host = {
        .type            = CPLUG_PLUGIN_IS_STANDALONE,
        .sendParamEvent  = Cplug_HostContext_SendParamEvent,
        .rescan          = Cplug_HostContext_Rescan,
        .getHostName     = Cplug_HostContext_GetHostName,
        .requestResize   = Cplug_HostContext_RequestResize,
        .registerTimer   = Cplug_HostContext_RegisterTimer,
        .unregisterTimer = Cplug_HostContext_UnregisterTimer,
        .registerFd      = Cplug_HostContext_RegisterFd,
        .unregisterFd    = Cplug_HostContext_UnregisterFd,
    };

    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        g_app.timerFds[i] = -1;
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
        g_app.fds[i].fd = -1;

    // No SA_RESTART, so poll() returns and the loop sees 'quit'
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    cplug_libraryLoad();
    g_app.plugin = cplug_createPlugin(&host);
    if (g_app.plugin)
    {
        run_loop();
        cplug_destroyPlugin(g_app.plugin);
    }
    cplug_libraryUnload();

    // Catch anything the plugin forgot to unregister
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        if (g_app.timerFds[i] != -1)
            close(g_app.timerFds[i]);

    return g_app.plugin ? 0 : 1;
}

/*
@@@@@@@%%%%####++++*+++**+++++++==++++++++++*#%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#***+*+++++++++++**+*+++*+*#######%%
@@@@%#%%#####*+**++++++++===========+++++*#######%@@@@@@@@@@@@@@@@@@@@@@@@@%%%##**++++++++++++++**++++++++*#*+**####
@@#*%####***###%#****+++++++=======++++*+++*##%#%%@@@@@@@@@@@@@@@@%%@@@@@%%%##*++++++++=======+++++======+++**#+++**
@%%%%%@@%%%%@%%%%##%%%%##%%###*#*++++*****#*##%%%%@@@@@@@@@@@@@@@@%%@@@@%%###**+++++++==================+=++++++++++
@@@%%@%@@@@@@@@@@@@@@@@@@@%%%%%%%%%######*#####%%%@@@@@@@@@@@@@@@@%%%%%%%##**+++++=======++============++++++++***++
@@%%%%@@%@@@@@@@@@@%#++++++++++++++++===+*++####%%@%@@@@@@@@@@@@@@%%%#####*++===+++++********#*+++###+++++****##****
@%%%%%@@@%@%@%#**###%@@@@@%%%%#*++=====+++===+*##%%%%@@@@@@@@@@@@@%%%###*+++=++=++****++++==+++++=+++===++++++++**#*
@@%%#%%%%%%*+++#%@%#++=========+++*++====++++++=+%%%@@@@@@@@@@@@@@%###**++==+++++**+==+==+++++++++++======+++==+*#*#
%@@@@%%%%++++*##*++*#+++*##==++*+*++**+=+==+++++#++@%@@@@@@@@@@@@@%###*++++=+*++=++++==+**+==========+++++=====+++++
####%%##+*++++=+*%@@%**#++*=-+###@@%+++++++++++**%#=%@@@@@@@@@@@@@%##*++++=+++=*+===+++++==###-=+=-=======++=====+++
++#+%@@@#*##+#**#@@@@#*##**+*####@@@@*+==+****+*%%@@+%@@@@@@@@@@@@%##***+++==#+++++===#@%*++++-=++*==*++==========++
%%%**+++%%%%%%@@%@@@@@%#########@@@@%##%@@#*%@@@@%%@%+@+++#%%#++++++==+==+==#%##+===#%@@@#**+=++++***#%#++========++
@@@@@@+#@@@@@@@@@@@@@@@@@%%%%@@@@@@@@%%%#%%%@@@@@@@@@*=+@@@@@@@@@@%%#*++-==#%%##++*+#%%@@@###*++*#*#%@%#++++++=+++++
@@@@@@+%@@@@@@@@@@@@@@@@@@@@@@@%@@%%%%%%%@@@@@@@@@@@@%=+%%@@@@@@@@@%#*++==+##%%@%%##%#%#%%@%%#*+*#%%%##*##***+++++++
@@@@@@+@@@@@@@@@@@@@@@@@@@@@@%@@@@@%%%%%%%%@@@@@@@*=++=*%@@@@@@@@@@@%#=+==###%%%%%%%#%%%%%%%%%%%%%%%#%%%#########+**
@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@%%%%@%%@@@@@%@@@@@+=====+#@@@@@@@@@@@@%#++==+=+###%%%%%####%##########################
@@@@@@*%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@#+=====#@@@@@@@@@@@@@%##++=+==+*##%%%%%%%%######%%#####*#############
@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@==+==+@@@@@@@@@@@@@@@%#+======##%%%%%%%%%%%%###################%##%%
@@@@@@@#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@%%%*++@@@@@@@@@@@@@@@@%##+====*##%%%%@@@%@@@@@%%%%%%###%%#%%%%%%%%%%%
@@@@@@@@*@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@++@@@@@@@@@@@@@@@@@%##*===**#%%%%%%%%%@%%%@@@@@@@@@@%%%%%%%%%%%%@@
@@@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@+#@@@@@@@@@@@@@@@@@@%%###===%*%%%%%%%%%%%%%%%@@@@@%%%%@%%%%%%%@@@@@
@@@@@@@@@@#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%=%@@@@@@@@@@@@@@@@@@@%%####===#%%%%%%%%%%##%%%%%%%%%%%%%%%%%%%@%@%@@
@@@@@@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@+=@@@@@@@@@@@@@@@@@@@@@@%####*+#=%%#%%%@@%%%%%%%%%%%%%%%%%%%%%@%@@@@%+
@@@@@@@@@@@@@@=%@@@@@@@@@@@@@@@@@@@@@@@@@@@@+=#@@@@@@@@@@@@@@@@@@@@@@@%######+*==#%%@%@@@@%%%%%%%%%%%%%%%%%%@%@@@#+%
@@@@@@@@@@@@@@@@*+%@@@@@@@@@@@@@@@@@@@@@@%+#%%@@@@@@@@@@@@@@@@@@@@@@@@%%%####**##==%%@@%@@%%%%%%%%%%%%@@%%@@@@%#+#%%
@@@@@@@@@@@@@@@@@@@@+=*@@@@@@@@@@@@@@#==%%%#@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####*+*#%+-%%%@@@@@@@@@@%%@%%%%%@%#+=%%#%%
@@@@@@@@@@@@@@@@@@@@@@@@@%##****###%@@%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##########%%#*==+#%@%@@@@@@@@%#+==*%%%####%
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####**#####%%%%%%#++=======+#%%%%%%#######
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%######*####%%%%%%%%%%%%%%%%%%%%%%%%#%####%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#%####*####%%%%%%%%%%%%%%%%%%%%%%%%%%#%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%##%%%%#***####%%%%%%%%%%%%%%%%%%%%%%%%%###
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%####%%%%%#++#####%#%%%%%%%%%%%%%%%%%%%%%%%##
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%######%%%%%#*=+*#####%%%%%%%%%%%@@@%%@%%%%%%##
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#####%%@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##**++**#######=+++*#####%%%%%%%%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%####%%@@@@@@@@@@@@@@@@%%%%%@%@@@@%%##*+++++===+++*##*+++++**###%%%%%%%%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@%%%%#%%%@@@@@@@@@@%%#**+===---==+#%%###*++++==----====++****++++*#######%%%%%%%%%%%%%%%%%%%
@@@@@@@@@@@@@@@@@@@@@@@@%@@%@@@@@@@@@@@@@@@%#*===+=====++++#**++=======-========++**#*############%#%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###*#**#%%%%####*+==+============++++*##################%%%%%%%%%%%####
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#%######%%%%#*++=+==+========++**#######%%%#########%#%%%%%#########
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%#**#%%#*++++++====+++++*+***########%%%###%###%%%#############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@%%%%#####*++++++===+=+++++++***###########%%%####%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%#*++++++++++++**+***+***##################################
@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@#***+*++***+++***#**####%##%##############################
@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%###*+**##############%##*##*#%#**#*#*#####################
@@@@@@@@@@@@@@@@@@@@@@@@%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####*#############%###**##*###******+****#################
@@@@@@@@@@@@@@@@@@@@@%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###%%%#########*#####*#*#####****++*++**+*################
@@@@@@@@@@@@@@@@@@@@@%%%##%#%%@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%########**#*****++++++++++++***################
@@@@@@@@@@@@@@@@@@@%%%%#*+=++*#@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@%%%%%%%%%%####****++++++++++****#%%%#############
@@@@@@@@@@@@@@@@@@@@@@@@@%%%@#+==+*#%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#####%%######**++=====++++**##############*###
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###**+========++++++++*##%@@%%%##*++++====+=++==++++=======++++**###%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%@@@%%#*++============++++=============------========++**######%%###############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%@@@@@%%%####+++====------========---=====+++++*#######%%%%%%%%###########*#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@%%%%####%#############+**#*++***########%%%%%%%%#############*
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@%@@@@%%%%%######**#####**#*########%%%%%%%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%%%%%%#####**+*****###***############%%%###############**
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%##%%%%%%%#######*****+**+++*+**###############################**
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%@@%#**#***#*++*****+++++++++++++++**####################*#######****+
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##****+++***+++++++++*+++++****++****################*#*****####*#**+#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%####****++++++++++++++++*******###################*#********++***#
*/