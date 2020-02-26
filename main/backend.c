/*
 * ws protocol handler plugin for "lws-minimal"
 *
 * Written in 2010-2019 by Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 *
 * This version holds a single message at a time, which may be lost if a new
 * message comes.  See the minimal-ws-server-ring sample for the same thing
 * but using an lws_ring ringbuffer to hold up to 8 messages at a time.
 */

///@note this .c file was used as an include, now is compiled
#define LWS_PLUGIN_STATIC

#include "backend.h"

#include <libwebsockets.h>
#include <string.h>
#include <signal.h>


/// one of these created for each message 
typedef struct {
	void *payload; // is malloc'd 
	size_t len;
} msg_t;


/// one of these is created for each vhost our protocol is used with
struct per_vhost_data__minimal {
	struct lws_context *context;
	struct lws_vhost *vhost;
	const struct lws_protocols *protocol;

    // linked-list of live pss
	struct per_session_data__minimal *pss_list;
    // the one pending message...
	msg_t amsg;
    // the current message number we are caching
	int current;
};


/// destroys the message when everyone has had a copy of it
static void __minimal_destroy_message(void *_msg) {
	msg_t *msg = _msg;

	free(msg->payload);
	msg->payload = NULL;
	msg->len = 0;
}



#if !defined (LWS_PLUGIN_STATIC)
/// boilerplate needed if we are built as a dynamic plugin

#   define LWS_PLUGIN_PROTOCOL_MINIMAL { \
        "lws-minimal", \
        callback_minimal, \
        sizeof(struct per_session_data__minimal), \
        128, \
        0, NULL, 0 \
    }

static const struct lws_protocols protocols[] = {
    LWS_PLUGIN_PROTOCOL_MINIMAL
};

int init_protocol_minimal(struct lws_context *context, struct lws_plugin_capability *c) {
    if (c->api_magic != LWS_PLUGIN_API_MAGIC) {
        lwsl_err("Plugin API %d, library API %d", LWS_PLUGIN_API_MAGIC, c->api_magic);
        return 1;
    }

    c->protocols        = protocols;
    c->count_protocols  = LWS_ARRAY_SIZE(protocols);
    c->extensions       = NULL;
    c->count_extensions = 0;
    return 0;
}

int destroy_protocol_minimal(struct lws_context *context) {
    return 0;
}
#endif

#if defined(LWS_HAS_RETRYPOLICY)
///@note this feature is somewhat rare among LWS library builds
static const lws_retry_bo_t retry = {
    .secs_since_valid_ping = 3,
    .secs_since_valid_hangup = 10,
};
#endif




/// This does most of the work in handling the websockets
int backend_callback(   struct lws *wsi, 
                        enum lws_callback_reasons reason, 
                        void *user, 
                        void *in, 
                        size_t len      ) {
            
	struct per_session_data__minimal *pss;
	struct per_vhost_data__minimal *vhd;
	int m;
    int rc = 0;   

    ///@todo make sure this wasn't as "Static" from demo app
    pss = (struct per_session_data__minimal *)user;
    vhd = (struct per_vhost_data__minimal *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));

    ///@todo this operational switch needs to be mutexed

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct per_vhost_data__minimal));
		vhd->context    = lws_get_context(wsi);
		vhd->protocol   = lws_get_protocol(wsi);
		vhd->vhost      = lws_get_vhost(wsi);
		break;

	case LWS_CALLBACK_ESTABLISHED:
		/// add ourselves to the list of live pss held in the vhd 
		lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
		pss->wsi    = wsi;
		pss->last   = vhd->current;
		break;

	case LWS_CALLBACK_CLOSED:
		/// remove our closing pss from the list of live pss 
		lws_ll_fwd_remove(struct per_session_data__minimal, pss_list, pss, vhd->pss_list);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
    /// This is the routine that actually writes to the websocket(s)
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
        // amsg should be empty.  If it's not, we're looking at the last message
		if (vhd->amsg.payload) {
			__minimal_destroy_message(&vhd->amsg);
        }
		vhd->amsg.len = len;
        
		/// notice we over-allocate by LWS_PRE 
		vhd->amsg.payload = malloc(LWS_PRE + len);
		if (!vhd->amsg.payload) {
			lwsl_user("Out of Memory: dropping\n");
			break;
		}

		memcpy((char *)vhd->amsg.payload + LWS_PRE, in, len);
		vhd->current++;

		/// let everybody know we want to write something on them
        /// as soon as they are ready
		lws_start_foreach_llp(struct per_session_data__minimal **, ppss, vhd->pss_list) {
			lws_callback_on_writable((*ppss)->wsi);
		} lws_end_foreach_llp(ppss, pss_list);
		break;

	default:
		break;
	}

	return rc;
}



