#!/bin/bash

valgrind --leak-check=full ./server -cnfg ./Config/config1.txt &
SERVER_PID=$! #pid del processo più recente


echo "Avvio del Server"
sleep 2 #attesa post avvio server

./client -h -p #verifico l'opzione -h

./client -f ./serverSocket.sk -t 200 -w ./Test1/cartella1/sottocartella1 -W ./Test1/cartella1/f1.txt -D ./Test1/cartella1/w_aux_dir1 -r ./Test1/cartella1/sottocartella1/f4.txt,./Test1/cartella1/sottocartella1/f5.txt -d Shinokj -l ./Test1/cartella1/sottocartella1/f4.txt,./Test1/cartella1/f1.txt -u ./Test1/cartella1/sottocartella1/f4.txt -c ./Test1/cartella1/f1.txt ./Test1/cartella1/r_aux_dir1 -R 0 -p

#invio di sighup al server
killall -s SIGHUP memcheck-amd64-
