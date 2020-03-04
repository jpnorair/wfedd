/* Copyright 2017, JP Norair
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


#include "cliopt.h"


static cliopt_t* master;

cliopt_t* cliopt_init(cliopt_t* new_master) {
    master = new_master;
    
    //master->verbose_on  = false;
    //master->debug_on    = false;
    
    return master;
}

bool cliopt_isverbose(void) {
    return master->verbose_on;
}

bool cliopt_isdebug(void) {
    return master->debug_on;
}

bool cliopt_isquiet(void) {
    return master->quiet_on;
}

FORMAT_Type cliopt_getformat(void) {
    return master->format;
}

INTF_Type cliopt_getintf(void) {
    return master->intf;
}



