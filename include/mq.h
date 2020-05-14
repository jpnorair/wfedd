/* Copyright 2014, JP Norair
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

#ifndef mq_h
#define mq_h

// Standard C & POSIX Libraries
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

///@todo find a way to support queue.h in Linux.  It's a BSD library.
#include <sys/queue.h>


typedef struct {
    int     l_type;
    size_t  pagesize;
    char*   l_socket;
    char*   websocket;
} sockmap_t;

typedef struct {
    size_t size;
    sockmap_t* map;
} socklist_t;


struct mq_msg {
    void* data;
    size_t size;
    STAILQ_ENTRY(mq_msg) entries;
};

typedef struct mq_msg mq_msg_t;

typedef STAILQ_HEAD(mq_head, mq_msg) mq_t;




mq_msg_t* msg_new(size_t len);

void msg_free(mq_msg_t* msg);


void mq_init(mq_t* mq);

bool mq_isempty(mq_t* mq);

mq_msg_t* mq_getmsg(mq_t* mq);

void mq_putmsg(mq_t* mq, mq_msg_t* msg);




#endif
