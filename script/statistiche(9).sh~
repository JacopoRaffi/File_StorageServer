#!/bin/bash

cd ./script || exit
tail -n 12 ../log.txt

echo  "Operazioni svolte da ogni thread:"

grep thread ../log.txt| cut -d" " -f 2 | sort -g | uniq | while read t_id
do
    echo -n $t_id": "
    grep -c $t_id ../log.txt
done
