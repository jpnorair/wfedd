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

#include <libwebsockets.h>
#include <string.h>
#include <signal.h>

// Local Libraries
#include <argtable3.h>
//#include <cJSON.h>

// Local to this project
#include "wfedd_cfg.h"
#include "cliopt.h"
#include "frontend.h"
#include "backend.h"
#include "debug.h"


static cliopt_t cliopts;


/// wfedd() is the main process.  
/// main() just validates command line inputs and invokes wfedd()
int wfedd(const char* rsrcpath, const char* urlpath, int port, bool use_tls, socklist_t* socklist);




/// main() just processes and validates command line inputs.
int main(int argc, char* argv[]) {

    // ArgTable params: These define the input argument behavior
#   define FILL_FILEARG(ARGITEM, VAR)   do { \
        size_t str_sz = strlen(ARGITEM->filename[0]) + 1;   \
        if (VAR != NULL) free(VAR);                         \
        VAR = malloc(str_sz);                               \
        if (VAR == NULL) goto main_FINISH;                  \
        memcpy(VAR, ARGITEM->filename[0], str_sz);          \
    } while(0);
#   define FILL_STRINGARG(ARGITEM, VAR)   do { \
        size_t str_sz = strlen(ARGITEM->sval[0]) + 1;   \
        if (VAR != NULL) free(VAR);                         \
        VAR = malloc(str_sz);                               \
        if (VAR == NULL) goto main_FINISH;                  \
        memcpy(VAR, ARGITEM->sval[0], str_sz);          \
    } while(0);

    // Generic
    struct arg_lit  *verbose = arg_lit0("v","verbose",                  "Use verbose mode");
    struct arg_lit  *debug   = arg_lit0("d","debug",                    "Set debug mode on (requires compiling for debug)");
    struct arg_lit  *quiet   = arg_lit0("q","quiet",                    "Supress reporting of errors");
    struct arg_lit  *help    = arg_lit0(NULL,"help",                    "Print this help and exit");
    struct arg_lit  *version = arg_lit0(NULL,"version",                 "Print version information and exit");
    // wfedd-specific
    struct arg_str  *rsrc    = arg_str0("R","resources","path",         "Path for HTTP(S) webserver resources.");
    struct arg_str  *urlpath = arg_str0("U","urlpath","path",           "Additional path addressing in Web Front End URL");
    struct arg_int  *port    = arg_int0("P","port","number",            "HTTP server port (default 7681)");
    struct arg_lit  *tls     = arg_lit0("s","tls",                      "Use TLS (HTTPS)");
    struct arg_str  *socket  = arg_strn("S","socket","path", 1,255,     "Daemon Socket");
    // Terminator
    struct arg_end  *end    = arg_end(20);
    
    void* argtable[] = { verbose, debug, quiet, help, version, rsrc, urlpath, port, tls, socket, end };
    const char* progname = WFEDD_PARAM(NAME);
    
    int nerrors;
    bool bailout        = true;
    int exitcode        = 0;
    
    ///@todo these settings aren't used yet
    //FORMAT_Type fmt_val = FORMAT_Default;
    //INTF_Type intf_val  = INTF_interactive;
    
    bool verbose_val    = false;
    bool debug_val      = false;
    bool quiet_val      = false;
    char* rsrc_val      = NULL;
    char* urlpath_val   = NULL;
    int port_val        = 7681;
    bool tls_val        = false;
    
    socklist_t socklist = {
        .size = 0,
        .map = NULL,
    };

    if (arg_nullcheck(argtable) != 0) {
        /// NULL entries were detected, some allocations must have failed 
        fprintf(stderr, "%s: insufficient memory\n", progname);
        exitcode = 1;
        goto main_FINISH;
    }

    /// Parse the command line as defined by argtable[] 
    nerrors = arg_parse(argc, argv, argtable);

    /// special case: '--help' takes precedence over error reporting
    if (help->count > 0) {
        printf("Usage: %s", progname);
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        exitcode = 0;
        goto main_FINISH;
    }

    /// special case: '--version' takes precedence error reporting 
    if (version->count > 0) {
        printf("%s -- %s\n", WFEDD_PARAM_VERSION, WFEDD_PARAM_DATE);
        printf("Commit-ID: %s\n", WFEDD_PARAM_GITHEAD);
        printf("Designed by %s\n", WFEDD_PARAM_BYLINE);
        printf("Based on wfedd by JP Norair (indigresso.com)\n");
        exitcode = 0;
        goto main_FINISH;
    }

    /// If the parser returned any errors then display them and exit
    /// - Display the error details contained in the arg_end struct.
    if (nerrors > 0) {
        arg_print_errors(stdout,end,progname);
        printf("Try '%s --help' for more information.\n", progname);
        exitcode = 1;
        goto main_FINISH;
    }

    /// special case: with no command line options induces brief help 
    if (argc==1) {
        printf("Try '%s --help' for more information.\n",progname);
        exitcode = 0;
        goto main_FINISH;
    }

    /// Handle output arguments (verbose, debug, quiet)
    if (verbose->count != 0) {
        verbose_val = true;
    }
    if (quiet->count != 0) {
        quiet_val   = true;
        verbose_val = false;
    }
    if (debug->count != 0) {
        debug_val = true;
        quiet_val = false;
    }

    /// Handle webserver arguments
    if (tls->count > 0) {
        tls_val = true;
    }
    if (rsrc->count > 0) {
        ///@todo test that rsrc->sval[0] is to a real directory
        ///@todo test that rsrc dir contains a directory called "mount-origin"
        ///@todo test that rsrc dir contains cert & key if tls_val == true
        FILL_STRINGARG(rsrc, rsrc_val);
    }
    else {
        rsrc_val = malloc(sizeof("./resources") + 1);                               \
        if (rsrc_val == NULL) {
            goto main_FINISH;
        }
        strcpy(rsrc_val, "./resources");
    }
    if (urlpath->count > 0) {
        ///@todo test that urlpath->sval[0] is valid according to URL path rules
        FILL_STRINGARG(urlpath, urlpath_val);
    }
    if (port->count > 0) {
        if ((port->ival[0] >= 0) || (port->ival[0] >= 65536)) {
            printf("Error: Supplied port is out of acceptable range (1-65535)\n");
        }
        ///@todo Add a test to make sure port isn't already in use
        else {
            port_val = port->ival[0];
        }
    }

    /// Handle Socket arguments
    if (socket->count <= 0) {
        printf("Input must contain socket specification argument.\n");
        exitcode = 1;
        goto main_FINISH;
    }

    socklist.size   = socket->count;
    socklist.map    = calloc(socket->count, sizeof(sockmap_t));
    if (socklist.map == NULL) {
        exitcode = 2;
        goto main_FINISH;
    }
    
    for (int i=0; i<socket->count; i++) {
        const char* sep;
        const char* end;
        int strsize;
        
        // 1. look for ':' which is path separator
        end = strchr(socket->sval[i], 0);
        sep = strchr(socket->sval[i], ':');
        if ((sep == NULL) || (end == NULL)) {
            printf("Error: socket input \"%s\" is not correctly formatted.\n", socket->sval[i]);
            exitcode = 3;
            continue;
        }
        
        // 2. store the local socket
        ///@todo check path is valid path
        ///@todo have some way of type checking the type of the socket
        ///@todo make pagesize configurable
        strsize = (int)(sep - socket->sval[i]);
        if (strsize <= 0) {
            exitcode = 4;
            goto main_FINISH;
        }
        socklist.map[i].pagesize    = 128;
        socklist.map[i].l_type      = 0;
        socklist.map[i].l_socket    = malloc( (strsize+1) * sizeof(char) );
        if (socklist.map[i].l_socket == NULL) {
            exitcode = 5;
            goto main_FINISH;
        }
        socklist.map[i].l_socket[strsize] = 0;
        memcpy(socklist.map[i].l_socket, socket->sval[i], strsize);
        
        // 3. store the websocket
        ///@todo check path is valid path
        sep++;
        strsize = (int)(end - sep);
        if (strsize <= 0) {
            exitcode = 6;
            goto main_FINISH;
        }
        socklist.map[i].websocket   = malloc( (strsize+1) * sizeof(char) );
        if (socklist.map[i].websocket == NULL) {
            exitcode = 7;
            goto main_FINISH;
        }
        socklist.map[i].websocket[strsize] = 0;
        memcpy(socklist.map[i].websocket, socket->sval[i], strsize);
    }
    
    if (exitcode != 0) {
        goto main_FINISH;
    }

    /// Client Options.  These are read-only from internal modules
    //cliopts.intf        = intf_val;
    //cliopts.format      = fmt_val;
    cliopts.verbose_on  = verbose_val;
    cliopts.debug_on    = debug_val;
    cliopts.quiet_on    = quiet_val;
    cliopt_init(&cliopts);

    /// All configuration is done.
    /// Send all configuration data to program main function.
    bailout = false;
    
    /// Final value checks
    main_FINISH:

    /// Free un-necessary resources
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    
    /// Run wfedd if no issues
    if (bailout == false) {
        exitcode = wfedd(   (const char*)rsrc_val, 
                            (const char*)urlpath_val, 
                            port_val, tls_val, 
                            &socklist
                        );
    }
    
    /// Free allocated data
    for (int i=0; i<socklist.size; i++) {
        free(socklist.map[i].l_socket);
        free(socklist.map[i].websocket);
    }
    free(socklist.map);
    free(rsrc_val);
    free(urlpath_val);

    return exitcode;
}




