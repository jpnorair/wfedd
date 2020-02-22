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

#ifndef backend_h
#define backend_h

// Local Headers
#include "formatters.h"


// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>




// MPipe Data Type(s)
typedef void* backend_handle_t;

typedef enum {
    DS_ASSISTNOW = 0,
    DS_MAX = 1
} backend_ds_t;







// These files are generally opened via fmemopen(), which yields no remnant
// in the filesystem
typedef struct {
    pthread_mutex_t data_mutex;
    
    pthread_cond_t  croncond;
    pthread_mutex_t cronmutex;
    bool            croncond_pred;
    
    pthread_cond_t  readycond;
    pthread_mutex_t readymutex;
    int             readycond_cnt;
    
    double          client_lon;
    double          client_lat;
    
    assistnow_t     assistnow;
    
    cron_expr       cronexp;
    time_t          cronbasis;
    
} backend_ctx_t;




int backend_init(backend_handle_t* handle, const char* cronstr);

void backend_deinit(backend_handle_t handle);



// Thread Functions
///@todo move into mpipe_io section
void* agnss_reader(void* args);







#endif
