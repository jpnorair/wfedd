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




// Local to this project
#include "wfedd_cfg.h"
#include "cliopt.h"
#include "frontend.h"
#include "backend.h"
#include "debug.h"

#include "../local_lib/uthash.h"

#include <libwebsockets.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#ifndef UNIX_PATH_MAX
#   define UNIX_PATH_MAX    104
#endif





/// Threads: only one will be called in wfedd().
/// * poll_unix(): local sockets are UNIX domain sockets 
/// * poll_ubus(): local sockets are ubus (OpenWRT) sockets -- not implemented yet
/// * poll_dbus(): local sockets are dbus sockets -- not implemented yet
/// * poll_tcp(): local sockets are TCP sockets -- not implemented yet
void* poll_unix(void* args);
void* poll_ubus(void* args);
void* poll_dbus(void* args);
void* poll_tcp(void* args);




typedef enum {
    BIRQ_NONE       = 0x00,
    BIRQ_GLOBAL     = 0x01,
    BIRQ_ALL        = 0xFF
} birq_type;

typedef struct {
    int         active;
    int         group;
    int         total;
    bool        remap_pended;
    useconds_t  wait_us;
} fdsparam_t;

typedef struct {
    struct lws_context* ws_context;
    socklist_t*         socklist;
    void*               buf;
    size_t              bufsize;
    volatile birq_type  irq;
    
    // These may be deprecated
    void*               filedict;
    struct pollfd*      fds;
    fdsparam_t          fdsparam;
    
} backend_t;






/// ----- Connection Dictionary ---------
/// This is a connection to a backend client socket from a frontend websocket.
/// Daemons are expected to be able to handle multiple client connections.
/// Connections are stored in a hash table.

typedef struct cs {
    int         fd_ds;
    sockmap_t*  sock_handle;
    mq_t        mqweb;
    mq_t        mqlocal;
} conn_t;

//typedef enum {
//    FILE_DS,
//    FILE_WS,
//    FILE_MAX
//} dict_filetype;

typedef union {
    void* pointer;
    int64_t integer;
    double number;
} dict_union;

struct itemstruct {
    int id;
    //dict_filetype type;
    //conn_t conn;
    void* data;
    UT_hash_handle hh;
};

typedef struct {
    size_t size;
    struct itemstruct* base;
} dict_t;



static struct itemstruct* sub_finditem(dict_t* dict, int id) {
    struct itemstruct* item;
    HASH_FIND_INT(dict->base, &id, item);
    return item;
}


void* dict_init(void) {
    dict_t* dict;
    
    dict = malloc(sizeof(dict_t));
    if (dict != NULL) {
        dict->size = 0;
        dict->base = NULL;
    }

    return dict;
}


void dict_deinit(void* handle) {
    struct itemstruct* tmp;
    struct itemstruct* item;
    
    if (handle != NULL) {
        struct itemstruct* itemtab = ((dict_t*)handle)->base;
        
        HASH_ITER(hh, itemtab, item, tmp) {
            HASH_DEL(itemtab, item);         // delete item (vartab advances to next)
            free(item);
        }
        
        free(handle);
    }
}


int dict_del(void* handle, int id) {
    struct itemstruct* input;
    dict_t* dict;
    int dels = 0;
    
    if (handle != NULL) {
        dict = handle;
        if (dict->base != NULL) {
            input = sub_finditem(dict, id);
            if (input != NULL) {
                HASH_DEL(dict->base, input);
                free(input->data);
                free(input);
                dels = 1;
                dict->size--;
            }
        }
    }
    return dels;
}

