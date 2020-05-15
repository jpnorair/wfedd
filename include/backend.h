/*  Copyright 2020, JP Norair
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted provided that the following conditions are met:
  *
  * 1. Redistributions of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  *
  * 2. Redistributions in binary form must reproduce the above copyright 
  *    notice, this list of conditions and the following disclaimer in the 
  *    documentation and/or other materials provided with the distribution.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
  * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
  * POSSIBILITY OF SUCH DAMAGE.
  */

#ifndef backend_h
#define backend_h

#include "mq.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>



typedef struct {
    int     l_type;
    size_t  pagesize;
    char*   l_socket;
    char*   websocket;
} sockmap_t;

typedef struct {
    size_t size;
    sockmap_t* map;
} socklist_t;





int backend_run(socklist_t* socklist,
                int intsignal,
                int logs_mask,
                bool do_hostcheck,
                bool do_fastmonitoring,
                const char* hostname,
                int port_number,
                const char* certpath,
                const char* keypath,
                struct lws_protocols* protocols,
                struct lws_http_mount* mount
                );


int pollfd_open(void* backend_handle, struct pollfd* ws_pollfd);
int pollfd_close(void* backend_handle, struct pollfd* ws_pollfd);
int pollfd_update(void* backend_handle, struct pollfd* ws_pollfd);

void* conn_open(void* backend_handle, void* ws_handle, const char* ws_name);
void conn_close(void* backend_handle, void* conn_handle);


int conn_putmsg_outbound(void* conn_handle, void* data, size_t len);
mq_msg_t* conn_getmsg_outbound(void* conn_handle);
bool conn_hasmsg_outbound(void* conn_handle);


int conn_putmsg_inbound(void* conn_handle, void* data, size_t len);
void* conn_getmsg_inbound(void* conn_handle, size_t* len);
bool conn_hasmsg_inbound(void* conn_handle);




#endif
