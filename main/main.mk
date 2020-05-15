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

SUBAPP      := main
WFEDD_PKG   ?=
WFEDD_DEF   ?= 
WFEDD_INC   ?=
WFEDD_LIB   ?= 
WFEDD_OSCFLAGS ?=

ifneq ($(EXT_DEBUG),0)
    ifeq ($(EXT_DEBUG),1)
        CFLAGS  ?= -std=gnu99 -Og -g -Wall $(WFEDD_OSCFLAGS) -pthread -D__DEBUG__
    else
        CFLAGS  ?= -std=gnu99 -O2 -Wall $(WFEDD_OSCFLAGS) -pthread -D__DEBUG__
    endif
else 
    CFLAGS      ?= -std=gnu99 -O3 $(WFEDD_OSCFLAGS) -pthread
endif

BUILDDIR    := ../$(WFEDD_BLD)

SUBAPPDIR   := .
SRCEXT      := c
DEPEXT      := d
OBJEXT      := o
LIB         := $(WFEDD_LIB)
LIBINC      := $(subst -L./,-L./../,$(WFEDD_LIBINC))
INC			:= $(subst -I./,-I./../,$(WFEDD_INC)) -I./../test
INCDEP      := $(INC)

SOURCES     := $(shell find . -type f -name "*.$(SRCEXT)")
OBJECTS     := $(patsubst ./%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))


all: directories $(SUBAPP)
obj: $(OBJECTS)
remake: cleaner all


#Make the Directories
directories:
	@mkdir -p $(SUBAPPDIR)
	@mkdir -p $(BUILDDIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)

#Full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(SUBAPPDIR)

#Pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

#Direct build of the test app with objects
$(SUBAPP): $(OBJECTS)
	$(CC) $(INC) $(LIBINC) -o $(SUBAPPDIR)/$(SUBAPP) $^ $(LIB)

#Compile Stages
$(BUILDDIR)/%.$(OBJEXT): ./%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WFEDD_DEF) $(INC) -c -o $@ $<
	@$(CC) $(CFLAGS) $(WFEDD_DEF) $(INCDEP) -MM ./$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

#Non-File Targets
.PHONY: all obj remake resources clean cleaner

