#!/bin/sh

cd `dirname $0`
cd ../../log

pid="$$"
yestoday="`date -d '1 day ago' '+%Y-%m-%d'`"
today="`date '+%Y-%m-%d'`"
cat restart_record.log | grep "$yestoday" > yestoday_restart_record_$pid
if [ $? -ne 0 ]
then
        rm yestoday_restart_record_$pid > /dev/null
        exit 100;
fi

tail -n 1 yestoday_restart_record_$pid | awk '{print $1 " " $2}'
if [ $? -ne 0 ]
then
        rm yestoday_restart_record_$pid > /dev/null
        exit 101;
fi

cat restart_record.log | grep -E "$today|$yestoday" > yestoday_restart_record_$pid
cat yestoday_restart_record_$pid > restart_record.log
rm yestoday_restart_record_$pid > /dev/null
