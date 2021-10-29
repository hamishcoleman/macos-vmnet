#
#
#

all: simple

LDFLAGS+=-framework vmnet
LDFLAGS+=-framework SystemConfiguration
LDFLAGS+=-framework CoreFoundation
CFLAGS+=-g
