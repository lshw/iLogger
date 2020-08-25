#!/bin/bash

cd `dirname $0`
killall minicom 2>/dev/null
killall avrdude 2>/dev/null
sleep 3
echo please reset the iLogger
sleep 1
avrdude -v -patmega328p -carduino -P/dev/ttyUSB0 -b115200 -D -Uflash:w:iLogger.hex:i
#minicom -D /dev/ttyUSB0 -b 115200
