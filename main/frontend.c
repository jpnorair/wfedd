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
#include "debug.h"

#include <libwebsockets.h>
#include <string.h>
#include <signal.h>


/// 



#if defined(LWS_HAS_RETRYPOLICY)
///@note this feature is somewhat rare among LWS library builds
static const lws_retry_bo_t retry = {
    .secs_since_valid_ping = 3,
    .secs_since_valid_hangup = 10,
};
#endif




int frontend_cli_callback(  struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len      ) {
/// Handles a raw client socket.  The purpose of this callback is to enqueue
/// received messages for forwarding onto the associated websocket.
/// The "user" pointer is the backend connection object (conn_t*).
    void* backend   = lws_context_user(lws_get_context(wsi));
    void* conn      = lws_get_opaque_user_data(wsi);
    int rc = 0;
  
    switch (reason) {        
        //RAW mode file was adopted (equivalent to 'wsi created')
        case LWS_CALLBACK_RAW_ADOPT_FILE:
            DEBUG_PRINTF("%s LWS_CALLBACK_RAW_ADOPT_FILE\n", __FUNCTION__);
            break;
    
        //This is the indication the RAW mode file has something to read. 
        //This doesn't actually do the read of the file and len is always 0... 
        //your code should do the read having been informed there is something to read now.
        case LWS_CALLBACK_RAW_RX_FILE: {
            DEBUG_PRINTF("%s LWS_CALLBACK_RAW_RX_FILE\n", __FUNCTION__);
            int size;
            void* data;
            size = conn_readraw_local(&data, backend, conn);
            DEBUG_PRINTF("reading msg frome otdb: %s\n", (char*)data);
            if (size > 0) {
                conn_putmsg_forweb(conn, data, (size_t)size);
                lws_callback_on_writable(lws_get_parent(wsi));
            }
        } break;
    
        //RAW mode file is writeable
        case LWS_CALLBACK_RAW_WRITEABLE_FILE:
            DEBUG_PRINTF("%s LWS_CALLBACK_RAW_WRITEABLE_FILE\n", __FUNCTION__);
            while (conn_hasmsg_forlocal(conn)) {
                mq_msg_t* msg;
                // Get the next message for this websocket.  Exit if no message.
                msg = conn_getmsg_forlocal(conn);
                if (msg == NULL)  {
                    break;
                }
                // Finally, write the message onto the raw socket and free it.
                DEBUG_PRINTF("writing msg to otdb: %s\n", (char*)msg->data);
                conn_writeraw_local(backend, conn, msg->data, msg->size);
                msg_free(msg);
            }
            break;
        
        // RAW mode wsi that adopted a file is closing
        case LWS_CALLBACK_RAW_CLOSE_FILE:
            DEBUG_PRINTF("%s LWS_CALLBACK_RAW_CLOSE_FILE\n", __FUNCTION__);
            conn_close(conn);   
            conn_del(backend, conn);
            break;
        
        default: 
            DEBUG_PRINTF("%s REASON=%i\n", __FUNCTION__, reason);
            break;
    }
    
    return rc;
}


int frontend_http_callback(  struct lws *wsi, 
                            enum lws_callback_reasons reason, 
                            void *user, 
                            void *in, 
                            size_t len) {
/// There could be an additional switch statement, here, that does additional
/// work to what's available in the dummy function.
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
    void* backend;
	int m;
    int rc = 0;   

    pss     = (struct per_session_data *)user;
    vhd     = (struct per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    backend = lws_context_user(lws_get_context(wsi)); // lws_context_user(vhd->context)

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

	case LWS_CALLBACK_ESTABLISHED: {
        lws_adoption_type type;
        lws_sock_file_fd_type desc;
        const char* pname;
        
        // Create a connection object to bridge the web and local worlds
        pss->conn_handle = conn_new(backend, vhd->protocol->name);
        if (pss->conn_handle == NULL) {
            ///@todo Some sort of error reporting
            rc = -1;
        }
        else {
            // add ourselves to the list of live pss held in the vhd 
            lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
            //pss->wsi = wsi;
        
            // open the connection -- must be accepted to be adopted
            conn_open(pss->conn_handle);
        
            // Adopt the connection to the lws service loop, and this vhost.
            desc.filefd = conn_get_descriptor(pss->conn_handle);
            type        = conn_get_adoptiontype(pss->conn_handle);
            pname       = conn_get_protocolname(pss->conn_handle);
            pss->lwsi   = lws_adopt_descriptor_vhost(vhd->vhost, type, desc, pname, wsi);
            
            // This will enable access of the conn handle from the child wsi
            lws_set_opaque_user_data(pss->lwsi, pss->conn_handle);
        }
    } break;

	case LWS_CALLBACK_CLOSED: {
        DEBUG_PRINTF("%s LWS_CALLBACK_CLOSED\n", __FUNCTION__);
        // Kill the corresponding daemon client socket
        ///@todo could/should the socket closing part be in the raw callback?
        //conn_close(pss->conn_handle);
        
        // remove our closing pss from the list of live pss 
		lws_ll_fwd_remove(struct per_session_data, pss_list, pss, vhd->pss_list);
  
        // Kill the websocket
    } break;

	case LWS_CALLBACK_SERVER_WRITEABLE: 
        DEBUG_PRINTF("%s LWS_CALLBACK_SERVER_WRITEABLE\n", __FUNCTION__);
        /// This is the routine that actually writes to the websocket.
        /// This loop inspects the msg queue of the daemon socket (ds) that is
        /// associated with this websocket.  It will consume messages from the
        /// daemon socket queue until it has no more or until the websocket is
        /// too busy to do so.
        while (conn_hasmsg_forweb(pss->conn_handle)) {
            mq_msg_t* msg;
            
            // Re-instate this callback on the next service loop in the event that 
            // data cannot be written to it right now.
            if (lws_partial_buffered(wsi) || lws_send_pipe_choked(wsi)) {
                lws_callback_on_writable(wsi);
                break;
            }
            
            // Get the next message for this websocket.  Exit if no message.
            msg = conn_getmsg_forweb(pss->conn_handle);
            if (msg == NULL)  {
                break;
            }
            
            // Finally, write the message onto the websocket.
            ///@note We allowed for LWS_PRE in the payload via creation of the data
            ///@todo have a specifier to select BINARY mode or TEXT
            DEBUG_PRINTF("writing msg to ws: %s\n", (char*)msg->data+LWS_PRE);
            m = lws_write(wsi, (uint8_t*)msg->data+LWS_PRE, msg->size, LWS_WRITE_TEXT);     //LWS_WRITE_BINARY
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
        DEBUG_PRINTF("%s LWS_CALLBACK_RECEIVE\n", __FUNCTION__);
        DEBUG_PRINTF("reading msg from ws: %s\n", (char*)in);
        conn_putmsg_forlocal(pss->conn_handle, in, len);
        lws_callback_on_writable(pss->lwsi);
		break;

	default:
        DEBUG_PRINTF("%s REASON=%i\n", __FUNCTION__, reason);
		break;
	}

	return rc;
}




mq_msg_t* frontend_createmsg(void* in, size_t len) {
    mq_msg_t* msg = NULL;
    
    if ((in != NULL) && (len != 0)) {
        msg = msg_new(len + LWS_PRE);
        if (msg != NULL) {
            memcpy((uint8_t*)msg->data+LWS_PRE, in, len);
        }
        else {
            ///@todo handle some error
        }
    }
    
    return msg;
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









