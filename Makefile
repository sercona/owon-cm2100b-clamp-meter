LOCATION=/usr/local
CFLAGS=-O2 -Wall -g
LIBS=

OBJ=cm2100b_cli
default: cm2100b_cli

.c.o:
	${CC} ${CFLAGS} -c $*.c

all: ${OBJ} 

cm2100b_cli: ${OFILES} cm2100b_cli.c 
	${CC} ${CFLAGS} cm2100b_cli.c -o cm2100b_cli ${LIBS}

install: ${OBJ}
	cp cm2100b_cli ${LOCATION}/bin/

clean:
	rm -f *.o *core ${OBJ}
