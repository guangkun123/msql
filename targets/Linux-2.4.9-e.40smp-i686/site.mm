# Generated automatically from site.mm.in by configure.
#
# Site specific configuration
#

CC= gcc
LINK= $(CC)
CPP= gcc -E
RANLIB= ranlib
ARCH= Linux-2.4.9-e.40smp-i686

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

# CC Only flags  
# These flags are not passed to makedepend.  Example use is -pic
CC_ONLY= 

# Directory for pid file
PID_DIR= /var/log

CFLAGS= -g -I$(TOP)/ $(EXTRA_CFLAGS)
LDLIBS= -L$(TOP)/lib $(EXTRA_LIB)


#
# EXPRIMENTAL - Don't play with this stuff
#

NEW_DB=
NEW_DB_INC=
NEW_DB_LIB=

