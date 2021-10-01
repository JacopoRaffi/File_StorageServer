CC 			= gcc
CFLAGS		= -g -Wall 
TARGETS		= server client
INCLUDES    = -I ./includes/api.h
LIB 		= -L ./lib/ lib/libapi.a

.PHONY: all clean cleanall test1 test2F test2var test3

#genera tutti gli eseguibili
all : $(TARGETS)

# $< rappresenta il primo prerequisito (solitamente un file sorgente)
# $@ rappresenta il target che stiamo generando
server : src/server.c
	$(CC) $(CFLAGS) src/server.c -o server -lpthread

client : src/client.c lib/libapi.a
	$(CC) $(CFLAGS) src/client.c -o client $(LIB)

obj/serverFunctions.o : src/serverFunctions.c
	$(CC) -c src/serverFunctions.c -o obj/serverFunctions.o

lib/libapi.a : obj/serverFunctions.o
	ar rvs lib/libapi.a obj/serverFunctions.o

stat :
	chmod +x ./script/statistiche.sh
	./script/statistiche.sh
#elimina gli eseguibili
clean :
	-rm -f $(TARGETS)

#ripulisce tutto
#*~ ripulisce i files residui
cleanall :
	-rm -f $(TARGETS) objs/*.o lib/*.a log.txt *.sk *~

#primo test
test1 : $(TARGETS)
	chmod +x ./script/test1.sh
	./script/test1.sh &

#secondo test FIFO
test2FIFO : $(TARGETS)
	chmod +x ./script/test2FIFO.sh
	./script/test2FIFO.sh &

#secondo test LFU
test2LFU : $(TARGETS)
	chmod +x ./script/test2LFU.sh
	./script/test2LFU.sh &

#terzo test
test3 : $(TARGETS)
	chmod +x ./script/test3.sh
	./script/test3.sh &



