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
  

#include "frontend.h"
#include "backend.h"

#include <libwebsockets.h>
#include <string.h>
#include <signal.h>


#if defined(LWS_HAS_RETRYPOLICY)
///@note this feature is somewhat rare among LWS library builds
static const lws_retry_bo_t retry = {
    .secs_since_valid_ping = 3,
    .secs_since_valid_hangup = 10,
};
#endif




int frontend_http_callback(  struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len) {
/// Override the lws_callback_http_dummy() callback (default callback) for the
/// subset of external polling handlers we need to handle uniquely.
/// @note The "void* in" parameter will contain a struct pollfd* datatype.
///       The "void* user" parameter will store the backend handle.
    
printf("%s %i\n", __FUNCTION__, __LINE__);
printf("reason = %i\n", reason);
    
    /// @todo look into returning the value from conn_ws_...() functions
    /// rather than just 0.
    switch (reason) {
        case LWS_CALLBACK_ADD_POLL_FD: 
printf("%s %i\n", __FUNCTION__, __LINE__);
            pollfd_open(user, (struct pollfd*)in);
            return 0;
        case LWS_CALLBACK_DEL_POLL_FD: 
printf("%s %i\n", __FUNCTION__, __LINE__);
            pollfd_close(user, (struct pollfd*)in);
            return 0;
        case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
printf("%s %i\n", __FUNCTION__, __LINE__);
            pollfd_update(user, (struct pollfd*)in);
            return 0;

        default:
            break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}




/// This does most of the work in handling the websockets
int frontend_ws_callback(   struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len      ) {
            
	struct per_session_data *pss;
	struct per_vhost_data *vhd;
	int m;
    int rc = 0;   

printf("%s %i\n", __FUNCTION__, __LINE__);

    ///@todo make sure this wasn't as "Static" from demo app
    pss = (struct per_session_data *)user;
    vhd = (struct per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

    ///@todo this operational switch needs to be mutexed

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd             = lws_protocol_vh_priv_zalloc(
                                lws_get_vhost(wsi), 
                                lws_get_protocol(wsi), 
                                sizeof(struct per_vhost_data));
		vhd->context    = lws_get_context(wsi);
		vhd->protocol   = lws_get_protocol(wsi);
		vhd->vhost      = lws_get_vhost(wsi);
		break;

	case LWS_CALLBACK_ESTABLISHED:
		/// add ourselves to the list of live pss held in the vhd 
		lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
		pss->wsi = wsi;
        
        // Open a corresponding daemon client socket
        pss->conn_handle = conn_open(lws_context_user(vhd->context), wsi, vhd->protocol->name);
        
        ///@todo some form of error handling if conn_handle returns as NULL
        //if (pss->conn_handle == NULL) {
        //}
		break;

	case LWS_CALLBACK_CLOSED:
		/// remove our closing pss from the list of live pss 
        // Kill the corresponding daemon client socket
        conn_close(lws_context_user(vhd->context), pss->conn_handle);
        // Kill the websocket
		lws_ll_fwd_remove(struct per_session_data, pss_list, pss, vhd->pss_list);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE: 
        /// This is the routine that actually writes to the websocket.
        /// This loop inspects the msg queue of the daemon socket (ds) that is
        /// associated with this websocket.  It will consume messages from the
        /// daemon socket queue until it has no more or until the websocket is
        /// too busy to do so.
        while (conn_hasmsg_outbound(pss->conn_handle)) {
            mq_msg_t* msg;
            
            // Re-instate this callback on the next service loop in the event that 
            // data cannot be written to it right now.
            if (lws_partial_buffered(wsi) || lws_send_pipe_choked(wsi)) {
                lws_callback_on_writable(wsi);
                break;
            }
            
            // Get the next message for this websocket.  Exit if no message.
            msg = conn_getmsg_outbound(pss->conn_handle);
            if (msg == NULL)  {
                break;
            }
            
            // Finally, write the message onto the websocket.
            ///@note We allowed for LWS_PRE in the payload via 
            m = lws_write(wsi, msg->data + LWS_PRE, msg->size, LWS_WRITE_TEXT);
            if (m < msg->size) {
                lwsl_err("ERROR %d writing to ws\n", m);
                rc = -1;
            }
            
            msg_free(msg);
        }
        break;

    /// Put the message received from the the websocket onto its queue.
    /// This message will be written to corresponding daemon socket (ds).
	case LWS_CALLBACK_RECEIVE:
        conn_putmsg_inbound(pss->conn_handle, in, len);
		break;

	default:
		break;
	}

	return rc;
}




mq_msg_t* frontend_createmsg(void* in, size_t len) {
    mq_msg_t* msg = NULL;
    
    if ((in != NULL) && (len != 0)) {
        msg = msg_new(len + LWS_PRE);
        if (msg != NULL) {
            memcpy((char*)msg->data + LWS_PRE, in, len);
        }
        else {
            ///@todo handle some error
        }
    }
    
    return msg;
}

void frontend_pendmsg(void* ws_handle) {
    struct lws* wsi = ws_handle;
    
    if (wsi != NULL) {
        lws_callback_on_writable(wsi);
    }
}




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
                ) {

    struct lws_context_creation_info info;
    struct lws_context *context;
    
    lws_set_log_level(logs_mask, NULL);
    //lwsl_user("LWS start msg");

    /// These info parameters [mostly] come from command line arguments
    bzero(&info, sizeof(struct lws_context_creation_info));
    info.user       = backend_handle;
    info.port       = port_number;
    info.mounts     = mount;
    info.protocols  = protocols;
    info.vhost_name = hostname;
    info.options    = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    info.ws_ping_pong_interval = 10;
    if ((certpath != NULL) && (keypath != NULL)) {
        info.options                   |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        info.ssl_cert_filepath          = certpath;
        info.ssl_private_key_filepath   = keypath;
    }
    if (do_hostcheck) {
        info.options |= LWS_SERVER_OPTION_VHOST_UPG_STRICT_HOST_CHECK;
    }
#   if defined(LWS_HAS_RETRYPOLICY)
    ///@note this feature is not in all builds of libwebsockets
    if (do_fastmonitoring) {
        info.retry_and_idle_policy = &retry;
    }
#   endif

    /// Initialization.
    context = lws_create_context(&info);
    if (context == NULL) {
        lwsl_err("lws init failed\n");
    }
    
    /// Run lws_service() in order to do a first pass on the HTTP Protocol-0.
    /// Otherwise, backend won't have any fds to poll.
    //lws_service(context, 0);
    
    return context;
}


int frontend_stop(void* handle) {
    struct lws_context *context;
    
    if (handle == NULL) {
        return -1;
    }
    context = handle;
    
    lws_context_destroy(context);
    return 0;
}




// ---- Remove below ----

volatile int interrupted;
void frontend_inthandler(int sig) {
    interrupted = 1;
}

int frontend_wait(void* handle, int intsignal) {
    struct lws_context *context = handle;
    int n = 0;
    
    signal(intsignal, frontend_inthandler);
    
    /// Runtime Loop: waits for interrupt signal
    while (n >= 0 && !interrupted) {
        n = lws_service(context, 0);
    }
    
    /// Deinitialize
    
    
    return 0;
}









