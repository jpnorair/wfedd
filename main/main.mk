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

