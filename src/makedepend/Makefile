
SRCS= include.c main.c parse.c pr.c cppsetup.c ifparser.c
OBJS= include.o main.o parse.o pr.o cppsetup.o ifparser.o

CFLAGS= -g -DOBJSUFFIX=\".o\" -DINCLUDEDIR=\"/usr/include\"


all: makedepend

makedepend : $(OBJS)
	$(CC) -o makedepend $(OBJS)

clean :
	rm -f core $(OBJS) makedepend
