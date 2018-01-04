#!/bin/sh

cd `dirname $0`

source ../util.sh

program_name=`cat ../program_main_name.conf`
reload_cfg $program_name
exit_code="$?"
exit $exit_code
