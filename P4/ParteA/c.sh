#!/bin/bash


for i in {0..40}
do

	echo remove $i > /proc/modlist_SMP-safe
	echo "Lista:"
	cat /proc/modlist_SMP-safe
	sleep 2
	echo "---------------------"

done 









