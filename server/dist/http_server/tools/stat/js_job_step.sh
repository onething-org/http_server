#!/bin/sh

cd `dirname $0`
cd ../../log

pid="$$"
yestoday="`date -d '1 day ago' '+%Y-%m-%d'`"
today="`date '+%Y-%m-%d'`"

cat job_step_statistic.log | grep "$yestoday" > yestoday_job_step_stat_$pid
if [ $? -ne 0 ]
then
	rm yestoday_job_step_stat_$pid > /dev/null
	exit 100
fi

totalSubmitedJobStep=0
totalFailedJobStep=0
while read line
do
	submitedJobStep="`echo $line | awk -F ' ' '{print $4}'`"
	failedJobStep="`echo $line | awk -F ' ' '{print $5}'`"
	totalSubmitedJobStep=$((totalSubmitedJobStep + submitedJobStep))
	totalFailedJobStep=$((totalFailedJobStep + failedJobStep))
done < yestoday_job_step_stat_$pid
echo $totalSubmitedJobStep $totalFailedJobStep

cat job_step_statistic.log | grep -E "$today|$yestoday" > today_job_step_stat_$pid
cat yestoday_job_step_stat_$pid > job_step_statistic.log
rm yestoday_job_step_stat_$pid > /dev/null
