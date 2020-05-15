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

#ifndef mq_h
#define mq_h

// Standard C & POSIX Libraries
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


///@todo find a way to support queue.h in Linux.  It's a BSD library.
#include <sys/queue.h>


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
