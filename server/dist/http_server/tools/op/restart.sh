#!/bin/sh

cd `dirname $0`

./stop.sh
exit_code="$?"
if [ $exit_code -ne 0 ]
then
    exit $exit_code
fi

./start.sh
exit_code="$?"
if [ $exit_code -ne 0 ]
then
    exit $exit_code
fi
