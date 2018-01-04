#!/bin/sh

if [ "$#" -ge 4 ]
then
	passwd="$4"
else
	passwd="green@dog;;"
fi

if [ "$#" -ge 5 ]
then
	ignore_error="$5"
else
	ignore_error="0"
fi

while read ip
do
	./ercp.exp "$1" "$2" root "$passwd" "$ip"
	exitcode="$?"

	if [ "$ignore_error" -ne 0 ]
	then
		continue
	fi

	if [ "$exitcode" -ne 0 ]
	then
		exit $exitcode
	fi
done < $3