/*
void* dict_new(int* err, void* handle, int id, dict_filetype type) {
    struct itemstruct* input;
    dict_t* dict;
    size_t data_alloc;
    int rc;
    void* rp = NULL;
    
    if (handle == NULL) {
        rc = 1;
        goto dict_new_EXIT;
    }
    dict = handle;
    
    input = sub_finditem(dict, id);
    if (input != NULL) {
        rc = 2;
        rp = input->data;
        goto dict_new_EXIT;
    }
    
    input = malloc(sizeof(struct itemstruct));
    if (input == NULL) {
        rc = 3;
        goto dict_new_EXIT;
    }
    
    switch (type) {
        default:
        case FILE_DS:   data_alloc = sizeof(conn_t);
                        break;
        case FILE_WS:   data_alloc = sizeof(struct pollfd);
                        break;
    }
    input->data = malloc(data_alloc);
    if (input->data == NULL) {
        free(input);
        rc = 4;
        goto dict_new_EXIT;
    }
    
    input->id = id;
    dict->size++;
    rp = input->data;
    rc = 0;
    
    dict_new_EXIT:
    if (err != NULL) *err = rc;
    return rp;
}
*/
void* dict_new(int* err, void* handle, int id) {
    struct itemstruct* input;
    dict_t* dict;
    int rc;
    void* rp = NULL;
    
    if (handle == NULL) {
        rc = 1;
        goto dict_new_EXIT;
    }
    dict = handle;
    
    input = sub_finditem(dict, id);
    if (input != NULL) {
        rc = 2;
        rp = input->data;
        goto dict_new_EXIT;
    }
    
    input = malloc(sizeof(struct itemstruct));
    if (input == NULL) {
        rc = 3;
        goto dict_new_EXIT;
    }
    
    input->data = malloc(sizeof(conn_t));
    if (input->data == NULL) {
        free(input);
        rc = 4;
        goto dict_new_EXIT;
    }
    
    input->id = id;
    dict->size++;
    rp = input->data;
    rc = 0;
    
    dict_new_EXIT:
    if (err != NULL) *err = rc;
    return rp;
}



void* dict_get(void* handle, int id) {
    struct itemstruct* item;
    dict_t* dict;
    
    if (handle != NULL) {
        dict = handle;
        if (dict->base != NULL) {
            item = sub_finditem(dict, id);
            if (item != NULL) {
                //if (item->type == type) {
                    return item->data;
                //}
            }
        }
    }

    return NULL;
}

/// --------------------------------------










///@todo could have sig input correspond to some IRQs.
volatile birq_type* birq_pointer;
void backend_inthandler(int sig) {
    *birq_pointer = BIRQ_GLOBAL;
}



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
                struct lws_http_mount* mount) {

    int rc = 0;
    backend_t backend;
    //void* poll_rc;
    int lws_rc = 0;
    
    /// 1. Initialize the backend object
    
    ///@todo take bufsize from cliopts.  Currently hardcoded to 1024.
    backend.bufsize = 1024;
    backend.buf = malloc(backend.bufsize);
    if (backend.buf == NULL) {
        return -1;
    }
    
    /*
    ///@todo take wait_ms from cliopts.  Currently hardcoded to 20ms
    ///@todo take fdsparams from cliopts.  Currently hardcoded.
    backend.fdsparam.wait_us= 20 * 1000;
    backend.fdsparam.active = 0;    // number of currently used fds
    backend.fdsparam.group  = 4;    // reallocation chunk size
    backend.fdsparam.total  = 8;    // total allocated size
    */
    
    // Things that must be initialized externally
    backend.socklist = socklist;

    // initialize filedict
    backend.filedict = dict_init();
    if (backend.filedict == NULL) {
        rc = -2;
        goto backend_run_EXIT;
    }

    /*
    // Configure backend parameters
    backend.fds = calloc(backend.fdsparam.total, sizeof(struct pollfd));
    if (backend.fds == NULL) {
        rc = -3;
        goto backend_run_EXIT;
    }

    // This is a flag that tells the polling loop to restart.
    // It is set to true if the fds array is reallocated.
    backend.fdsparam.remap_pended = false;
    */

    /// 2. Start the frontend.  These are the websockets.  Any messages that 
    ///    are generated by the daemon sockets prior to frontend being online
    ///    will be queued.  The frontend->backend path is more direct, thus 
    ///    the backend is started before the frontend.
    backend.ws_context = frontend_start(&backend, logs_mask, do_hostcheck, 
                            do_fastmonitoring, hostname, port_number, certpath, 
                            keypath, protocols, mount);
    if (backend.ws_context == NULL) {
        rc = -4;
        goto backend_run_EXIT;
    }
    
    /// 3. Configure an IRQ in order to stop wfedd asynchronously. 
    backend.irq     = BIRQ_NONE;
    birq_pointer    = &(backend.irq);
    signal(intsignal, backend_inthandler);
    
    /// 4. Run the service loop.
    while ((backend.irq == BIRQ_NONE) && (lws_rc >= 0)) {
        lws_rc = lws_service(backend.ws_context, 0);
    }
    
    
printf("%s %i\n", __FUNCTION__, __LINE__);
    /// 5. Runtime loop is over, so first close the libwebsockets context, and 
    ///    second, close all the backend socket fds.
    ///@todo detach signal?
    frontend_stop(backend.ws_context);
printf("%s %i\n", __FUNCTION__, __LINE__);
    {   struct itemstruct*  dict_item;

        // Start at the front of the filedict linked-list
        dict_item = ((dict_t*)backend.filedict)->base;
        while (dict_item != NULL) {
            //if (dict_item->type == FILE_DS) {
                close( ((conn_t*)dict_item->data)->fd_ds );
            //}
            //else if (dict_item->type == FILE_WS) {
            //    close( ((struct pollfd*)dict_item->data)->fd );
            //}
            dict_item = (struct itemstruct*)(dict_item->hh.next);
        }
    }
printf("%s %i\n", __FUNCTION__, __LINE__);
    backend_run_EXIT:
    switch (rc) {
        default:    
        case -4:    //free(backend.fds);
        case -3:    dict_deinit(backend.filedict);
        case -2:    free(backend.buf);
        case -1:    break;
    }
    return rc;
}



