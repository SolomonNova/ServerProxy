/*
    File name: main.c
    Created at: 18-12-25
    Author: Solomon
*/

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "server.h"

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
int main(void)
{
    printf("entered inside the server:\n");
    SERVER server = server_create(
        AF_INET,        // domain
        SOCK_STREAM,    // service
        IPPROTO_TCP,    // protocol
        INADDR_ANY,     // injerface
        8080,           // port
        128,            // backlog
        4               // worker count
    );

    /* setup socket address */
    memset(&server.m_si_address, 0, sizeof(server.m_si_address));
    server.m_si_address.sin_family      = AF_INET;
    server.m_si_address.sin_addr.s_addr = htonl(server.m_iInterface);
    server.m_si_address.sin_port        = htons(server.m_iPort);

    /* setup listening socket */
    if (server_setup_listener(&server) < 0)
    {
        perror("server_setup_listener failed");
        return 1;
    }

    /* spawn worker processes */
    if (server_spawn_workers(&server) < 0)
    {
        perror("server_spawn_workers failed");
        return 1;
    }

    /* master supervision loop */
    server_master_loop(&server);
    
    printf("running safely till before the shutdown\n");
    /* graceful shutdown */
    server_shutdown(&server);

    return 0;
}

