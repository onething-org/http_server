#!/bin/sh

if [ "$#" -ge 3 ]
then
	passwd="$3"
else
	passwd="green@dog;;"
fi

if [ "$#" -ge 4 ]
then
	ignore_error="$4"
else
	ignore_error="0"
fi

while read ip
do
	./ersh.exp "$1" root "$passwd" $ip
	exitcode="$?"

	if [ "$ignore_error" -ne 0 ]
	then
		continue
	fi

	if [ "$exitcode" -ne 0 ]
	then
		exit $exitcode
	fi
done < $2