int conn_putmsg_forweb(void* conn_handle, void* data, size_t len) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    conn_t* conn;
    mq_msg_t* msg;

    if ((conn_handle == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }
    
    conn = conn_handle;
    msg = frontend_createmsg(data, len);
    if (msg == NULL) {
        return -2;
    }
    
    mq_putmsg(&conn->mqweb, msg);
    return 0;
}

int conn_putmsg_forlocal(void* conn_handle, void* data, size_t len) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    conn_t* conn;
    mq_msg_t* msg;

    if ((conn_handle == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }

    conn = conn_handle;
    msg = msg_new(len);
    if (msg == NULL) {
        return -2;
    }
    
    memcpy((void*)msg->data, data, len);
    mq_putmsg(&conn->mqlocal, msg);
    return 0;
}


mq_msg_t* conn_getmsg_forweb(void* conn_handle) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    mq_msg_t* msg = NULL;
    if (conn_handle != NULL) {
        msg = mq_getmsg( &(((conn_t*)conn_handle)->mqweb) );
    }
    return msg;
}

mq_msg_t* conn_getmsg_forlocal(void* conn_handle) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    mq_msg_t* msg = NULL;
    if (conn_handle != NULL) {
        msg = mq_getmsg( &(((conn_t*)conn_handle)->mqlocal) );
    }
    return msg;
}


bool conn_hasmsg_forweb(void* conn_handle) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    bool result = false;
    if (conn_handle != NULL) {
        result = !mq_isempty( &(((conn_t*)conn_handle)->mqweb) );
    }
    return result;
}

bool conn_hasmsg_forlocal(void* conn_handle) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    bool result = false;
    if (conn_handle != NULL) {
        result = !mq_isempty( &(((conn_t*)conn_handle)->mqlocal) );
    }
    return result;
}








void* conn_new(void* backend_handle, const char* ws_name) {
    backend_t*  backend = backend_handle;
    conn_t*     conn    = NULL;
    sockmap_t*  lsock   = NULL;
    int fd_ds;
    int err;

    if ((backend_handle == NULL) || (ws_name == NULL)) {
        return NULL;
    }

    // Create a new client socket to the daemon mapped to the specified websocket
    fd_ds = socklist_newclient(&lsock, backend->socklist, ws_name);
    if (fd_ds < 0) {
        goto conn_new_TERM1;
    }

    // Create a new connection entry based on the new client socket
    conn = (conn_t*)dict_new(&err, backend->filedict, fd_ds);
    if (err != 0) {
        if (conn != NULL) {
            // this fd already exists in the filedict.  That's a problem that needs to be debugged
            printf("fd collision in filedict (fd = %i)\n", fd_ds);
        }
        goto conn_new_TERM2;
    }

    conn->fd_ds         = fd_ds;
    conn->sock_handle   = lsock;
    mq_init(&conn->mqweb);
    mq_init(&conn->mqlocal);
    return conn;
    
    // De-allocate on failures
    dict_del(backend->filedict, fd_ds);
    conn_new_TERM2:
    close(fd_ds);
    conn_new_TERM1:
    return NULL;
}


