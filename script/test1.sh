#!/bin/bash

valgrind --leak-check=full ./server -cnfg ./Config/config1.txt &
SERVER_PID=$! #pid del processo più recente


echo "Avvio del Server"
sleep 2 #attesa post avvio server

./client -h -p #verifico l'opzione -h

./client -f ./serverSocket.sk -t 200 -w ./Test/Test1/SubTest1 -W ./Test/Test1/f1.txt -D ./ShinokjjS -r ./Test/Test1/SubTest1/f6.txt, ./Test/Test1/SubTest1/f5.txt -d Shinokj -l ./Test/Test1/SubTest1/f4.txt, ./Test/Test1/SubTest1/f5.txt -u ./Test/Test1/SubTest1/f4.txt, ./Test/Test1/SubTest1/f5.txt -c ./Test/Test1/f1.txt -R 0 -a ./Test/Test1/f1.txt, ciaoSonoPippo -p

#invio di sighup al server
killall -s SIGHUP memcheck-amd64-
