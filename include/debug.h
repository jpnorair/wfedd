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

#ifndef debug_h
#define debug_h

#include "cliopt.h"
#include "wfedd_cfg.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>


/// Set __DEBUG__ during compilation to enable debug features (mainly printing)

#define _HEX_(HEX, SIZE, ...)  do { \
    fprintf(stderr, _E_YEL"DEBUG: "_E_NRM __VA_ARGS__); \
    for (int i=0; i<(SIZE); i++) {   \
        fprintf(stderr, "%02X ", (HEX)[i]);   \
    } \
    fprintf(stderr, "\n"); \
} while (0)


#if defined(__DEBUG__)
#   define DEBUG_RUN(CODE)      do { CODE } while(0)
#   define DEBUG_PRINTF(...)    do { if (cliopt_isdebug()) {fprintf(stderr, _E_YEL"DEBUG: " __VA_ARGS__); fprintf(stderr, _E_NRM);}} while(0)
#   define HEX_DUMP(HEX, SIZE, ...) do { if (cliopt_isdebug()) { _HEX_(HEX, SIZE, __VA_ARGS__); } } while(0)

#else
//#   define DEBUG_PRINTF(...)    do { if (cliopt_isdebug()) fprintf(stderr, "DEBUG: " __VA_ARGS__); } while(0)
#   define DEBUG_PRINTF(...)    do { } while(0)
#   define TTY_PRINTF(...)      do { } while(0)
#   define TTY_TX_PRINTF(...)   do { } while(0)
#   define TTY_RX_PRINTF(...)   do { } while(0)
#   define HEX_DUMP(HEX, SIZE, ...) do { if (cliopt_isdebug()) { _HEX_(HEX, SIZE, __VA_ARGS__); } } while(0)

#endif




#endif /* debug_h */
