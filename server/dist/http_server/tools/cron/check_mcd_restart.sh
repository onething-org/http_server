#!/bin/sh

cd `dirname $0`

source ../util.sh

program_names=`cat ../program_main_name.conf`

cd ../../bin/

echo "$program_names" | while read program_name
do
    check_mcd_restart $program_name
done