int wfedd(  const char* rsrcpath, 
            const char* urlpath, 
            int port, 
            bool use_tls,
            socklist_t* socklist 
        ) {
        
    //void* backend_handle;
    //void* frontend_handle;
    
    char* certpath;
    char* keypath;
    int cursor;
    int logs_mask;
    int exitcode = 0;
    int i, j;
    
    struct lws_protocols* protocols;
    char* str_mountorigin;
    const char* protocol_http = "http";
    const char* hostname = "localhost";
    
    struct lws_http_mount mount = {
        .mount_next             = NULL,             // linked-list "next" 
        .mountpoint             = urlpath,          // e.g. "/"
        .origin                 = NULL,             // allocated later (e.g. "./resources/mount-origin")
        .def                    = "index.html",     // default filename
        .protocol               = NULL,
        .cgienv                 = NULL,
        .extra_mimetypes        = NULL,
        .interpret              = NULL,
        .cgi_timeout            = 0,
        .cache_max_age          = 0,
        .auth_mask              = 0,
        .cache_reusable         = 0,
        .cache_revalidate       = 0,
        .cache_intermediaries   = 0,
        .origin_protocol        = LWSMPRO_FILE,     // files in a dir
        .mountpoint_len         = strlen(urlpath),  // char count
        .basic_auth_login_file  = NULL,
    };

    ///1. Create the protocol list array
    protocols = calloc(2+socklist->size, sizeof(struct lws_protocols));
    if (protocols == NULL) {
        return -1;
    }
    
    /// The "Protocol-0" is HTTP.  
    // HTTP server protocol, first in the list
    protocols[0].name                   = protocol_http;
    protocols[0].callback               = frontend_http_callback;
    protocols[0].per_session_data_size  = 0;
    protocols[0].rx_buffer_size         = 0;
    protocols[0].id                     = 0;
    protocols[0].user                   = NULL; ///@todo maybe needs to be backend_handle
    protocols[0].tx_packet_size         = 0;
    
    // Each websocket protocol
    ///@todo some of these parameters may be changed
    for (i=0, j=1; i<socklist->size; i++, j++) {
        protocols[j].name                   = socklist->map[i].websocket;
        protocols[j].callback               = frontend_ws_callback;
        protocols[j].per_session_data_size  = sizeof(struct per_session_data);
        protocols[j].rx_buffer_size         = 128;
        protocols[j].id                     = 0;
        protocols[j].user                   = NULL; ///@todo maybe needs to be backend_handle
        protocols[j].tx_packet_size         = 0;
    }
    
    // Terminator
    bzero(&protocols[j], sizeof(struct lws_protocols));
//    protocols[j].name                   = NULL;
//    protocols[j].callback               = NULL;
//    protocols[j].per_session_data_size  = 0;
//    protocols[j].rx_buffer_size         = 0;
//    protocols[j].id                     = 0;
//    protocols[j].user                   = NULL;
//    protocols[j].tx_packet_size         = 0;
    
    
    ///2. Create the http mount.  This is based largely on the defined template
    ///@todo allocate and write .mountpoint and .origin in wfedd() function
    if (use_tls) {
        cursor = asprintf(&certpath, "%s/localhost-100y.cert", rsrcpath);
        if (cursor > 0) {
            exitcode = 1;
            goto wfedd_FINISH;
        }
        cursor = asprintf(&keypath, "%s/localhost-100y.key", rsrcpath);
        if (cursor > 0) {
            exitcode = 2;
            goto wfedd_FINISH;
        }
    }
    
    if (asprintf(&str_mountorigin, "%s/mount-origin", rsrcpath) < 0) {
        exitcode = 3;
        goto wfedd_FINISH;
    }
    mount.origin = (const char*)str_mountorigin;
    
    /// Startup Message: just printed to console and not saved
    printf("Starting wfedd on:\n");
    printf(" * mount:%s/mount-origin\n", rsrcpath);
    printf(" * %s://localhost:%i/%s\n", use_tls ? "http" : "https", port, urlpath);
    if (use_tls) {
        printf(" * %s\n", certpath);
        printf(" * %s\n", keypath);
    }

    ///3. Run the polling subsystem (backend).
    ///   This will also invoke the frontend parts, which is the libwebsockets element.
    // Logs configuration (to stdout)
    logs_mask = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
    /* for LLL_ verbosity above NOTICE to be built into lws,
     * lws must have been configured and built with
     * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
    /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
    /* | LLL_EXT */ /* | LLL_CLIENT */ /* | LLL_LATENCY */
    /* | LLL_DEBUG */;
    backend_run(socklist,
                SIGINT,
                logs_mask, 
                false,  ///@todo -h argument from demo app (do_hostcheck)
                false,  ///@todo -v argument from demo app (do_fastmonitoring)
                hostname,
                port, 
                use_tls ? certpath : NULL,
                use_tls ? keypath : NULL,
                protocols,
                &mount      );
    
    wfedd_FINISH:
    switch (exitcode) {
       default: 
        case 4: free(str_mountorigin);
        case 3: free(keypath);
        case 2: free(certpath);
        case 1: free(protocols);
                break;
    }
    
    return 0;
}


