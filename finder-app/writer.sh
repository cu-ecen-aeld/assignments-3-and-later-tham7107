#!/bin/bash

######################################################################
##
## Thomas Ames
## ECEA 5305, assignment #1, writer.sh
## July 2023
##

# $# is the # of args not including the filename ($0)
if [ $# -ne 2 ]; then
    echo "Usage: $0 writefile writestr"
    echo "Create the file writefile containing text writestr."
    echo "Overwrites writefile if already exists."
    echo "Exit code 0 on success, 1 if file cannot be written or bad args"
    exit 1
fi

# For clarity in script, use descriptive variable names
writefile=$1
writestr=$2
destdir=`dirname $writefile`

# Create destination dir if it doesn't exist, exit on error
mkdir -p $destdir

if [ $? -ne 0 ]; then
    echo "Error creating $destdir"
    exit 1
fi

echo $writestr > $writefile

# Check result of echo
if [ $? -ne 0 ]; then
    echo "Error writing to $writefile"
    exit 1
fi

exit 0
