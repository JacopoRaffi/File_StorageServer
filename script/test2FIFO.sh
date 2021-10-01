#!/bin/bash

# inizio parte makefile

./server -cnfg ./Config/config2FIFO.txt &
SERVER_PID=$! #pid del processo più recente

# fine parte makefile

echo "ATTESA : Avvio del Server"
sleep 2 #attesa post avvio server

#il client 1 scriverà 3 files arrivando quasi al massimo della capacità
#il client 2 scriverà un solo file ma di grandi dimensioni -> 3 files verranno rimpiazzati e il server ne conterrà 1

./client -f ./serverSocket.sk -t 200 -w ./Test2/cartella2_a -D ./ShinoJ -p

./client -f ./serverSocket.sk -t 200 -w ./Test2/cartella2_b -D ./ShinoJ -p

#invio di sighup al server
kill -s SIGHUP $SERVER_PID
