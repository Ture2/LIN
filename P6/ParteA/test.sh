#!/bin/sh

cd ~/pFinal/ParteA

make -f Makefile

echo "Default value for max_entries = 3"
echo "Default value for max_size = 64"
echo "----------------"

sleep 4

echo "Installing module with max_entries = 5 and max_size = 32..."
sleep 4
sudo insmod fifoproc.ko max_entries=5 max_size=32
echo "Done !"

echo "----------------"
echo "Creating 4 fifos to test max_entries"
sudo echo create fifoA > /proc/fifo/control
sudo echo create fifoB > /proc/fifo/control
sudo echo create fifoC > /proc/fifo/control
sudo echo create fifoD > /proc/fifo/control

echo "----------------"
echo "Supose to have one error"
echo "----------------"
echo "Now test manually the fifos"
