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


#include "msgq.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>


msgq_entry_t* msg_new(size_t len) {
    msgq_entry_t* msg = NULL;
    
    msg = malloc(sizeof(msgq_entry_t));
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


void msg_free(msgq_entry_t* msg) {
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


msgq_entry_t* mq_getmsg(mq_t* mq) {
    msgq_entry_t* msg;
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


void mq_putmsg(mq_t* mq, msgq_entry_t* msg) {
    assert(mq);
    assert(msg);
    STAILQ_INSERT_TAIL(mq, msg, entries);
}


