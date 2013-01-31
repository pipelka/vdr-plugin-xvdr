#
# Makefile for a Video Disk Recorder plugin
#
# $Id$

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#
PLUGIN = xvdr

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' src/xvdr/xvdr.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

OPTLEVEL ?= 2
CXXFLAGS = -O$(OPTLEVEL) -g -Wall -Woverloaded-virtual -fPIC -DPIC

### The directory environment:

DVBDIR = ../../../../DVB
VDRDIR = ../../..
LIBDIR = ../../lib
TMPDIR = /tmp

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config
-include $(VDRDIR)/Make.global

### The version number of VDR (taken from VDR's "config.h"):

APIVERSION = $(shell grep 'define APIVERSION ' $(VDRDIR)/config.h | awk '{ print $$3 }' | sed -e 's/"//g')

### The name of the distribution archive:

ARCHIVE = vdr-plugin-$(PLUGIN)-$(VERSION)
PACKAGE = $(ARCHIVE)

### Includes and Defines (add further entries here):

INCLUDES += -I$(VDRDIR)/include -I$(DVBDIR)/include -I$(VDRDIR) -I./src -I.

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"' -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -DXVDR_VERSION='"$(VERSION)"'
ifeq ($(DEBUG),1)
  DEFINES += -DDEBUG=1
endif
ifeq ($(CONSOLEDEBUG),1)
  DEFINES += -DCONSOLEDEBUG=1
endif

### The object files (add further files here):

OBJS = \
	src/config/config.o \
	src/demuxer/demuxer.o \
	src/demuxer/demuxer_ADTS.o \
	src/demuxer/demuxer_LATM.o \
	src/demuxer/demuxer_AC3.o \
	src/demuxer/demuxer_EAC3.o \
	src/demuxer/demuxer_H264.o \
	src/demuxer/demuxer_MPEGAudio.o \
	src/demuxer/demuxer_MPEGVideo.o \
	src/demuxer/demuxer_PES.o \
	src/demuxer/demuxer_Subtitle.o \
	src/demuxer/parser.o \
	src/demuxer/streaminfo.o \
	src/live/channelcache.o \
	src/live/livepatfilter.o \
	src/live/livequeue.o \
	src/live/livestreamer.o \
	src/net/msgpacket.o \
	src/net/os-config.o \
	src/net/socketlock.o \
	src/recordings/recordingscache.o \
	src/recordings/recplayer.o \
	src/scanner/wirbelscan.o \
	src/tools/hash.o \
	src/xvdr/xvdr.o \
	src/xvdr/xvdrclient.o \
	src/xvdr/xvdrserver.o \
	src/xvdr/xvdrchannels.o

### Implicit rules:

all-redirect: all

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) -o $@ $<

# Dependencies:

MAKEDEP = g++ -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Targets:

all: libvdr-$(PLUGIN).so

libvdr-$(PLUGIN).so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) -o $@ -lz
	@cp $@ $(LIBDIR)/$@.$(APIVERSION)

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@-rm -f $(TMPDIR)/$(ARCHIVE)/*.so.*
	@-rm -f $(TMPDIR)/$(ARCHIVE)/*.so
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/debian/vdr-plugin-$(PLUGIN)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/debian/*.log
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/debian/*.substvars
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/debian/.gitignore
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/.git*
	@tar czf $(PACKAGE).tar.gz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/
	@echo Distribution package created as $(PACKAGE).tar.gz

clean:
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~

install:
	@install -d ../../man
	@install README ../../man/$(PLUGIN).man
