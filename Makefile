CC := gcc
LD := ld

THISMACHINE ?= $(shell uname -srm | sed -e 's/ /-/g')
THISSYSTEM	?= $(shell uname -s)

VERSION     ?= 0.1.0

APP         := ws-server
SRCDIR      := .
INCDIR      := .
BUILDDIR    := build/$(THISMACHINE)
TARGETDIR   := bin/$(THISMACHINE)
RESDIR      := 
SRCEXT      := c
DEPEXT      := d
OBJEXT      := o

ABSPATH     := $(shell pwd)
CFLAGS      ?= -std=gnu99 -O3 -fPIC
LIBINC      := -L/usr/local/opt/openssl@1.1/lib 

LIB         := -lssl -lcrypto -lev -luv -lglib-2.0 -lz -lc $(EXT_LIB) -L$(ABSPATH)/lws/lib -lwebsockets
INC         := -I/usr/local/opt/openssl@1.1/include -I$(ABSPATH)/lws/include $(EXT_INC) 
INCDEP      := -I/usr/local/opt/openssl@1.1/include -I$(ABSPATH)/lws/include -I$(INCDIR) $(EXT_INC) 
#LIB         := -lssl -lcrypto -lev -luv -lglib-2.0 -lz -lc -lwebsockets
#INC         := -I/usr/local/opt/openssl@1.1/include -I/usr/local/include $(EXT_INC) 
#INCDEP      := -I/usr/local/opt/openssl@1.1/include -I/usr/local/include -I$(INCDIR) $(EXT_INC) 

#SOURCES     := $(shell find $(SRCDIR) -type f -name "*.$(SRCEXT)")
SOURCES     := $(shell ls $(SRCDIR)/*.$(SRCEXT))
OBJECTS     := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))



all: resources $(APP)
remake: cleaner all


#Make the Directories
resources:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)

#Clean only Objects
clean:
	@$(RM) -rf $(BUILDDIR)

#Full Clean, Objects and Binaries
cleaner: clean
	@$(RM) -rf $(TARGETDIR)

#Pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))
	
$(APP): $(OBJECTS)
	$(eval OBJECTS := $(shell find $(BUILDDIR) -type f -name "*.$(OBJEXT)"))
	$(CC) $(CFLAGS) $(INC) $(LIBINC) -o $(TARGETDIR)/$(APP) $(OBJECTS) $(LIB)

#Compile
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<
	@$(CC) $(CFLAGS) $(INCDEP) -MM $(SRCDIR)/$*.$(SRCEXT) > $(BUILDDIR)/$*.$(DEPEXT)
	@cp -f $(BUILDDIR)/$*.$(DEPEXT) $(BUILDDIR)/$*.$(DEPEXT).tmp
	@sed -e 's|.*:|$(BUILDDIR)/$*.$(OBJEXT):|' < $(BUILDDIR)/$*.$(DEPEXT).tmp > $(BUILDDIR)/$*.$(DEPEXT)
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.$(DEPEXT).tmp | fmt -1 | sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.$(DEPEXT)
	@rm -f $(BUILDDIR)/$*.$(DEPEXT).tmp

#Non-File Targets
.PHONY: all app remake clean cleaner resources


