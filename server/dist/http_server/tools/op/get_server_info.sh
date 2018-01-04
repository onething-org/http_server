#!/bin/sh

cd `dirname $0`

source ../util.sh

program_name=`cat ../program_main_name.conf`
get_server_info $program_name print_server_info "../../log/server_info"
