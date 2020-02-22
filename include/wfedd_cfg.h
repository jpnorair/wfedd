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
#   define WFEDD_PARAM_BYLINE       "Haystack Technologies, Inc."
#endif
#ifndef WFEDD_PARAM_MMAP_PAGESIZE
#   define WFEDD_PARAM_MMAP_PAGESIZE (128*1024)
#endif


#endif
