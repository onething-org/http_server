#!/bin/sh

cd `dirname $0`

source ../util.sh

program_names=`cat ../program_names.conf`
echo "$program_names" | while read program_name
do
    get_program_status $program_name
done
