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

#ifndef wfedd_cfg_h
#define wfedd_cfg_h

#ifndef ENABLED
#   define ENABLED  1
#endif
#ifndef DISABLED
#   define DISABLED 0
#endif


/// Color codes for terminal output coloring
///@todo move this and printer macros to a debug.h

#define _E_NRM  "\033[0m"

// Normal colors
#define _E_RED  "\033[31m"
#define _E_GRN  "\033[32m"
#define _E_YEL  "\033[33m"
#define _E_BLU  "\033[34m"
#define _E_MAG  "\033[35m"
#define _E_CYN  "\033[36m"
#define _E_WHT  "\033[37m"

// Overlay color on black
#define _E_OBLK  "\033[30;40m"
#define _E_ORED  "\033[31;40m"
#define _E_OGRN  "\033[32;40m"
#define _E_OYEL  "\033[33;40m"
#define _E_OBLU  "\033[34;40m"
#define _E_OMAG  "\033[35;40m"
#define _E_OCYN  "\033[36;40m"
#define _E_OWHT  "\033[37;40m"

// Bright
#define _E_BBLK "\033[1;30;40m"
#define _E_BRED "\033[1;31;40m"
#define _E_BGRN "\033[1;32;40m"
#define _E_BYEL "\033[1;33;40m"
#define _E_BBLU "\033[1;34;40m"
#define _E_BMAG "\033[1;35;40m"
#define _E_BCYN "\033[1;36;40m"
#define _E_BWHT "\033[1;37;40m"

#define ERRMARK                 _E_RED"ERR: "_E_NRM
#define ERR_PRINTF(...)         do { if (cliopt_isverbose()) { fprintf(stdout, _E_RED "ERR: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VERBOSE_PRINTF(...)     do { if (cliopt_isverbose()) { fprintf(stdout, _E_CYN "MSG: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VDATA_PRINTF(...)       do { if (cliopt_isverbose()) { fprintf(stdout, _E_GRN "DATA: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)
#define VCLIENT_PRINTF(...)     do { if (cliopt_isverbose()) { fprintf(stdout, _E_MAG "CLIENT: " _E_NRM __VA_ARGS__); fflush(stdout); }} while(0)







/// Default feature configurations
#define WFEDD_FEATURE(VAL)          WFEDD_FEATURE_##VAL

#ifndef WFEDD_FEATURE_HBUILDER
#   ifdef __HBUILDER__
#   define WFEDD_FEATURE_HBUILDER   ENABLED
#   else
#   define WFEDD_FEATURE_HBUILDER   DISABLED
#   endif
#endif


/// Parameter configuration defaults
#define WFEDD_PARAM(VAL)            WFEDD_PARAM_##VAL
#ifndef WFEDD_PARAM_NAME
#   define WFEDD_PARAM_NAME         "wfedd"
#endif
#ifndef WFEDD_PARAM_VERSION 
#   define WFEDD_PARAM_VERSION      "1.0.a"
#endif
#ifndef WFEDD_PARAM_GITHEAD
#   define WFEDD_PARAM_GITHEAD      "(unknown)"
#endif
#ifndef WFEDD_PARAM_DATE
#   define WFEDD_PARAM_DATE         __DATE__
#endif
#ifndef WFEDD_PARAM_BYLINE
#   define WFEDD_PARAM_BYLINE       "JP Norair (indigresso.com)"
#endif
#ifndef WFEDD_PARAM_MMAP_PAGESIZE
#   define WFEDD_PARAM_MMAP_PAGESIZE (128*1024)
#endif


#endif
