#!/bin/bash

./server -cnfg ./Config/config3.txt &
SERVER_PID=$! #pid del processo più recente

(sleep 30; kill -s SIGINT $SERVER_PID)&

echo "Avvio del Server"

sleep 2

# il test si assicura che vi siano sempre almeno 10 client connessi al server
while true
do
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f1.txt -r ./Test/Test3/f1.txt -l ./Test/Test3/f1.txt -u ./Test/Test3/f1.txt -c ./Test/Test3/f1.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f2.txt -r ./Test/Test3/f2.txt -l ./Test/Test3/f2.txt -u ./Test/Test3/f2.txt -c ./Test/Test3/f2.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f3.txt -r ./Test/Test3/f3.txt -l ./Test/Test3/f3.txt -u ./Test/Test3/f3.txt -c ./Test/Test3/f3.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f4.txt -r ./Test/Test3/f4.txt -l ./Test/Test3/f4.txt -u ./Test/Test3/f4.txt -c ./Test/Test3/f4.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f5.txt -r ./Test/Test3/f5.txt -l ./Test/Test3/f5.txt -u ./Test/Test3/f5.txt -c ./Test/Test3/f5.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f6.txt -r ./Test/Test3/f6.txt -l ./Test/Test3/f6.txt -u ./Test/Test3/f6.txt -c ./Test/Test3/f6.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f7.txt -r ./Test/Test3/f7.txt -l ./Test/Test3/f7.txt -u ./Test/Test3/f7.txt -c ./Test/Test3/f7.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f8.txt -r ./Test/Test3/f8.txt -l ./Test/Test3/f8.txt -u ./Test/Test3/f8.txt -c ./Test/Test3/f8.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f9.txt -r ./Test/Test3/f9.txt -l ./Test/Test3/f9.txt -u ./Test/Test3/f9.txt -c ./Test/Test3/f9.txt &
./client -f ./serverSocket.sk -t 0 -W ./Test/Test3/f10.txt -r ./Test/Test3/f10.txt -l ./Test/Test3/f10.txt -u ./Test/Test3/f10.txt -c ./Test/Test3/f10.txt &


done
