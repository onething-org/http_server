#!/bin/sh

cd `dirname $0`

program_dir=`cat ../program_dir.conf`

../cron/delete_from_crontab.sh

./stop.sh

cd /usr/local

rm -fr $program_dir
