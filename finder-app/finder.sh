#!/bin/sh

if [ $# -lt 2 ]
then
	echo "some parameters were not specified in the command line"
	exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d ${filesdir} ]
then
	echo "${filesdir} is not a directory. Exiting"
	exit 1
fi


X=$(find ${filesdir} -type f | wc -l)
Y=$(grep -rn ${searchstr} ${filesdir} | wc -l)

echo "The number of files are ${X} and the number of matching lines are ${Y}"


