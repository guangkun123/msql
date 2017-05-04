# Generated automatically from site.mm.in by configure.
#
# Site specific configuration
#

CC= gcc
LINK= $(CC)
CPP= gcc -E
RANLIB= ranlib

YACC= bison -y

SIGLIST= 
DIRENT= -DHAVE_DIRENT_H -DHAVE_DIRENT
MMAP= -DHAVE_MMAP
U_INT= -DHAVE_U_INT
ROOT_EXEC= -DROOT_EXEC
ROOT= root

# Top of Minerva install tree
INST_DIR= /usr/local/Minerva

# Extra libraries if required
EXTRA_LIB=  -lnsl

# Any other CFlags required
EXTRA_CFLAGS= -DHAVE_CONFIG_H

# Directory for pid file
PID_DIR= /var/log

CFLAGS= -g -I$(TOP)/ $(EXTRA_CFLAGS)
LDLIBS= -L$(TOP)/lib $(EXTRA_LIB)

