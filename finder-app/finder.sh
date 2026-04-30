#!/bin/bash

filesdir=$1
searchstr=$2

if [ $# -ne 2 ]; then
    echo "Usage: finder.sh <filesdir> <searchstr>"
    exit 1
fi

if [ ! -d $filesdir ]; then
    echo "$filesdir is not a directory"
    exit 1
fi


nof_files=$(find $filesdir -type f | wc -l)
nof_matches=$(grep -Ir ${searchstr} ${filesdir} | wc -l)

echo "The number of files are ${nof_files} and the number of matching lines are ${nof_matches}"
