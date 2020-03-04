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

#include <errno.h>
#include <limits.h>
#include <poll.h>
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


/// Mutex implementations
/// LWS is sort of vague about how it should work with multithreads (the terse
/// documentation just says "don't do it"), but as always, there is a way to do
/// it right.  We are trying different mutex arrangements to allow it to work
/// correctly.

//#define GLOBAL_MUTEX
#define CONNDICT_MUTEX

#ifdef CONNDICT_MUTEX
#   define CONNDICT_LOCK(MUTEX)     pthread_mutex_lock(MUTEX)
#   define CONNDICT_UNLOCK(MUTEX)   pthread_mutex_unlock(MUTEX)
#else
#   define CONNDICT_LOCK(MUTEX);
#   define CONNDICT_UNLOCK(MUTEX);
#endif
#ifdef GLOBAL_MUTEX
#   define GLOBAL_LOCK(MUTEX)     pthread_mutex_lock(MUTEX)
#   define GLOBAL_UNLOCK(MUTEX)   pthread_mutex_unlock(MUTEX)
#else
#   define GLOBAL_LOCK(MUTEX);
#   define GLOBAL_UNLOCK(MUTEX);
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


/// This is a linked-list of connections between a websocket (frontend) and a
/// and a backend socket.  Connections are N:1, such that multiple websockets
/// identities, or vhosts, (i.e. the client side) can be connected to a single 
/// backend daemon.
typedef struct cs {
    int         fd_sock;
    sockmap_t*  sock_handle;
    void*       ws_handle;
    
    struct sockaddr_un addr;
    
    //int             id;
    //unsigned int    flags;
    
//    struct cs*  next;
//    struct cs*  prev;
} conn_t;

typedef struct {
    pthread_t       thread;
    socklist_t*     socklist;
    
#   ifdef GLOBAL_MUTEX
    pthread_mutex_t global_mutex;
#   endif
    
    void*           conndict;
#   ifdef CONNDICT_MUTEX
    pthread_mutex_t conndict_mutex;
#   endif
    
    size_t          bufsize;
    
    int             pollirq_pipe[2];
    bool            pollirq_inactive;
    pthread_cond_t  pollirq_cond;
    pthread_mutex_t pollirq_mutex;
} backend_t;





/// ----- Connection Dictionary ---------
typedef union {
    void* pointer;
    int64_t integer;
    double number;
} dict_union;

struct itemstruct {
    int id;
    conn_t conn;
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
                free(input);
                dels = 1;
                dict->size--;
            }
        }
    }
    return dels;
}


conn_t* dict_add(int* err, void* handle, int id) {
    struct itemstruct* input;
    dict_t* dict;
    
    if (handle == NULL) {
        if (err != NULL) *err = 1;
        return NULL;
    }
    
    dict = handle;
    input = sub_finditem(dict, id);
    if (input != NULL) {
        if (err != NULL) *err = 2;
        return &input->conn;
    }
    
    input = malloc(sizeof(struct itemstruct));
    if (input == NULL) {
        if (err != NULL) *err = 3;
        return NULL;
    }
    input->id = id;
    
    dict->size++;
    if (err != NULL) *err = 0;
    return &input->conn;
}