void conn_del(void* backend_handle, void* conn_handle) {
printf("%s %i\n", __FUNCTION__, __LINE__);
    backend_t*  backend = backend_handle;
    conn_t*     conn    = conn_handle;

    if ((backend_handle != NULL) && (conn_handle != NULL)) {
        // Remap the poll array without the removed connection
        dict_del(backend->filedict, conn->fd_ds);
        //sub_remap_withdel(backend);
    }
}




int conn_open(void* conn_handle) {
/// Used by frontend when a websocket opens a client connection.
printf("%s %i\n", __FUNCTION__, __LINE__);
    int rc;
    conn_t* conn;
    struct sockaddr_un addr;
    
    if (conn_handle == NULL) {
        return -1;
    }
    conn = conn_handle;
    
    // Open a connection to the client socket
    ///@todo the connection procedure could be different for different conn types
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, conn->sock_handle->l_socket, UNIX_PATH_MAX);
    
    rc = connect(conn->fd_ds, (struct sockaddr *)&addr, sizeof(struct sockaddr_un));
    return rc;
}



void conn_close(void* conn_handle) {
/// Used by frontend when a websocket closes a client connection
printf("%s %i\n", __FUNCTION__, __LINE__);
    if (conn_handle == NULL) {
        return;
    }
    
    // Close this connection
    ///@todo might be different ways to close based on different connection types
    close ( ((conn_t*)conn_handle)->fd_ds );
}



int conn_readraw_local(void** data, void* backend_handle, void* conn_handle) {
/// returns the number of bytes read, or negative on error.
/// "data" parameter stores a void* output
/// backend_handle is needed to locate the read buffer
/// conn_handle is needed to determine the type of read to be done.
    backend_t* backend;
    conn_t* conn;
    int bytes_in;

    if ((data == NULL) || (backend_handle == NULL) || (conn_handle == NULL)) {
        return -1;
    }
    backend = backend_handle;
    conn    = conn_handle;
    
    ///@todo currently there is only one type of read, via read()
    bytes_in = (int)read(conn->fd_ds, backend->buf, backend->bufsize);
    
    *data = backend->buf;
    return bytes_in;
}



int conn_writeraw_local(void* backend_handle, void* conn_handle, void* data, size_t len) {
/// returns the number of bytes read, or negative on error.
/// "data" parameter stores a void* output
/// backend_handle is needed to locate the read buffer
/// conn_handle is needed to determine the type of read to be done.
    backend_t* backend;
    conn_t* conn;

    if ((data == NULL) || (len == 0) || (backend_handle == NULL) || (conn_handle == NULL)) {
        return -1;
    }
    backend = backend_handle;
    conn    = conn_handle;
    
    ///@todo currently there is only one type of write, via write()
    return (int)write(conn->fd_ds, data, len);
}



lws_adoption_type conn_get_adoptiontype(void* conn_handle) {
    lws_adoption_type type;
    //conn_t* conn;
    if (conn_handle) {
        ///@todo there's only one type of conn at this moment.
        //conn = conn_handle;
        type = LWS_ADOPT_RAW_FILE_DESC;
    }
    else {
        type = 0;
    }
    return type;
}


int conn_get_descriptor(void* conn_handle) {
    conn_t* conn;
    if (conn_handle) {
        ///@todo there's only one type of conn at this moment.
        conn = conn_handle;
        return conn->fd_ds;
    }
    return -1;
}


const char* conn_get_protocolname(void* conn_handle) {
    static const char* pname = "CLI";
    return pname;
}
