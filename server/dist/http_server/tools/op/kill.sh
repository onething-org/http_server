#!/bin/sh

cd `dirname $0`

source ../util.sh

check_file=`cat ../program_check_file.conf`

chmod 600 ../cron/$check_file

program_names=`cat ../program_names.conf`
echo "$program_names" | while read program_name
do
    kill_program $program_name
    exit_code="$?"
    if [ $exit_code -ne 0 ]
    then
        chmod 755 ../cron/$check_file
        exit $exit_code
    fi
done
