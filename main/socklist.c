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
#include "debug.h"
#include "socklist.h"

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




int socklist_init(socklist_t** sl_handle, size_t maxsize) {
    if (sl_handle == NULL) {
        return -1;
    }
    
    *sl_handle = malloc(sizeof(socklist_t));
    if (sl_handle == NULL) {
        return -2;
    }
    
    (*sl_handle)->size  = 0;
    (*sl_handle)->alloc = maxsize;
    (*sl_handle)->map   = calloc(maxsize, sizeof(sockmap_t));
    if ((*sl_handle)->map == NULL) {
        free(*sl_handle);
        *sl_handle = NULL;
        return -3;
    }
    
    return 0;
}



void socklist_deinit(socklist_t* socklist) {
    if (socklist != NULL) {
        free(socklist->map);
        free(socklist);
    }
}



int sub_testsocket(const char* sockpath, int socktype) {
/// Test if the socket_path argument is indeed a path to a socket.
///@todo integrate socktype into the checks.
    struct stat statdata;

    if (stat(sockpath, &statdata) != 0) {
        return -2;
    }
    if (S_ISSOCK(statdata.st_mode) == 0) {
        return -3;
    }
    
    return 0;
}





int socklist_addmap(socklist_t* socklist, const char* mapstr) {
    const char* ds;
    const char* ds_end;
    const char* ws;
    const char* ws_end;
    int ds_size;
    int ws_size;
    char* dspath;
    char* wspath;
    int i;
    int rc = 0;
    
    if (socklist == NULL) {
        return -1;
    }
    
    /// 1. make sure socklist isn't full.
    if (socklist->size >= socklist->alloc) {
        return -2;
    }
    
    /// 2. The format of the mapstr is shown below, with a ':' separator.
    ///    local-socket-path:websocket-path
    ds      = mapstr;
    ds_end  = strchr(mapstr, ':');
    ws      = ds_end + 1;
    ws_end  = strchr(ws, 0);
    if ((ds == NULL) || (ds_end == NULL) || (ws == NULL) || (ws_end == NULL)) {
        //printf("Error: socket input \"%s\" is not correctly formatted.\n", mapstr);
        return -3;
    }
    
    /// 3. Create proper strings for ds and ws.
    ds_size = (int)(ds_end - ds);
    ws_size = (int)(ws_end - ws);
    
    dspath  = calloc((ds_size + 1), sizeof(char));
    if (dspath == NULL) {
        return -4;
    }
    memcpy(dspath, ds, ds_size);
    
    wspath  = calloc((ws_size + 1), sizeof(char));
    if (wspath == NULL) {
        rc = -4;
        goto socklist_addmap_TERM;
    }
    memcpy(wspath, ws, ws_size);
    
    ///@todo 3. Validate that the daemon socket exists and is of the right type.
    rc = sub_testsocket(dspath, 0);
    if (rc != 0) {
        rc -= 5;
        goto socklist_addmap_TERM;
    }
    
    /// 4. Do insertion based on the name of the websocket.
    ///    Only one instance per websocket name is allowed.
    for (i=0; i<socklist->size; i++) {
        int cmp = strcmp(wspath, socklist->map[i].websocket);
        if (cmp == 0) {
            rc = -5;
            goto socklist_addmap_TERM;
        }
        if (cmp < 0) {
            for (int j=(int)socklist->size; j>i; j--) {
                socklist->map[j] = socklist->map[j-1];
            }
            break;
        }
    }
    
    ///@todo the 1024,0 elements should come from somewhere.
    socklist->map[i].pagesize   = 1024;
    socklist->map[i].l_type     = 0;
    socklist->map[i].l_socket   = dspath;
    socklist->map[i].websocket  = wspath;
    socklist->size++;
    return 0;
    
    socklist_addmap_TERM:
    free(wspath);
    free(dspath);
    return rc;
}



int socklist_newclient(sockmap_t** newclient, socklist_t* socklist, const char* ws_name) {
    sockmap_t* clisock = NULL;
    int test;
    int newfd = -1;
   
    // Find the socket that matches the websocket mapping
    DEBUG_PRINTF("%s : ws_name = %s\n", __FUNCTION__, ws_name); 
    clisock = socklist_search(socklist, ws_name);
    if (clisock == NULL) {
        goto socklist_newclient_EXIT;
    }
    
    // Test the socket to make sure it is still active.  If it fails, we want
    // to delete the dead socket from the socklist.
    DEBUG_PRINTF("%s : socket found = %s\n", __FUNCTION__, clisock->l_socket); 
    test = sub_testsocket(clisock->l_socket, clisock->l_type);
    if (test != 0) {
        ///@todo delete the dead socket and flag an error.
        clisock = NULL;
        goto socklist_newclient_EXIT;
    }
      
    // Create a client socket of the resolved type.
    ///@todo Right now we only support UNIX sockets.
    DEBUG_PRINTF("%s : socket passed test\n", __FUNCTION__);
    newfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (newfd < 0) {
        goto socklist_newclient_EXIT;
    }
    
    DEBUG_PRINTF("%s : socket opened, fd=%i\n", __FUNCTION__, newfd);   
    socklist_newclient_EXIT:
    if (newclient != NULL) {
        *newclient = clisock;
    }
    return newfd;
}



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
sockmap_t* socklist_search(socklist_t* socklist, const char* ws_name) {
    int i;
//printf("%s : ws_name = %s\n", __FUNCTION__, ws_name);       
    if ((socklist == NULL) || (ws_name == NULL)) {
        return NULL;
    }
    
    for (i=0; i<socklist->size; i++) {
        if (strcmp(socklist->map[i].websocket, ws_name) == 0) {
//printf("%s : socket found\n", __FUNCTION__);   
            return &socklist->map[i];
        }
    }

    return NULL;
}


