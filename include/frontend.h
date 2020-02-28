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

#ifndef frontend_h
#define frontend_h

// Libwebsockets
#include <libwebsockets.h>

// Standard C & POSIX Libraries
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>



/// one of these created for each message 
typedef struct {
    void *payload; // is malloc'd 
    size_t len;
} msg_t;


/// one of these is created for each client connecting to us
struct per_session_data {
    struct per_session_data *pss_list;
    struct lws *wsi;
    int last;               // the last message number we sent 
};


/// one of these is created for each vhost our protocol is used with
struct per_vhost_data {
    struct lws_context *context;
    struct lws_vhost *vhost;
    const struct lws_protocols *protocol;

    // linked-list of live pss
    struct per_session_data *pss_list;
    // the one pending message...
    msg_t amsg;
    // the current message number we are caching
    int current;
    
    //handle to local socket connection (see backend.h / backend.c)
    void* socket_conn;
};



int frontend_callback(   struct lws *wsi, 
                        enum lws_callback_reasons reason, 
                        void *user, 
                        void *in, 
                        size_t len      );


void* frontend_start(int logs_mask,
                    bool do_hostcheck,
                    bool do_fastmonitoring,
                    const char* hostname,
                    int port_number,
                    const char* certpath,
                    const char* keypath,
                    const char* start_msg,
                    struct lws_protocols* protocols,
                    struct lws_http_mount* mount
                    );

int frontend_wait(void* handle, int intsignal);














#endif
