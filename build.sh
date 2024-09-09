#!/bin/bash

cd `dirname $0`

mkdir -p /tmp/build
arduino=/opt/arduino-1.8.19
sketchbook=./iLogger
fqbn=arduino:avr:uno

$arduino/arduino-builder \
-dump-prefs \
-logger=machine \
-hardware "$arduino/hardware" \
-hardware "$HOME/.arduino15/packages" \
-tools "$arduino/tools-builder" \
-tools "$arduino/hardware/tools/avr" \
-tools "$HOME/.arduino15/packages" \
-built-in-libraries "$arduino/libraries" \
-libraries "$HOME/sketchbook/libraries" \
-fqbn=$fqbn \
-ide-version=10819 \
-build-path "/tmp/build" \
-warnings=none \
-prefs=build.warn_data_percentage=75 \
-verbose "$sketchbook/iLogger.ino"

$arduino/arduino-builder \
-compile \
-logger=machine \
-hardware "$arduino/hardware" \
-hardware "$HOME/.arduino15/packages" \
-tools "$arduino/tools-builder" \
-tools "$arduino/hardware/tools/avr" \
-tools "$HOME/.arduino15/packages" \
-built-in-libraries "$arduino/libraries" \
-libraries "$HOME/sketchbook/libraries" \
-fqbn=$fqbn \
-ide-version=10819 \
-build-path "/tmp/build" \
-warnings=none \
-prefs=build.warn_data_percentage=75 \
-verbose "$sketchbook/iLogger.ino"|tee /tmp/info.log

chmod -R og+w /tmp/build
cp -a /tmp/build/iLogger.ino.hex iLogger.hex
grep "Global vari" /tmp/info.log |awk -F[ '{printf $2}'|tr -d ']'|awk -F' ' '{print "内存：使用"$1"字节,"$3"%,剩余:"$4"字节"}'
grep "Sketch uses" /tmp/info.log |awk -F[ '{printf $2}'|tr -d ']'|awk -F' ' '{print "ROM：使用"$1"字节,"$3"%"}'
