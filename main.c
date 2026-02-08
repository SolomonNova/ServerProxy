/*
    File name: main.c
    Created at: 18-12-25
    Author: Solomon
*/

#include <stdio.h>      // provides printf()
#include <string.h>     // provides memset()
#include <arpa/inet.h>  // provides htonl() and htons()
#include <signal.h>     // providse signal(), SIGINT, SIGTERM, sig_atomic_t
#include "server.h"

volatile sig_atomic_t g_master_running = 1;

static void master_on_signal(int sig)
{
    (void)sig;
    g_master_running = 0;
}

int main(void)
{
    signal(SIGINT, master_on_signal);
    signal(SIGTERM, master_on_signal);

    printf("entered inside the server:\n");
    SERVER server = server_create(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP,
        INADDR_ANY,
        8080,
        128,
        4
    );

    memset(&server.m_si_address, 0, sizeof(server.m_si_address));
    server.m_si_address.sin_family = AF_INET;
    server.m_si_address.sin_addr.s_addr = htonl(server.m_iInterface);
    server.m_si_address.sin_port = htons(server.m_iPort);

    if (server_setup_listener(&server) < 0)
        return 1;

    if (server_spawn_workers(&server) < 0)
        return 1;

    server_master_loop(&server);

    server_shutdown(&server);
    return 0;
}
