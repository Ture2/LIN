#!/bin/bash

#El limite de elementos que tiene mi buffer es de los primeros 35 numeros empezando por el 0
for i in {0..100}
do
	echo "Introduciendo $i"
	echo add $i > /proc/modlist_SMP-safe
	echo "--------------"
	sleep 1
	#echo add $i+2 > /proc/modlist_SMP-safe
done 
