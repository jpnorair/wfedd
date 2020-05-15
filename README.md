# wfedd

"wfedd" is short for "Web Front-End for Daemons Daemon." It is a POSIX-C daemon that will bridge one or more local sockets to websocket mount-points.  

wfedd uses libwebsockets, and it will create a very minimal webserver instance capable of serving static content.  Said static content is intended to be an HTML/JS front-end that interfaces with the websockets bridged by wfedd, although wfedd is permissively open sourced in case you have some alternative, creative usage in mind.

## Building wfedd

### External Dependencies

There are some external dependencies for wfedd.  All of these are relatively common to Mac and Linux distributions, and virtually all package managers have packages for all of them.  It should be noted that several of these dependencies are, in fact, child dependencies of libwebsockets, so if you have libwebsockets on your system, that's probably enough to be sure that `wfedd` will build and run.

* [libbsd](https://libbsd.freedesktop.org/wiki/) (obviously not needed if your platform is a BSD)
* [libwebsockets](https://libwebsockets.org) _child dependencies below_
  * libcrypto, libssl (from [OpenSSL](https://www.openssl.org) 1.1 or compatible)
  * [glib 2.x](https://developer.gnome.org/glib/)
  * [zlib](https://www.zlib.net)

### HBuilder Dependencies

wfedd is part of the HBuilder Middleware group, so the easiest way to build it is via the `hbgw_middleware` repository.  

1. Install external dependencies.
2. Clone/Download hbgw_middleware repository, and `cd` into it.
3. Do the normal: `make all; sudo make install` 
4. Everything will be installed into a `/opt/` directory tree.  Make sure your `$PATH` has `/opt/bin` in it.

### Building without hbgw_middleware

If you want to build wfedd outside of the hbgw_middleware repository framework, you'll need to clone/download the following HBuilder repositories.  You should have all these repo directories stored flat inside a root directory.

* _hbsys
* argtable
* **wfedd**

From this point:

```
$ cd wfedd
$ make pkg
```

You can find the binary inside `wfedd/bin/.../wfedd`

## Using wfedd

wfedd is a very simple program.  There is no interactivity; you just start it on the command line with command line arguments.

### Command Line Arguments

All of the command line arguments are optional, except for "socket", which is required.  The "socket" argument can be repeated as many times as you have sockets to bridge.

* **--help**: print help and exit (typical)
* **--verbose**: verbose logging information
* **--quiet**: suppress all logging information (overrides verbose)
* **--port, -P**: port of the webserver: default 7681
* **--tls, -s**: use TLS for webserver (HTTPS)
* **--socket, -S**: socket:websocket pair

### Mandatory Argument: Socket List

The only mandatory argument is the socket list argument.  It specifies the local socket (generally a UNIX socket) and the websocket path.  A socket:websocket pair is 1:1 -- in other words, you can't bridge a socket to multiple websockets, and vice versa.

This first command (below) will create a minimal webserver on port 7681, bridging the UNIX domain socket at `/opt/sockets/otdb` to `otdb` path relative to the webserver mount.

```
$ wfedd -S /opt/sockets/otdb:otdb
``` 

This second command (below) will do the same thing as the first command, except it is declaring two sockets and two websockets.

```
$ wfedd -S /opt/sockets/otdb:otdb -S /opt/sockets/otter:otter
``` 


## Version History

### 20 Feb 2020

wfedd is under development