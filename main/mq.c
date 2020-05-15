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


#include "mq.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>


mq_msg_t* msg_new(size_t len) {
    mq_msg_t* msg = NULL;
    
    msg = malloc(sizeof(mq_msg_t));
    if (msg != NULL) {
        msg->size   = len;
        msg->data   = malloc(sizeof(len) + 0);  ///@todo maybe add LWS overhead ???
        if (msg->data == NULL) {
            free(msg);
            msg = NULL;
        }
    }
    
    return msg;
}


void msg_free(mq_msg_t* msg) {
    if (msg != NULL) {
        free(msg->data);
        free(msg);
    }
}


void mq_init(mq_t* mq) {
    STAILQ_INIT(mq);
}


bool mq_isempty(mq_t* mq) {
    return (bool)STAILQ_EMPTY(mq);
}


mq_msg_t* mq_getmsg(mq_t* mq) {
    mq_msg_t* msg;
    assert(mq);
    
    if (!STAILQ_EMPTY(mq)) {
        msg = STAILQ_FIRST(mq);
        STAILQ_REMOVE_HEAD(mq, entries);
    }
    else {
        msg = NULL;
    }
    
    return msg;
}


void mq_putmsg(mq_t* mq, mq_msg_t* msg) {
    assert(mq);
    assert(msg);
    STAILQ_INSERT_TAIL(mq, msg, entries);
}


