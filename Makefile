# Copyright 2020, JP Norair
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, 
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright 
#    notice, this list of conditions and the following disclaimer in the 
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
# POSSIBILITY OF SUCH DAMAGE.

CC := gcc
LD := ld

THISMACHINE ?= $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	?= $(shell uname -s)

APP         ?= wfedd
PKGDIR      := ../_hbpkg/$(THISMACHINE)
SYSDIR      := ../_hbsys/$(THISMACHINE)
EXT_DEF     ?= 
EXT_INC     ?= 
EXT_LIBFLAGS ?= 
EXT_LIBS    ?= 
VERSION     ?= 0.1.a

# Try to get git HEAD commit value
ifneq ($(INSTALLER_HEAD),)
    GITHEAD := $(INSTALLER_HEAD)
else
    GITHEAD := $(shell git rev-parse --short HEAD)
endif

# Check for debugging build, and set necessary variables
ifeq ($(MAKECMDGOALS),debug)
	APPDIR      := bin/$(THISMACHINE)
	BUILDDIR    := build/$(THISMACHINE)_debug
	DEBUG_MODE  := 1
else
	APPDIR      := bin/$(THISMACHINE)
	BUILDDIR    := build/$(THISMACHINE)
	DEBUG_MODE  := 0
endif

# Make sure the LD_LIBRARY_PATH includes the _hbsys directory
ifneq ($(findstring $(SYSDIR)/lib,$(LD_LIBRARY_PATH)),)
	error "$(SYSDIR)/lib not in LD_LIBRARY_PATH.  Please update your settings to include this."
endif

# Make sure the LD_LIBRARY_PATH includes the _hbsys directory
ifneq ($(findstring $(SYSDIR)/lib,$(LD_LIBRARY_PATH)),)
	error "$(SYSDIR)/lib not in LD_LIBRARY_PATH.  Please update your settings to include this."
endif

# Conditional flags per OS
ifeq ($(THISSYSTEM),Darwin)
	OSCFLAGS := -Wno-nullability-completeness -Wno-expansion-to-defined
	OSLIBINC := -L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib
	OSINC := -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include
	LIBBSD :=
else ifeq ($(THISSYSTEM),Linux)
	OSCFLAGS := 
	OSLIBINC := 
	OSINC := 
	LIBBSD :=
else
	error "Only Darwin and Linux supported at this point"
endif

# Conditional build operations depending on how libwebsockets is built.
# This sections needs some additional work
ABSPATH     := $(shell pwd)
PATH_LIBLWS  := 
#PATH_LIBLWS := -L$(ABSPATH)/lws/lib
PATH_INCLWS	:= 
#PATH_LIBLWS := -I$(ABSPATH)/lws/include
LIBEVUV    := 
#LIBEVUV		:= -lev -luv
LIBSSL		:= -lssl -lcrypto
PATH_LIBSSL := -L/usr/local/opt/openssl@1.1/lib
PATH_INCSSL := -I/usr/local/opt/openssl@1.1/include

# These variables don't need to change unless the build changes
DEFAULT_DEF := -DWFEDD_PARAM_GITHEAD=\"$(GITHEAD)\"
LIBMODULES  := argtable $(EXT_LIBS)
SUBMODULES  := main
SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

# Build flags, derived from above variables
CFLAGS_DEBUG?= -std=gnu99 -Og -g -Wall $(OSCFLAGS) -pthread
CFLAGS      ?= -std=gnu99 -O3 $(OSCFLAGS) -pthread
INC         := $(EXT_INC) $(PATH_INCLWS) $(PATH_INCSSL) -I. -I./include -I./$(SYSDIR)/include -I/usr/local/include $(OSINC)
INCDEP      := -I.
LIBINC      := $(EXT_LIB) $(PATH_LIBLWS) $(PATH_LIBSSL) -L./$(SYSDIR)/lib -L/usr/local/lib $(OSLIBINC)
LIB         := $(LIBSSL) $(LIBEVUV) -lglib-2.0 -lz -lc -lwebsockets -largtable

# Export to local and subordinate makefiles
WFEDD_OSCFLAGS:= $(OSCFLAGS)
WFEDD_PKG   := $(PKGDIR)
WFEDD_DEF   := $(DEFAULT_DEF) $(EXT_DEF)
WFEDD_INC   := $(INC) $(EXT_INC)
WFEDD_LIBINC:= $(LIBINC)
WFEDD_LIB   := $(EXT_LIBFLAGS) $(LIB)
WFEDD_BLD   := $(BUILDDIR)
WFEDD_APP   := $(APPDIR)
export WFEDD_OSCFLAGS
export WFEDD_PKG
export WFEDD_DEF
export WFEDD_LIBINC
export WFEDD_INC
export WFEDD_LIB
export WFEDD_BLD
export WFEDD_APP

deps: $(LIBMODULES)
all: release
release: directories $(APP)
debug: directories $(APP).debug
obj: $(SUBMODULES)
pkg: deps all install
remake: cleaner all

install: 
	@rm -rf $(PKGDIR)/$(APP).$(VERSION)
	@mkdir -p $(PKGDIR)/$(APP).$(VERSION)
	@cp $(APPDIR)/$(APP) $(PKGDIR)/$(APP).$(VERSION)/
	@rm -f $(PKGDIR)/$(APP)
	@ln -s $(APP).$(VERSION) ./$(PKGDIR)/$(APP)
	cd ../_hbsys && $(MAKE) sys_install INS_MACHINE=$(THISMACHINE) INS_PKGNAME=$(APP)

directories:
	@mkdir -p $(APPDIR)
	@mkdir -p $(BUILDDIR)

# Clean only this machine
clean:
	@$(RM) -rf $(BUILDDIR)
	@$(RM) -rf $(APPDIR)

# Clean all builds
cleaner: 
	@$(RM) -rf ./build
	@$(RM) -rf ./bin

#Linker
$(APP): $(SUBMODULES) 
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS) $(WFEDD_DEF) $(WFEDD_INC) $(WFEDD_LIBINC) -o $(APPDIR)/$(APP) $(OBJECTS) $(WFEDD_LIB)

$(APP).debug: $(SUBMODULES)
	$(eval OBJECTS_D := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS_DEBUG) $(WFEDD_DEF) -D__DEBUG__ $(WFEDD_INC) $(WFEDD_LIBINC) -o $(APPDIR)/$(APP).debug $(OBJECTS_D) $(WFEDD_LIB)

#Library dependencies (not in wfedd sources)
$(LIBMODULES): %: 
	cd ./../$@ && $(MAKE) pkg


#wfedd submodules
$(SUBMODULES): %: directories
	cd ./$@ && $(MAKE) -f $@.mk obj EXT_DEBUG=$(DEBUG_MODE)

#Non-File Targets
.PHONY: deps all release debug obj pkg remake install directories clean cleaner
