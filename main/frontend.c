/* Copyright 2020, JP Norair
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
 * ----------------------------------------------------------------------------
 * This is a derivative work by JP Norair based on example software from the
 * libwebsockets project.  The libwebsockets example code is released in the
 * public domain, with all rights waived, as descibed below.
 * 
 * ws protocol handler plugin for "lws-minimal"
 *
 * Written in 2010-2019 by Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 * ----------------------------------------------------------------------------
 *
 * This version holds a single message at a time, which may be lost if a new
 * message comes.  See the minimal-ws-server-ring sample for the same thing
 * but using an lws_ring ringbuffer to hold up to 8 messages at a time.
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


/// destroys the message when everyone has had a copy of it
static void sub_destroy_message(void *_msg) {
	msg_t *msg = _msg;

	free(msg->payload);
	msg->payload = NULL;
	msg->len = 0;
}





/// This does most of the work in handling the websockets
int frontend_callback(   struct lws *wsi, 
                        enum lws_callback_reasons reason, 
                        void *user, 
                        void *in, 
                        size_t len      ) {
            
	struct per_session_data *pss;
	struct per_vhost_data *vhd;
	int m;
    int rc = 0;   

    ///@todo make sure this wasn't as "Static" from demo app
    pss = (struct per_session_data *)user;
    vhd = (struct per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

    ///@todo this operational switch needs to be mutexed

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct per_vhost_data));
		vhd->context    = lws_get_context(wsi);
		vhd->protocol   = lws_get_protocol(wsi);
		vhd->vhost      = lws_get_vhost(wsi);
		break;

	case LWS_CALLBACK_ESTABLISHED:
		/// add ourselves to the list of live pss held in the vhd 
		lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
		pss->wsi            = wsi;
		pss->last           = vhd->current;
        
        // Open a corresponding daemon client socket
        pss->conn_handle    = backend_conn_open(lws_context_user(vhd->context), wsi, vhd->protocol->name);
        
        ///@todo some form of error handling if conn_handle returns as NULL
        //if (pss->conn_handle == NULL) {
        //}
		break;

	case LWS_CALLBACK_CLOSED:
		/// remove our closing pss from the list of live pss 
        // Kill the corresponding daemon client socket
        backend_conn_close(lws_context_user(vhd->context), pss->conn_handle);
        // Kill the websocket
		lws_ll_fwd_remove(struct per_session_data, pss_list, pss, vhd->pss_list);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
    /// This is the routine that actually writes to the websocket
    ///@todo might toss-in a ring buffer here, check the lws ring buffer example.
		if (!vhd->amsg.payload) {
			break;
        }
		if (pss->last == vhd->current) {
			break;
        }
        
		/// notice we allowed for LWS_PRE in the payload already 
		m = lws_write(wsi, ((unsigned char *)vhd->amsg.payload) + LWS_PRE, vhd->amsg.len, LWS_WRITE_TEXT);
		if (m < (int)vhd->amsg.len) {
			lwsl_err("ERROR %d writing to ws\n", m);
			rc = -1;
		}
        else {
            pss->last = vhd->current;
        }
		break;

    ///@todo this is where data from the websocket gets forwarded to socket
    ///      The writeback part should be moved into the polling thread.
	case LWS_CALLBACK_RECEIVE:
        backend_queuemsg(lws_context_user(vhd->context), pss->conn_handle, in, len);
		break;

	default:
		break;
	}

	return rc;
}



int frontend_queuemsg(void* ws_handle, void* in, size_t len) {
    struct lws* wsi = ws_handle;
    struct per_vhost_data* vhd;

    vhd = (struct per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    
    /// amsg should be empty.  If it's not, we're looking at the previous message
    if (vhd->amsg.payload) {
        sub_destroy_message(&vhd->amsg);
    }
    vhd->amsg.len = len;
    
    /// notice we over-allocate by LWS_PRE 
    vhd->amsg.payload = malloc(LWS_PRE + len);
    if (!vhd->amsg.payload) {
        lwsl_user("Out of Memory: dropping\n");
        len = 0;
    }
    else {
        memcpy((char *)vhd->amsg.payload + LWS_PRE, in, len);
        vhd->current++;
        lws_callback_on_writable(wsi);
    }

    return (int)len;
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
    memset(&info, 0, sizeof info);
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
    lws_context_destroy(context);
    
    return 0;
}