struct raw_vhd {
//    lws_sock_file_fd_type u;
    int filefd;
};

static char filepath[256];

static int callback_raw_test(   struct lws *wsi, enum lws_callback_reasons reason,
                                void *user, void *in, size_t len)
{
    struct raw_vhd *vhd = (struct raw_vhd *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    
    lws_sock_file_fd_type u;
    uint8_t buf[1024];
    int n;

    switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
        vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct raw_vhd));
                
        vhd->filefd = lws_open(filepath, O_RDWR);
        if (vhd->filefd == -1) {
            lwsl_err("Unable to open %s\n", filepath);

            return 1;
        }
        u.filefd = (lws_filefd_type)(long long)vhd->filefd;
        if (!lws_adopt_descriptor_vhost(lws_get_vhost(wsi),
                        LWS_ADOPT_RAW_FILE_DESC, u,
                        "raw-test", NULL)) {
            lwsl_err("Failed to adopt fifo descriptor\n");
            close(vhd->filefd);
            vhd->filefd = -1;

            return 1;
        }
        break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:
        if (vhd && vhd->filefd != -1)
            close(vhd->filefd);
        break;

    /* callbacks related to raw file descriptor */

    case LWS_CALLBACK_RAW_ADOPT_FILE:
        lwsl_notice("LWS_CALLBACK_RAW_ADOPT_FILE\n");
        break;

    case LWS_CALLBACK_RAW_RX_FILE:
        lwsl_notice("LWS_CALLBACK_RAW_RX_FILE\n");
        n = read(vhd->filefd, buf, sizeof(buf));
        if (n < 0) {
            lwsl_err("Reading from %s failed\n", filepath);

            return 1;
        }
        lwsl_hexdump_level(LLL_NOTICE, buf, n);
        break;

    case LWS_CALLBACK_RAW_CLOSE_FILE:
        lwsl_notice("LWS_CALLBACK_RAW_CLOSE_FILE\n");
        break;

    case LWS_CALLBACK_RAW_WRITEABLE_FILE:
        lwsl_notice("LWS_CALLBACK_RAW_WRITEABLE_FILE\n");
        /*
         * you can call lws_callback_on_writable() on a raw file wsi as
         * usual, and then write directly into the raw filefd here.
         */
        break;

    default:
        break;
    }

    return 0;
}




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
                ) {

    struct lws_context_creation_info info;
    struct lws_context *context;
    
    lws_set_log_level(logs_mask, NULL);
    lwsl_user(start_msg);

    /// These info parameters [mostly] come from command line arguments
    memset(&info, 0, sizeof info);
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
void backend_inthandler(int sig) {
    interrupted = 1;
}

int backend_wait(void* handle, int intsignal) {
    struct lws_context *context = handle;
    int n = 0;
    
    signal(intsignal, backend_inthandler);
    
    /// Runtime Loop: waits for interrupt signal
    while (n >= 0 && !interrupted) {
        n = lws_service(context, 0);
    }
    
    /// Deinitialize
    lws_context_destroy(context);
    
    return 0;
}
