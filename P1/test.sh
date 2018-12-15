#!/bin/sh


#make 

make -f Makefile

#Instalamos modulo

sudo insmod modlist.ko

#Mostramos lista vacia

cat /proc/modlist

#Probando ADD

echo "Añadiendo 10..."
echo add 10 > /proc/modlist

echo "Añadiendo 5..."
echo add 5 > /proc/modlist

echo "Mostrando lista actual: "
cat /proc/modlist

#Probando REMOVE
echo "Añadiendo 10..."
echo add 10 > /proc/modlist

echo "Añadiendo 10..."
echo add 10 > /proc/modlist

echo "Mostrando lista actual: "
cat /proc/modlist

echo "Borrando 10..."
echo remove 10 > /proc/modlist

echo "Mostrando lista actual:"
cat /proc/modlist

#Probando CLEANUP

echo "Añadiendo 5..."
echo add 5 > /proc/modlist

echo "Añadiendo 21..."
echo add 21 > /proc/modlist

echo "Añadiendo 3..."
echo add 3 > /proc/modlist

echo "Mostrando lista actual (vacia):"
cat /proc/modlist


echo "Borrando con cleanup..."
echo cleanup > /proc/modlist

echo "Mostrando lista actual (vacia):"
cat /proc/modlist

#Desistalamos

echo "Desistalando..."
sudo rmmod modlist

#Make clean

make -f Makefile clean

