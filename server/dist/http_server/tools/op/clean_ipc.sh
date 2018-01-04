#! /bin/sh

#SHM_ID=`ipcs -m | awk '{if (match($1, /0x6004c77e/)) print $1}'`

cd `dirname $0`

source ../util.sh

Ids="0x6004c772 0x6004c773 0x6004c774  0x4cedda41"

clean_ipc "$Ids"