conn_t* dict_get(void* handle, int id) {
    struct itemstruct* item;
    dict_t* dict;
    
    if (handle != NULL) {
        dict = handle;
        if (dict->base != NULL) {
            item = sub_finditem(dict, id);
            if (item != NULL) {
                return &item->conn;
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



/// Send the IRQ and block until pollirq_cond is received.
void sub_pollirq(backend_t* backend, const char* irq) {
    int wait_test = 0;
    
    write(backend->pollirq_pipe[1], irq, 4);
    
    backend->pollirq_inactive = true;
    while (backend->pollirq_inactive && (wait_test == 0)) {
        wait_test = pthread_cond_wait(&backend->pollirq_cond, &backend->pollirq_mutex);
    }
}






void* poll_unix(void* args) {
    backend_t* backend  = args;
    struct pollfd* fds  = NULL;
    uint8_t* readbuf;
    
    int ready_fds;
    
    // Initial fd list is a single interruptor pipe
    // "poll_groupfds" is the reallocation chunk size
    // "poll_activefds" is the number of actually used fds
    int poll_activefds  = 1;
    int poll_groupfds   = 4;
    int poll_allocfds   = 4;
    
    // Read buffer is allocated to a certain size dictated at wfedd startup
    readbuf = malloc(backend->bufsize * sizeof(uint8_t));
    if (readbuf == NULL) {
        ///@todo global error
        goto poll_unix_TERM;
    }
    
    // Allocate the fd array
    fds = calloc(poll_allocfds, sizeof(struct pollfd));
    if (fds == NULL) {
        ///@todo global error
        goto poll_unix_TERM;
    }
    fds[0].fd       = backend->pollirq_pipe[0];
    fds[0].events   = (POLLIN | POLLNVAL | POLLHUP);
    
    // Main thread loop
    while (1) {
        int delta = 0;
        
        ready_fds = poll(fds, poll_activefds, -1);
        if (ready_fds < 0) {
            // poll() has been interrupted.  Most likely via pthreads_cancel().
            goto poll_unix_TERM;
        }
        if (ready_fds == 0) {
            // This is a timeout with no fds reporting.  
            // This should not occur in the current implementation.
            // It is currently ignored.
            continue;
        }
        
        // Go through the fds[1:], back to front.
        for (int i=(poll_activefds-1); i>0; i--) {
            // The daemon socket has data to forward
            if (fds[i].revents == POLLIN) {
                conn_t* conn;
                void* wsi;
                ssize_t bytes_in;
                
                CONNDICT_LOCK(&backend->conndict_mutex);
                conn    = dict_get(backend->conndict, fds[i].fd);
                wsi     = (conn != NULL) ? conn->ws_handle : NULL;
                CONNDICT_UNLOCK(&backend->conndict_mutex);
            
                if (conn != NULL) {
                    bytes_in = read(fds[i].fd, readbuf, backend->bufsize);
                    if (bytes_in > 0) {
                        frontend_queuemsg(wsi, readbuf, bytes_in);
                    }
                }
            }
            // The daemon socket has dropped!
            else {
                ///@todo drop the corresponding websocket and propagate some error to the client
                // Delete this connection
                CONNDICT_LOCK(&backend->conndict_mutex);
                close(fds[i].fd);
                dict_del(backend->conndict, fds[i].fd);
                delta = -1;
                CONNDICT_UNLOCK(&backend->conndict_mutex);
            }
            
        }
        
        // fds[0] is the irq pipe, which is a special case.
        // It controls alterations to the fds array based on active connections
        if (fds[0].revents != 0) {
            uint8_t irqbuf[4];
            ssize_t bytes_in = 0;
            
            // Anything other than POLLIN on the IRQ is a fatal error
            if (fds[0].revents != POLLIN) {
                ///@todo global error
                goto poll_unix_TERM;
            }
            
            // Read the IRQ command off the pipe.
            while (bytes_in < 4) {
                bytes_in += read(fds[0].fd, &irqbuf[bytes_in], 4-bytes_in);
            }
            
            // There are two interrupts, ADD and DEL
            // ADD may reallocate the fds array
            // DEL will never change allocation
            if (strncmp((const char*)irqbuf, "ADD", 3) == 0) {
                if (poll_allocfds <= poll_activefds) {
                    poll_allocfds = poll_activefds + poll_groupfds;
                    
                    free(fds);
                    fds = calloc(poll_allocfds, sizeof(struct pollfd));
                    if (fds == NULL) {
                        ///@todo global error
                        goto poll_unix_TERM;
                    }
                    fds[0].fd       = backend->pollirq_pipe[0];
                    fds[0].events   = (POLLIN | POLLNVAL | POLLHUP);
                }
                delta           = 1;
                poll_activefds += 1;
            }
            else if (strncmp((const char*)irqbuf, "DEL", 3) == 0) {
                delta           = (poll_activefds < 1) ? -1 : 0;
                poll_activefds += delta;
            }
        }
        
        // Rebuild the fds array if there is some change to the connlist
        if (delta != 0) {
            struct itemstruct* item;
            // Load the connlist.  Mutexed to prevent adds/dels from 
            // happenning concurrently
            CONNDICT_LOCK(&backend->conndict_mutex);
            item = ((dict_t*)backend->conndict)->base;
            for (int i=1; i<poll_activefds; i++) {
                if (item == NULL) {
                    poll_activefds = i;
                    break;
                }
                fds[i].fd       = item->conn.fd_sock;
                fds[i].events   = (POLLIN | POLLNVAL | POLLHUP);
                item            = (struct itemstruct*)(item->hh.next);
            }
            CONNDICT_UNLOCK(&backend->conndict_mutex);
            
            // Send the cond signal to unblock waiters
            ///@note Libwebsockets is single-threaded, but we do this anyway
            pthread_mutex_lock(&backend->pollirq_mutex);
            backend->pollirq_inactive = false;
            pthread_cond_broadcast(&backend->pollirq_cond);
            pthread_mutex_unlock(&backend->pollirq_mutex);
        }
    }

    poll_unix_TERM:
    return NULL;
}








void* backend_start(socklist_t* socklist) {
    backend_t* handle;
    
    // create backend handle
    handle = malloc(sizeof(backend_t));
    if (handle == NULL) {
        goto backend_start_EXIT;
    }
    handle->socklist        = socklist;
    handle->pollirq_pipe[0] = -1;
    handle->pollirq_pipe[1] = -1;
    
    // initialize conndict
    handle->conndict    = dict_init();
    if (handle->conndict == NULL) {
        goto backend_start_TERM1;
    }
    
#   ifdef GLOBAL_MUTEX
    // Initialize poll mutex: This is a global mutex
    if (pthread_mutex_init(&handle->global_mutex, NULL) != 0) {
        goto backend_start_TERM2;
    }
#   endif
#   ifdef CONNDICT_MUTEX
    // Initialize conndict mutex
    if (pthread_mutex_init(&handle->conndict_mutex, NULL) != 0) {
        goto backend_start_TERM3;
    }
#   endif
    
    // Initialize the IRQ cond
    if (pthread_mutex_init(&handle->pollirq_mutex, NULL) != 0) {
        goto backend_start_TERM4;
    }
    if (pthread_cond_init(&handle->pollirq_cond, NULL) != 0) {
        goto backend_start_TERM5;
    }
    
    // initialize the IRQ Pipe
    if (pipe(handle->pollirq_pipe) != 0) {
        goto backend_start_TERM6;
    }
    
    // create the thread
    if (pthread_create(&handle->thread, NULL, &poll_unix, (void*)handle) != 0) {
        goto backend_start_TERM7;
    }
    
    backend_start_EXIT:
    return handle;
    
    backend_start_TERM7:
    close(handle->pollirq_pipe[0]);
    close(handle->pollirq_pipe[1]);
    backend_start_TERM6:
    pthread_cond_destroy(&handle->pollirq_cond);
    backend_start_TERM5:
    pthread_mutex_destroy(&handle->pollirq_mutex);
    backend_start_TERM4:
#   ifdef CONNDICT_MUTEX
    pthread_mutex_destroy(&handle->conndict_mutex);
#   endif
    backend_start_TERM3:
#   ifdef GLOBAL_MUTEX
    pthread_mutex_destroy(&handle->global_mutex);
#   endif
    backend_start_TERM2:
    dict_deinit(handle->conndict);
    backend_start_TERM1:
    free(handle);
    return NULL;
}


void backend_stop(void* handle) {
    backend_t* backend = handle;
    
    if (backend == NULL) {
        return;
    }
    
    // Kill the thread, interrupting poll() as needed
    if (pthread_cancel(backend->thread) == 0) {
        pthread_join(backend->thread, NULL);
    }
    else {
        ///@todo thread could not be cancelled, log some type of error
        return;
    }
    
    // Close the interrupt pipes
    if (backend->pollirq_pipe[0] >= 0) {
        close(backend->pollirq_pipe[0]);
    }
    if (backend->pollirq_pipe[1] >= 0) {
        close(backend->pollirq_pipe[1]);
    }
    
    // Destroy interrupt concurrency resources
    pthread_cond_destroy(&backend->pollirq_cond);
    pthread_mutex_destroy(&backend->pollirq_mutex);
#   ifdef CONNDICT_MUTEX
    pthread_mutex_destroy(&backend->conndict_mutex);
#   endif
    
    // Free the connection list
    dict_deinit(backend->conndict);

    // Free the backend object itself.
    free(backend);
}



int backend_queuemsg(void* backend_handle, void* conn_handle, void* data, size_t len) {
    ///@note backend_handle currently unused.  Might be used in the future.
    //backend_t*  backend = backend_handle;
    conn_t*     conn    = conn_handle;
    int         rc;
    
    if ((backend_handle == NULL) || (conn == NULL) || (data == NULL) || (len == 0)) {
        return -1;
    }
    
    rc = (int)write(conn->fd_sock, data, len);
    
    return rc;
}



/// Used by frontend when a websocket is opened
void* backend_conn_open(void* backend_handle, void* ws_handle, const char* ws_name) {
    backend_t*  backend = backend_handle;
    conn_t*     conn    = NULL;
    sockmap_t*  lsock;
    struct stat statdata;
    int err;
    int fd_sock;
    
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
    fd_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_sock < 0) {
        goto backend_conn_open_TERM1;
    }
    
    // Create a new connection entry based on the new client socket
    CONNDICT_LOCK(&backend->conndict_mutex);
    conn = dict_add(&err, backend->conndict, fd_sock);
    if (err != 0) {
        if (conn != NULL) {
            // this fd already exists in the conndict.  That's a problem that needs to be debugged
            printf("fd collision in conndict (fd = %i)\n", fd_sock);
        }
        goto backend_conn_open_TERM2;
    }
    conn->fd_sock           = fd_sock;
    conn->sock_handle       = lsock;
    conn->ws_handle         = ws_handle;
    conn->addr.sun_family   = AF_UNIX;
    snprintf(conn->addr.sun_path, UNIX_PATH_MAX, "%s", lsock->l_socket);
    
    // Open a connection to the client socket
    if (connect(conn->fd_sock, (struct sockaddr *)&conn->addr, sizeof(struct sockaddr_un)) < 0) {
        goto backend_conn_open_TERM3;
    }
    
    // writing to pollirq_pipe[1] will interrupt the poll() call in the daemon 
    // socket polling thread.  We need to do this in order to have the polling
    // thread uptake the changes in the conndict
    /// 3. the polling thread will rebuild the fds list
    write(backend->pollirq_pipe[1], "ADD", 4);
    CONNDICT_UNLOCK(&backend->conndict_mutex);
    
    backend_conn_open_EXIT:
    return conn;
    
    backend_conn_open_TERM3:
    dict_del(backend->conndict, fd_sock);
    backend_conn_open_TERM2:
    CONNDICT_UNLOCK(&backend->conndict_mutex);
    close(fd_sock);
    backend_conn_open_TERM1:
    return NULL;
    
}


/// Used by frontend when a websocket is closed
void backend_conn_close(void* backend_handle, void* conn_handle) {
    backend_t*  backend = backend_handle;
    conn_t*     conn    = conn_handle;
    int         fd_sock;

    if ((backend_handle == NULL) || (conn_handle == NULL)) {
        return;
    }
    
    // Remove this connection
    CONNDICT_LOCK(&backend->conndict_mutex);
    fd_sock = conn->fd_sock;
    dict_del(backend->conndict, fd_sock);
    CONNDICT_UNLOCK(&backend->conndict_mutex);
    
    // sub_pollirq() will send the IRQ and block until poll thread services it.
    sub_pollirq(backend, "DEL");
    
    // close the connection to the socket & release the connection memory
    close(fd_sock);
}


