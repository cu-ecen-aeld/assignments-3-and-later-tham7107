#!/bin/sh

######################################################################
##
## Thomas Ames
## ECEA 5305, assignment #1, finder.sh
## July 2023
##

# $# is the # of args not including the filename ($0)
if [ $# -ne 2 ]; then
    echo "Usage: $0 filesdir searchstr"
    echo "Where filesdir is the directory of files to search, and searchstr"
    echo "is the string to search for in files within filesdir."
    exit 1
fi

filesdir=$1
searchstr=$2

# Check that dir exists
if [ ! -d $filesdir ]; then
    echo "Error: $filesdir is not a directory or does not exist"
    exit 1
fi

# Search in dir and all subdirs; use find for recursion
filecount=`find $filesdir -type f | wc -l`
matchcount=`find $filesdir -type f -exec grep $searchstr {} \; | wc -l`

echo The number of files are $filecount and the number of matching lines are $matchcount
exit 0
