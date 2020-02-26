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

// Libwebsockets
#include <libwebsockets.h>

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>


/// one of these is created for each client connecting to us
struct per_session_data__minimal {
    struct per_session_data__minimal *pss_list;
    struct lws *wsi;
    int last;               // the last message number we sent 
};






int backend_callback(   struct lws *wsi, 
                        enum lws_callback_reasons reason, 
                        void *user, 
                        void *in, 
                        size_t len      );


void* backend_start(int logs_mask,
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

int backend_wait(void* handle, int intsignal);














#endif
