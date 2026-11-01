/* Released into the public domain by Tré Dudman - 2026
 * For licensing and more info see https://github.com/Tremus/CPLUG */

// This is a stub so my builds compile. Piece by piece, things will be unstubbed. Wish me luck
// TODO:
// - Jack Audio
// - Jack MIDI

#include <cplug.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <xcb/xcb.h>

static struct
{
    void*                 plugin;
    void*                 gui;
    volatile sig_atomic_t quit;

    // The top-level window. The GUI is a child window inside it, on its own connection, like in a DAW
    xcb_connection_t* connection;
    xcb_window_t      window;
    xcb_atom_t        wmProtocols;
    xcb_atom_t        wmDeleteWindow;

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

static xcb_atom_t intern_atom(const char* name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(g_app.connection, 0, strlen(name), name);
    xcb_intern_atom_reply_t* reply  = xcb_intern_atom_reply(g_app.connection, cookie, NULL);
    xcb_atom_t               atom   = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static void set_property(xcb_atom_t property, xcb_atom_t type, uint8_t format, uint32_t len, const void* data)
{
    xcb_change_property(g_app.connection, XCB_PROP_MODE_REPLACE, g_app.window, property, type, format, len, data);
}

static void create_window(uint32_t width, uint32_t height)
{
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(g_app.connection)).data;

    g_app.window = xcb_generate_id(g_app.connection);
    xcb_create_window(
        g_app.connection,
        XCB_COPY_FROM_PARENT,
        g_app.window,
        screen->root,
        0,
        0,
        width,
        height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        0,
        NULL);

    static const char name[] = CPLUG_PLUGIN_NAME;
    set_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(name) - 1, name);
    set_property(intern_atom("_NET_WM_NAME"), intern_atom("UTF8_STRING"), 8, sizeof(name) - 1, name);
    // Instance and class names, each null terminated
    static const char wmClass[] = CPLUG_PLUGIN_NAME "\0" CPLUG_PLUGIN_NAME;
    set_property(XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);

    // _NET_WM_PID is only meaningful together with WM_CLIENT_MACHINE
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    set_property(XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING, 8, strlen(hostname), hostname);
    uint32_t pid = (uint32_t)getpid();
    set_property(intern_atom("_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1, &pid);

    // Close button
    g_app.wmProtocols    = intern_atom("WM_PROTOCOLS");
    g_app.wmDeleteWindow = intern_atom("WM_DELETE_WINDOW");
    set_property(g_app.wmProtocols, XCB_ATOM_ATOM, 32, 1, &g_app.wmDeleteWindow);

    // WM_SIZE_HINTS from ICCCM 4.1.2.3. Fixed size until the GUI supports cplug_setSize
    enum
    {
        P_MIN_SIZE = 1 << 4,
        P_MAX_SIZE = 1 << 5,
    };
    uint32_t sizeHints[18] = {0};
    sizeHints[0]           = P_MIN_SIZE | P_MAX_SIZE;
    sizeHints[5]           = width;
    sizeHints[6]           = height;
    sizeHints[7]           = width;
    sizeHints[8]           = height;
    set_property(XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, CPLUG_ARRLEN(sizeHints), sizeHints);
}

static void close_gui()
{
    if (g_app.gui)
    {
        cplug_setVisible(g_app.gui, false);
        cplug_setParent(g_app.gui, NULL);
        cplug_destroyGUI(g_app.gui);
        g_app.gui = NULL;
    }
}

static void handle_window_events()
{
    xcb_generic_event_t* event;
    while ((event = xcb_poll_for_event(g_app.connection)))
    {
        if ((event->response_type & ~0x80) == XCB_CLIENT_MESSAGE)
        {
            xcb_client_message_event_t* msg = (xcb_client_message_event_t*)event;
            if (msg->type == g_app.wmProtocols && msg->data.data32[0] == g_app.wmDeleteWindow)
            {
                close_gui();
                g_app.quit = 1;
            }
        }
        free(event);
    }
    if (xcb_connection_has_error(g_app.connection))
    {
        fprintf(stderr, "Lost the connection to the X server\n");
        g_app.quit = 1;
    }
}

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
                if (pfds[i].fd == xcb_get_file_descriptor(g_app.connection))
                    handle_window_events();
                else
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

    // xcb_connect reads DISPLAY too, but this gives a clearer message for the common case
    if (getenv("DISPLAY") == NULL)
    {
        fprintf(stderr, "DISPLAY is not set. " CPLUG_PLUGIN_NAME " needs an X11 server or XWayland\n");
        return 1;
    }
    g_app.connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(g_app.connection))
    {
        fprintf(stderr, "Failed connecting to the X server on DISPLAY=%s\n", getenv("DISPLAY"));
        xcb_disconnect(g_app.connection);
        return 1;
    }

    cplug_libraryLoad();
    g_app.plugin = cplug_createPlugin(&host);
    if (g_app.plugin)
    {
        // Without a GUI the window stays empty, and closing it still quits
        uint32_t width = 640, height = 360;
        g_app.gui      = cplug_createGUI(&host, g_app.plugin);
        if (g_app.gui)
            cplug_getSize(g_app.gui, &width, &height);

        create_window(width, height);
        if (g_app.gui)
        {
            cplug_setParent(g_app.gui, (void*)(uintptr_t)g_app.window);
            cplug_setVisible(g_app.gui, true);
        }
        xcb_map_window(g_app.connection, g_app.window);
        xcb_flush(g_app.connection);

        int windowFd = xcb_get_file_descriptor(g_app.connection);
        Cplug_HostContext_RegisterFd(&host, windowFd, CPLUG_FD_READ);
        // Atom replies may have queued events that poll() won't see
        handle_window_events();
        run_loop();
        Cplug_HostContext_UnregisterFd(&host, windowFd);

        close_gui();
        xcb_destroy_window(g_app.connection, g_app.window);
        cplug_destroyPlugin(g_app.plugin);
    }
    cplug_libraryUnload();
    xcb_disconnect(g_app.connection);

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