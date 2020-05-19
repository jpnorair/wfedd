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

#ifndef frontend_h
#define frontend_h

#include "mq.h"

// Libwebsockets
#include <libwebsockets.h>

// Standard C & POSIX Libraries
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>



/// one of these created for each message 
//typedef struct {
//    void *payload; // is malloc'd 
//    size_t len;
//} msg_t;


/// one of these is created for each client connecting to us
/// Basic idea: each session/client maps to a client socket for a corresponding daemon.
struct per_session_data {
    struct per_session_data* pss_list;
    struct lws*             wsi;
    void*                   conn_handle;        // connection handle (from backend data)
    
    //int                     last;               // the last message number we sent 
};



/// one of these is created for each vhost our protocol is used with
/// Basic idea: each vhost maps to a single daemon socket
struct per_vhost_data {
    struct lws_context*         context;
    struct lws_vhost*           vhost;
    const struct lws_protocols* protocol;
    struct per_session_data*    pss_list;   // linked-list of live pss
    
    //msg_t                       amsg;       // the one pending message...
    //int                         current;    // the current message number we are caching
};



int frontend_cli_callback(  struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len      );



/** @brief LWS service callback for http/https frontend
 *  @retval (int)
 *
 *  This function gets referenced as a callback during the frontend setup. 
 *  It will be called by libwebsockets (LWS).
 */
int frontend_http_callback( struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len      );


/** @brief LWS service callback for instantiated websockets
 *  @retval (int)
 *
 *  This function gets referenced as a callback during the frontend setup. 
 *  It will be called by libwebsockets (LWS).
 */
int frontend_ws_callback(   struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len      );


/** @brief Queues a message to a corresponding websocket, for output.
 *  @retval (int)
 *
 *  The backend will call this function to queue data that comes from a client
 *  client socket, onto its conjugate websocket.
 */
//int frontend_queuemsg(void* ws_handle, void* in, size_t len);


/** @brief Creates a message applicable to a mq, suitable for websocket
 *  @retval (mq_msg_t*)
 *
 *  The client is responsible for adding the msg to a mq, and for freeing.
 */
mq_msg_t* frontend_createmsg(void* in, size_t len);


/** @brief instructs the supplied websocket that there is a writable message
 *  @retval none
 *
 */
void frontend_pendmsg(void* ws_handle);



/** @brief Starts the frontend (libwebsockets)
 *  @retval (int)
 *
 *  This function gets called by backend_run() and may, otherwise, be ignored.
 */
void* frontend_start(void* backend_handle,
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


/** @brief Stops the frontend (libwebsockets)
 *  @retval (int)
 *
 *  This function gets called by backend_run() and may, otherwise, be ignored.
 */
int frontend_stop(void* handle);



#endif
