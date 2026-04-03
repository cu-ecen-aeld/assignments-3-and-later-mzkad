#!/bin/sh

if [ $# -lt 2 ]
then
	echo "some parameters were not specified in the command line"
	exit 1
fi

writefile=$1
writestr=$2

filesdir=$(dirname ${writefile})
if [ ! -d ${filesdir} ]
then
	mkdir -p ${filesdir}
fi

echo ${writestr} > ${writefile}

if [ ! $? -eq 0 ]
then
        echo "Unable to create file ${writefile}. Exiting..."
        exit 1
fi




