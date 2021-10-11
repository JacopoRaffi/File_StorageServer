#!/bin/bash


./server -cnfg ./Config/config2FIFO.txt &
SERVER_PID=$! #pid del processo più recente

# fine parte makefile

echo "ATTESA : Avvio del Server"
sleep 2 #attesa post avvio server

chmod +x ./client

./client -f ./serverSocket.sk -t 200 -W ./Test/Test2/f1.txt -p
./client -f ./serverSocket.sk -t 200 -W ./Test/Test2/f3.txt,./Test/Test2/f4.txt -D ./ShinoJ -p

#invio di sighup al server
kill -s SIGHUP $SERVER_PID
