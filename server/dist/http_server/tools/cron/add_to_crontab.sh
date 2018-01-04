#!/bin/sh

cd `dirname $0`

source ../util.sh

check_file=`cat ../program_check_file.conf`
check_mcd_restart=`cat ../program_check_mcd_restart.conf`
program_dir=`cat ../program_dir.conf`

add_to_crontab /usr/local/$program_dir/tools/cron/$check_file
add_to_crontab /usr/local/$program_dir/tools/cron/$check_mcd_restart
