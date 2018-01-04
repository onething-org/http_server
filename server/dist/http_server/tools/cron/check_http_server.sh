#!/bin/sh

cd `dirname $0`

source ../util.sh

program_names=`cat ../program_names_start.conf`
if [ "$program_names" = "" ]
then
    program_names=`cat ../program_names.conf`
fi

cd ../../bin/

echo "$program_names" | while read program_name
do
    check_program $program_name "no_cfg"
done
