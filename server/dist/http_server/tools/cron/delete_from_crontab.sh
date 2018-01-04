#!/bin/sh

cd `dirname $0`

source ../util.sh

check_file=`cat ../program_check_file.conf`
program_dir=`cat ../program_dir.conf`

delete_from_crontab /usr/local/$program_dir/tools/cron/$check_file
