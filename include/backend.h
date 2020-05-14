/* Copyright 2014, JP Norair
  *
  * Licensed under the OpenTag License, Version 1.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  * http://www.indigresso.com/wiki/doku.php?id=opentag:license_1_0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
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
