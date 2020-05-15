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

#ifndef cliopt_h
#define cliopt_h

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INTF_unix  = 0,
    INTF_ip = 1,
    INTF_ubus = 2,
    INTF_dbus = 3,
    INTF_max
} INTF_Type;

typedef enum {
    FORMAT_Default  = 0,
    FORMAT_Json     = 1,
    FORMAT_JsonHex  = 2,
    FORMAT_Bintex   = 3,
    FORMAT_Hex      = 4,
    FORMAT_MAX
} FORMAT_Type;


typedef struct {
    bool        verbose_on;
    bool        debug_on;
    bool        quiet_on;
    FORMAT_Type format;
    INTF_Type   intf;
} cliopt_t;


cliopt_t* cliopt_init(cliopt_t* new_master);

bool cliopt_isverbose(void);
bool cliopt_isdebug(void);
bool cliopt_isquiet(void);

FORMAT_Type cliopt_getformat(void);
INTF_Type cliopt_getintf(void);


#endif /* cliopt_h */
