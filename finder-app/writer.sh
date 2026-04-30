#!/bin/bash

writefile=$1
writestr=$2

if [ "$#" -ne 2 ]; then
    echo "Usage: writer.sh <path> <string>"
    exit 1
fi

writedir=$(dirname $writefile)
if [ ! -d $writedir ]; then
    mkdir -p $writedir
fi

echo $writestr > $writefile
if [ $? -ne 0 ]; then
    echo "Failed to write $writestr to $writefile"
    exit 1
fi
