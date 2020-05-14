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
    void*               filedict;
    struct pollfd*      fds;
    fdsparam_t          fdsparam;
    size_t              bufsize;
    birq_type           irq;
} backend_t;






/// ----- Connection Dictionary ---------
/// This is a connection to a backend client socket from a frontend websocket.
/// Daemons are expected to be able to handle multiple client connections.
/// Connections are stored in a hash table.

typedef struct cs {
    int         fd_ds;
    sockmap_t*  sock_handle;
    void*       ws_handle;
    mq_t        msgq;
    struct sockaddr_un addr;
} conn_t;

typedef enum {
    ITEM_CONN,
    ITEM_WSPOLLFD,
    ITEM_MAX
} dict_itemtype;

typedef union {
    void* pointer;
    int64_t integer;
    double number;
} dict_union;

struct itemstruct {
    int id;
    dict_itemtype type;
    
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

    return NULL;
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


void* dict_new(int* err, void* handle, int id, dict_itemtype type) {
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
        case ITEM_CONN:         data_alloc = sizeof(conn_t);        break;
        case ITEM_WSPOLLFD:     data_alloc = sizeof(struct pollfd); break;
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



void* dict_get(void* handle, int id, dict_itemtype type) {
    struct itemstruct* item;
    dict_t* dict;
    
    if (handle != NULL) {
        dict = handle;
        if (dict->base != NULL) {
            item = sub_finditem(dict, id);
            if (item != NULL) {
                if (item->type == type) {
                    return item->data;
                }
            }
        }
    }

    return NULL;
}

/// --------------------------------------






/// Search through the socklist to find the socket corresponding to supplied
/// websocket.  ws_name refers to the "protocol name", from libwebsockets.
/// Each "protocol name" must be bridged 1:1 to a corresponding daemon.
///
/// Called only when opening a new websocket (i.e. client connection)
///
/// Currently, the search is a linear search, because wfedd is not expected to
/// be used with very many daemons.  If that changes, we can change this easily
/// to a binary search, because the websocket:daemon bridging never changes
/// during runtime.
sockmap_t* sub_socklist_search(socklist_t* socklist, const char* ws_name) {
    int i;
    
    if ((socklist == NULL) || (ws_name == NULL)) {
        return NULL;
    }
    
    for (i=0; i<socklist->size; i++) {
        if (strcmp(socklist->map[i].websocket, ws_name) == 0) {
            return &socklist->map[i];
        }
    }

    return NULL;
}


void sub_remap_innerloop(backend_t* backend) {
/// Remap the pollfds array based on a revised filedict
    struct itemstruct*  dict_item;
    int i = 0;
    
    // Start at the front of the filedict linked-list
    dict_item = ((dict_t*)backend->filedict)->base;
    
    // Move through the list until reaching a NULL item (the end of it).
    // load the updated filedict information into the backend fds array
    while (dict_item != NULL) {
        if (dict_item->type == ITEM_CONN) {
            backend->fds[i].fd      = ((conn_t*)dict_item->data)->fd_ds;
            backend->fds[i].events  = (POLLIN | POLLNVAL | POLLHUP);
        }
        else {
            backend->fds[i] = *((struct pollfd*)dict_item->data);
        }
        dict_item = (struct itemstruct*)(dict_item->hh.next);
        i++;
    }
    
    backend->fdsparam.active        = i;
    backend->fdsparam.remap_pended  = true;
}


int sub_remap_withadd(backend_t* backend) {
    int rc = 0;
    
    if (backend->fds == NULL) {
        return -1;
    }

    // Reallocate the fds if that is necessary
    // Allocation is done in chunks of size = backend->fdsparam.group
    ///@note sub_remap_innerloop() will reload the filedict information into
    ///      backend->fds
    if (backend->fdsparam.total <= backend->fdsparam.active) {
        struct pollfd*  fds;
        backend->fdsparam.total = backend->fdsparam.active + backend->fdsparam.group;
        
        fds = calloc(backend->fdsparam.total, sizeof(struct pollfd));
        if (fds == NULL) {
            rc = -2;
            goto backend_conn_add_EXIT;
        }
        
        // Free the old data.  It will get reloaded in sub_remap_innerloop().
        free(backend->fds);
        backend->fds = fds;
    }
    
    backend->fdsparam.active += 1;
    sub_remap_innerloop(backend);
    
    backend_conn_add_EXIT:
    return rc;
}


int sub_remap_withdel(backend_t* backend) {
    int rc = 0;
    
    if (backend->fds == NULL) {
        return -1;
    }
    
    if (backend->fdsparam.active >= 1) {
        backend->fdsparam.active--;
        sub_remap_innerloop(backend);
    }
    
    return rc;
}





void* poll_unix(void* args) {
    backend_t* backend  = args;
    uint8_t* readbuf;
    int ready_fds;
    
    // Read buffer is allocated to a certain size dictated at wfedd startup
    readbuf = malloc(backend->bufsize * sizeof(uint8_t));
    if (readbuf == NULL) {
        ///@todo global error
        goto poll_unix_TERM;
    }
    
    // Main operating Loop
    poll_unix_MAIN:
    backend->fdsparam.remap_pended = false;
    
    // If there are no active fds, we wait for a period.
    if (backend->fdsparam.active <= 0) {
        usleep(backend->fdsparam.wait_us);
        goto poll_unix_MAIN;
    }
    
    ready_fds = poll(backend->fds, backend->fdsparam.active, backend->fdsparam.wait_us/1000 );
    if (ready_fds < 0) {
        // poll() has been interrupted.
        // This can happen if the thread is cancelled.
        goto poll_unix_TERM;
    }
    if (ready_fds == 0) {
        // poll() has timed-out.
        // - This is a routine service interval.
        // - Check the backend status flag and take appropriate actions.
        switch (backend->irq) {
            case BIRQ_NONE:     goto poll_unix_MAIN;
            case BIRQ_GLOBAL:   goto poll_unix_TERM;
            default:            goto poll_unix_TERM;
        }
    }
    
    // Go through the fd array and service all fds with pending data
    // The first operation is lws_service_fd(), which is part of libwebsockets.
    // It will deal with any fd that has a websocket on it, and then it will
    // set the revents of that pollfd to zero.
    for (int i=0; i<backend->fdsparam.active; i++) {
        
        // Handle websockets.  Websockets (the frontend) will open or close
        // client connections to backend daemons as they open or close, so we
        // need to consider that the pollfd array will change.  That's what
        // remap_pended flag is for.
        lws_service_fd(backend->ws_context, &(backend->fds[i]));
        if (backend->fdsparam.remap_pended) {
            goto poll_unix_MAIN;
        }
    
        // Websocket case has been handled above.  If FD was a websocket, now
        // the revents will be 0.  So, if FD revents is nonzero, then there's 
        // a daemon cilent socket that has something to handle.
        if (backend->fds[i].revents == POLLIN) {
            ssize_t bytes_in;
            conn_t* conn;
            
            // Conn object contains the link to the websocket frontend.
            // Forward the data on this client socket to the websocket.
            conn = (conn_t*)dict_get(backend->filedict, backend->fds[i].fd, ITEM_CONN);
            if (conn != NULL) {
                bytes_in = read(backend->fds[i].fd, readbuf, backend->bufsize);
                if (frontend_createmsg(readbuf, bytes_in) != NULL) {
                    frontend_pendmsg(conn->ws_handle);
                }
            }
        }
        
        // FD is not a websocket (it is daemon client) and does not have new data.
        // We consider this to mean that the daemon client socket has dropped.
        // As a result, we close this connection.
        else if (backend->fds[i].revents != 0) {
            ///@todo drop the corresponding websocket and propagate some error to the client
            close(backend->fds[i].fd);
            dict_del(backend->filedict, backend->fds[i].fd);
            sub_remap_withdel(backend);
            goto poll_unix_MAIN;
        }
    }

    poll_unix_TERM:
    return NULL;
}




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
    void* poll_rc;
    
    /// 1. Initialize the backend object
    
    // Things that must be initialized externally
    backend.socklist = socklist;
    
    // initialize filedict
    backend.filedict = dict_init();
    if (backend.filedict == NULL) {
        rc = -1;
        goto backend_run_EXIT;
    }
    
    // Configure backend parameters
    backend.fds = calloc(backend.fdsparam.total, sizeof(struct pollfd));
    if (backend.fds == NULL) {
        rc = -2;
        goto backend_run_EXIT;
    }
    
    // Initial fd list is a single interruptor pipe
    // "poll_groupfds" is the reallocation chunk size
    // "poll_activefds" is the number of actually used fds
    backend.fdsparam.active = 0;
    backend.fdsparam.group  = 4;
    backend.fdsparam.total  = 8;
    
    // This is a flag that tells the polling loop to restart.
    // It is set to true if the fds array is reallocated.
    backend.fdsparam.remap_pended = false;
    
    // This is the amount of microseconds to wait if there are no websockets
    // and no daemons to poll.  Default is 10 ms.
    backend.fdsparam.wait_us = 10 * 1000;
    
    /// 2. Initialize the frontend and link it back into the websocket context
    backend.ws_context = frontend_start(&backend, logs_mask, do_hostcheck, 
                            do_fastmonitoring, hostname, port_number, certpath, 
                            keypath, protocols, mount);
    if (backend.ws_context == NULL) {
        rc = -3;
        goto backend_run_EXIT;
    }
    
    /// 3. Run the backend loop.  There's an interrupt signal configured to
    ///    stop the backend loop, which is generally SIGINT.
    /// @todo maybe there's a way to pass a backend handle via sigaction.
    birq_pointer = &(backend.irq);
    signal(intsignal, backend_inthandler);
    
    ///@note The architecture used here could allow threads, but it's much easier
    ///      to use libwebsockets in a single-threaded manner.
    ///@todo select the appropriate poll loop based on socket daemon specification.
    poll_rc = poll_unix((void*)&backend);
    
    /// 4. Runtime loop is over, so first close the libwebsockets context, and 
    ///    second, close all the backend socket fds.
    ///@todo detach signal?
    frontend_stop(backend.ws_context);
    
    {   struct itemstruct*  dict_item;

        // Start at the front of the filedict linked-list
        dict_item = ((dict_t*)backend.filedict)->base;
        while (dict_item != NULL) {
            if (dict_item->type == ITEM_CONN) {
                close( ((conn_t*)dict_item->data)->fd_ds );
            }
            ///@note not sure if we actually need to close these, or if lws does it for us.
            else if (dict_item->type == ITEM_WSPOLLFD) {
                close( ((struct pollfd*)dict_item->data)->fd );
            }
            dict_item = (struct itemstruct*)(dict_item->hh.next);
        }
    }
    
    backend_run_EXIT:
    switch (rc) {
        default:    
        case -3:    free(backend.fds);
        case -2:    dict_deinit(backend.filedict);
        case -1:    break;
    }
    return rc;
}





int conn_putmsg_outbound(void* conn_handle, void* data, size_t len) {
    ///@note backend_handle currently unused.  Might be used in the future.
    conn_t*     conn    = conn_handle;
    msgq_entry_t* msg;
    
    if ((conn == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }

    msg = frontend_createmsg(data, len);
    if (msg == NULL) {
        return -2;
    }

    mq_putmsg(&conn->msgq, msg);
    return 0;
}


msgq_entry_t* conn_getmsg_outbound(void* conn_handle) {
    conn_t* conn = conn_handle;
    msgq_entry_t* msg = NULL;
    
    if (conn != NULL) {
        msg = mq_getmsg(&conn->msgq);
    }
    
    return msg;
}


bool conn_hasmsg_outbound(void* conn_handle) {
    conn_t* conn = conn_handle;
    bool result = false;
    
    if (conn != NULL) {
        result = !mq_isempty(&conn->msgq);
    }
    return result;
}


int conn_putmsg_inbound(void* conn_handle, void* data, size_t len) {
/// Unlinke conn_ds_putmsg(), the ws conjugate just dumps information onto the
/// Daemon socket via write.  The daemon socket is a client socket used only
/// by the conjugate websocket, and it is served by a separate process, so 
/// we don't need a queue like we need for data going to the websocket.
    conn_t* conn = conn_handle;
    int     rc;
    
    ///@todo get rid of backend_handle as long as this can be made universal across msg API
    if ((conn == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }
    
    rc = (int)write(conn->fd_ds, data, len);
    
    return rc;
}






int pollfd_open(void* backend_handle, struct pollfd* ws_pollfd) {
    backend_t* backend;
    struct pollfd* newpollfd;
    int err = 0;
    
    if ((backend_handle == NULL) || (ws_pollfd == NULL)) {
        return -1;
    }
    backend = backend_handle;
    
    // Create a new ws pollfd entry based on the new client socket
    newpollfd = (struct pollfd*)dict_new(&err, backend->filedict, ws_pollfd->fd, ITEM_WSPOLLFD);
    if (err != 0) {
        if (newpollfd != NULL) {
            // this fd already exists in the filedict.  That's a problem that needs to be debugged
            printf("fd collision in filedict (fd = %i)\n", ws_pollfd->fd);
        }
    }
    else {
        *newpollfd = *ws_pollfd;
    }
    
    return err;
}



int pollfd_close(void* backend_handle, struct pollfd* ws_pollfd) {
    backend_t* backend;
    int deletions = 0;
    
    if ((backend_handle == NULL) || (ws_pollfd == NULL)) {
        return -1;
    }
    backend = backend_handle;
    
    // Delete a ws pollfd entry
    deletions = dict_del(backend->filedict, ws_pollfd->fd);
    switch (deletions) {
        case 0: printf("fd marked for delete not found in filedict (fd = %i)\n", ws_pollfd->fd);
                return -2;
        case 1: return 0;
       default: printf("fd marked for delete not unique in filedict (fd = %i)\n", ws_pollfd->fd);
                return deletions;
    }
}



int pollfd_update(void* backend_handle, struct pollfd* ws_pollfd) {
    backend_t* backend;
    struct pollfd* modpollfd;
    
    if ((backend_handle == NULL) || (ws_pollfd == NULL)) {
        return -1;
    }
    backend = backend_handle;
    
    // Create a new ws pollfd entry based on the new client socket
    modpollfd = (struct pollfd*)dict_get(backend->filedict, ws_pollfd->fd, ITEM_WSPOLLFD);
    if (modpollfd == NULL) {
        printf("fd not found in filedict (fd = %i)\n", ws_pollfd->fd);
        return -1;
    }

    *modpollfd = *ws_pollfd;
    return 0;
}



void* conn_open(void* backend_handle, void* ws_handle, const char* ws_name) {
/// Used by frontend when a websocket opens a client connection
    backend_t*  backend = backend_handle;
    conn_t*     conn    = NULL;
    sockmap_t*  lsock;
    struct stat statdata;
    int err;
    int fd_ds;
    
    if ((backend_handle == NULL) || (ws_handle == NULL) || (ws_name == NULL)) {
        return NULL;
    }
    
    // make sure the socket is in the list
    lsock = sub_socklist_search(backend->socklist, ws_name);
    if (lsock == NULL) {
        goto backend_conn_open_EXIT;
    }
    
    // Test if the socket_path argument is indeed a path to a socket
    if (stat(lsock->l_socket, &statdata) != 0) {
        goto backend_conn_open_TERM1;
    }
    if (S_ISSOCK(statdata.st_mode) == 0) {
        goto backend_conn_open_TERM1;
    }
    
    // Create a client socket
    fd_ds = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ds < 0) {
        goto backend_conn_open_TERM1;
    }
    
    // Create a new connection entry based on the new client socket
    conn = (conn_t*)dict_new(&err, backend->filedict, fd_ds, ITEM_CONN);
    if (err != 0) {
        if (conn != NULL) {
            // this fd already exists in the filedict.  That's a problem that needs to be debugged
            printf("fd collision in filedict (fd = %i)\n", fd_ds);
        }
        goto backend_conn_open_TERM2;
    }
    
    conn->fd_ds             = fd_ds;
    conn->sock_handle       = lsock;
    conn->ws_handle         = ws_handle;
    conn->addr.sun_family   = AF_UNIX;
    snprintf(conn->addr.sun_path, UNIX_PATH_MAX, "%s", lsock->l_socket);
    mq_init(&conn->msgq);
    
    // Open a connection to the client socket
    if (connect(conn->fd_ds, (struct sockaddr *)&conn->addr, sizeof(struct sockaddr_un)) < 0) {
        goto backend_conn_open_TERM3;
    }
    
    // Finally, remap the backend polling array
    sub_remap_withadd(backend);
    
    // Successful exit
    backend_conn_open_EXIT:
    return conn;
    
    backend_conn_open_TERM3:
    dict_del(backend->filedict, fd_ds);
    backend_conn_open_TERM2:
    close(fd_ds);
    backend_conn_open_TERM1:
    return NULL;
    
}



void conn_close(void* backend_handle, void* conn_handle) {
/// Used by frontend when a websocket closes a client connection
    backend_t*  backend = backend_handle;
    conn_t*     conn    = conn_handle;
    int         fd_ds;

    if ((backend_handle == NULL) || (conn_handle == NULL)) {
        return;
    }
    
    // Close this connection
    fd_ds = conn->fd_ds;
    close(fd_ds);
    
    // Remap the poll array without the removed connection
    dict_del(backend->filedict, fd_ds);
    sub_remap_withdel(backend);
}


