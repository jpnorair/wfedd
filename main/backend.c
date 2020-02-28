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
#include "backend.h"
#include "debug.h"

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
    void*       ws_handle;
    sockmap_t*  sock_handle;
    
    struct sockaddr_un addr;
    int             fd_sock;
    //int             id;
    //unsigned int    flags;
    
    struct cs*  next;
    struct cs*  prev;
} conn_t;

typedef struct {
    pthread_t       thread;
    socklist_t*     socklist;
    
    pthread_mutex_t connlist_mutex;
    conn_t*         connlist;
    conn_t*         connlist_tail;
    
    int             pollirq_pipe[2];
    bool            pollirq_inactive;
    pthread_cond_t  pollirq_cond;
    pthread_mutex_t pollirq_mutex;
} backend_t;



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
    int ready_fds;
    
    // Initial fd list is a single interruptor pipe
    // "poll_groupfds" is the reallocation chunk size
    // "poll_activefds" is the number of actually used fds
    int poll_activefds  = 1;
    int poll_groupfds   = 4;
    int poll_allocfds   = 4;
    
    fds = calloc(poll_allocfds, sizeof(struct pollfd));
    if (fds == NULL) {
        ///@todo global error
        goto poll_unix_TERM;
    }
    fds[0].fd       = backend->pollirq_pipe[0];
    fds[0].events   = (POLLIN | POLLNVAL | POLLHUP);
    
    while (1) {
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
        
        for (int i = (poll_activefds-1); i>0; i--) {
            ///@todo data forwarding
        }
        
        // fds[0] is the irq pipe, which is a special case.
        // It controls alterations to the fds array based on active connections
        if (fds[0].revents != 0) {
            uint8_t irqbuf[4];
            int     delta;
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
            if (strncmp((const char*)irqbuf, "ADD", 4) == 0) {
                if ((poll_activefds % poll_groupfds) == 0) {
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
            else if (strncmp((const char*)irqbuf, "DEL", 4) == 0) {
                delta           = (poll_activefds < 1) ? -1 : 0;
                poll_activefds += delta;
            }
            else {
                delta = 0;
            }
            
            // if ADD or DEL is found, reload latest connlist into fds array
            if (delta != 0) {
                conn_t* conn;
                
                // Load the connlist.  Mutexed to prevent adds/dels from 
                // happenning concurrently
                pthread_mutex_lock(&backend->connlist_mutex);
                conn = backend->connlist;
                for (int i=1; i<poll_activefds; i++) {
                    if (conn == NULL) {
                        poll_activefds = i;
                        break;
                    }
                    fds[i].fd       = conn->fd_sock;
                    fds[i].events   = (POLLIN | POLLNVAL | POLLHUP);
                    conn            = conn->next;
                }
                pthread_mutex_unlock(&backend->connlist_mutex);
                
                // Send the cond signal to unblock waiters
                ///@note Libwebsockets is single-threaded, but we do this anyway
                pthread_mutex_lock(&backend->pollirq_mutex);
                backend->pollirq_inactive = false;
                pthread_cond_broadcast(&backend->pollirq_cond);
                pthread_mutex_unlock(&backend->pollirq_mutex);
            }
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
    
    // initialize connlist
    handle->connlist        = NULL;
    handle->connlist_tail   = NULL;
    if (pthread_mutex_init(&handle->connlist_mutex, NULL) != 0) {
        goto backend_start_TERM1;
    }
    
    // Initialize the IRQ cond
    if (pthread_mutex_init(&handle->pollirq_mutex, NULL) != 0) {
        goto backend_start_TERM2;
    }
    if (pthread_cond_init(&handle->pollirq_cond, NULL) != 0) {
        goto backend_start_TERM3;
    }
    
    // initialize the IRQ Pipe
    if (pipe(handle->pollirq_pipe) != 0) {
        goto backend_start_TERM4;
    }
    
    // create the thread
    if (pthread_create(&handle->thread, NULL, &poll_unix, (void*)handle) != 0) {
        goto backend_start_TERM5;
    }
    
    backend_start_EXIT:
    return handle;
    
    backend_start_TERM5:
    close(handle->pollirq_pipe[0]);
    close(handle->pollirq_pipe[1]);
    backend_start_TERM4:
    pthread_cond_destroy(&handle->pollirq_cond);
    backend_start_TERM3:
    pthread_mutex_destroy(&handle->pollirq_mutex);
    backend_start_TERM2:
    pthread_mutex_destroy(&handle->connlist_mutex);
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
    
    // Clear the connection list
    
    // Clear all other local resources (if any)
    
    if (backend->pollirq_pipe[0] >= 0) {
        close(backend->pollirq_pipe[0]);
    }
    if (backend->pollirq_pipe[1] >= 0) {
        close(backend->pollirq_pipe[1]);
    }

//    if (backend != NULL) {
//        pthread_cancel(backend->thread);
//    
//    }
}



/// Used by frontend when a websocket is opened
void* backend_conn_open(void* backend_handle, void* ws_handle, const char* ws_name) {
    backend_t*  backend = backend_handle;
    conn_t*     conn    = NULL;
    sockmap_t*  lsock;
    struct stat statdata;
    
    if ((backend_handle == NULL) || (ws_handle == NULL) || (ws_name == NULL)) {
        return NULL;
    }
    
    // make sure the socket is in the list
    lsock = sub_socklist_search(backend->socklist, ws_name);
    if (lsock == NULL) {
        goto backend_conn_open_EXIT;
    }
    
    // Create the connection object
    conn = malloc(sizeof(conn_t));
    if (conn == NULL) {
        ///@todo some sort of error handling
        goto backend_conn_open_EXIT;
    }
    conn->sock_handle   = lsock;
    conn->ws_handle     = ws_handle;
    
    // Test if the socket_path argument is indeed a path to a socket
    if (stat(lsock->l_socket, &statdata) != 0) {
        goto backend_conn_open_TERM1;
    }
    if (S_ISSOCK(statdata.st_mode) == 0) {
        goto backend_conn_open_TERM1;
    }
        
    // Create a client socket
    conn->fd_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (conn->fd_sock < 0) {
        goto backend_conn_open_TERM1;
    }
    conn->addr.sun_family = AF_UNIX;
    snprintf(conn->addr.sun_path, UNIX_PATH_MAX, "%s", lsock->l_socket);
        
    // Open a connection to the client socket
    if (connect(conn->fd_sock, (struct sockaddr *)&conn->addr, sizeof(struct sockaddr_un)) < 0) {
        goto backend_conn_open_TERM2;
    }
    
    // link this connection into the list
    pthread_mutex_lock(&backend->connlist_mutex);
    conn->next              = NULL;
    conn->prev              = backend->connlist_tail;
    backend->connlist_tail  = conn;
    
    // writing to pollirq_pipe[1] will interrupt the poll() call in the daemon 
    // socket polling thread.  We need to do this in order to have the polling
    // thread uptake the changes in the connlist
    /// 3. the polling thread will rebuild the fds list
    write(backend->pollirq_pipe[1], "ADD", 4);
    pthread_mutex_unlock(&backend->connlist_mutex);
    
    backend_conn_open_EXIT:
    return conn;
    
    backend_conn_open_TERM2:
    close(conn->fd_sock);
    backend_conn_open_TERM1:
    free(conn);
    return NULL;
    
}


/// Used by frontend when a websocket is closed
void backend_conn_close(void* backend_handle, void* conn_handle) {
    backend_t*  backend = backend_handle;
    conn_t*     conn    = conn_handle;

    if ((backend_handle == NULL) || (conn_handle == NULL)) {
        return;
    }
    
    // Unlink this connection
    pthread_mutex_lock(&backend->connlist_mutex);
    conn->next->prev = conn->prev;
    if (conn->prev != NULL) {
        conn->prev->next = conn->next;
    }
    pthread_mutex_unlock(&backend->connlist_mutex);
    
    // sub_pollirq() will send the IRQ and block until poll thread services it.
    sub_pollirq(backend, "DEL");
    
    // close the connection to the socket & release the connection memory
    close(conn->fd_sock);
    free(conn);
}


