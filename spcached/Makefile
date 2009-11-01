
#--------------------------------------------------------------------

CC = gcc
AR = ar cru
CFLAGS = -Wall -D_REENTRANT -D_GNU_SOURCE -g -fPIC
SOFLAGS = -shared
LDFLAGS = -lstdc++

LINKER = $(CC)
LINT = lint -c
RM = /bin/rm -f

ifeq ($(origin version), undefined)
	version = 0.1
endif

SPSERVER_LIB  = -L../spserver/ -lspserver -levent -lrt

SPDICT_LIB  = -L../spdict/ -lspdict

CFLAGS  += -I../
LDFLAGS += $(SPSERVER_LIB) $(SPDICT_LIB) -lpthread -lresolv

#-------------------Cprof related Macros----------------
ifeq ($(cprof), 1)
	CFLAGS += -finstrument-functions
	LDFLAGS += -lcprof
endif

#--------------------------------------------------------------------

TARGET = spcached

#--------------------------------------------------------------------

all: $(TARGET)

spcached: spcachemsg.o spcacheproto.o spcacheimpl.o spcached.o
	$(LINKER) $(LDFLAGS) $^ -o $@

dist: clean spcached-$(version).src.tar.gz

spcached-$(version).src.tar.gz:
	@ls | grep -v CVS | grep -v .so | sed 's:^:spcached-$(version)/:' > MANIFEST
	@(cd ..; ln -s spcached spcached-$(version))
	(cd ..; tar cvf - `cat spcached/MANIFEST` | gzip > spcached/spcached-$(version).src.tar.gz)
	@(cd ..; rm spcached-$(version))

clean:
	@( $(RM) *.o vgcore.* core core.* $(TARGET) )

#--------------------------------------------------------------------

# make rule
%.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@	

%.o : %.cpp
	$(CC) $(CFLAGS) -c $^ -o $@	

