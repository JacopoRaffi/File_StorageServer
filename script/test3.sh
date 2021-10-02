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
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f1.txt -r ./Test3/Cartella1/f1.txt -l ./Test3/Cartella1/f1.txt -u ./Test3/Cartella1/f1.txt -c ./Test3/Cartella1/f1.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f2.txt -r ./Test3/Cartella1/f2.txt -l ./Test3/Cartella1/f2.txt -u ./Test3/Cartella1/f2.txt -c ./Test3/Cartella1/f2.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f3.txt -r ./Test3/Cartella1/f3.txt -l ./Test3/Cartella1/f3.txt -u ./Test3/Cartella1/f3.txt -c ./Test3/Cartella1/f3.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f4.txt -r ./Test3/Cartella1/f4.txt -l ./Test3/Cartella1/f4.txt -u ./Test3/Cartella1/f4.txt -c ./Test3/Cartella1/f4.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f1.txt -r ./Test3/Cartella1/f1.txt -l ./Test3/Cartella1/f1.txt -u ./Test3/Cartella1/f1.txt -c ./Test3/Cartella1/f1.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f2.txt -r ./Test3/Cartella1/f2.txt -l ./Test3/Cartella1/f2.txt -u ./Test3/Cartella1/f2.txt -c ./Test3/Cartella1/f2.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f1.txt -r ./Test3/Cartella1/f1.txt -l ./Test3/Cartella1/f1.txt -u ./Test3/Cartella1/f1.txt -c ./Test3/Cartella1/f1.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test3/Cartella1/f2.txt -r ./Test3/Cartella1/f2.txt -l ./Test3/Cartella1/f2.txt -u ./Test3/Cartella1/f2.txt -c ./Test3/Cartella1/f2.txt &

done