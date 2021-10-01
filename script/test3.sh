#!/bin/bash

./server -cnfg ./Config/config3.txt &
SERVER_PID=$! #pid del processo più recente
MAIN_PID=$$ #pid di questa shell

(sleep 30; kill $MAIN_PID; kill -s SIGINT $SERVER_PID)&

echo "Avvio del Server"

sleep 2

# il test si assicura che vi siano sempre almeno 10 client connessi al server
while true
do
./client -f ./serverSocket.sk -t 0 -c ./Test3/Cartella1/f10.txt -R 1 -c ./Test3/Cartella1/f7.txt -c ./Test3/Cartella1/f11.txt -c ./Test3/Cartella1/f10.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f10.txt -R 1 -c ./Test3/Cartella1/f7.txt -c ./Test3/Cartella1/f11.txt -c ./Test3/Cartella1/f10.txt &
./client -f ./serverSocket.sk -t 0 -u ./Test3/Cartella1/f1.txt -R 1 -l ./Test3/Cartella1/f1.txt -l ./Test3/Cartella1/f7.txt -c ./Test3/Cartella1/f1.txt &
./client -f ./serverSocket.sk -t 0 -R 2 -r ./Test3/Cartella1/f1.txt -u ./Test3/Cartella1/f2.txt -l ./Test3/Cartella1/f2.txt -c ./Test3/Cartella1/f2.txt &
./client -f ./serverSocket.sk -t 0 -R 1 -W ./Test3/Cartella1/f5.txt -c ./Test3/Cartella1/f5.txt -l ./Test3/Cartella1/f4.txt -c ./Test3/Cartella1/f4.txt &
./client -f ./serverSocket.sk -t 0 -R 1 -W ./Test3/Cartella1/f8.txt -u ./Test3/Cartella1/f7.txt -l ./Test3/Cartella1/f7.txt -c ./Test3/Cartella1/f8.txt &
./client -f ./serverSocket.sk -t 0 -R 1 -r ./Test3/Cartella1/f9.txt -u ./Test3/Cartella1/f9.txt -l ./Test3/Cartella1/f9.txt -c ./Test3/Cartella1/f9.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f3.txt -R 0 -u ./Test3/Cartella1/f3.txt -l ./Test3/Cartella1/f3.txt -c ./Test3/Cartella1/f3.txt

done
