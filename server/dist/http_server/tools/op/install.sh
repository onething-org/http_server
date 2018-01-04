#!/bin/sh

#step1: put the tar file in /tmp
#step2: tar -xzf tarfile
#step3: cd program_dir/tools/op/
#step4: ./install.sh [job_server_id:job_server_ip:job_server_port[:client_module][|...]]
#step5: rm -fr tar file
#note:  these exact file will be moved to the /usr/local/ by install.sh, so you don't need to delete them

cd `dirname $0`

packet_path="`pwd`/../../../"

program_dir=`cat ../program_dir.conf`
program_names=`cat ../program_names.conf`

cd /usr/local/

touch temp_install_$$
exitcode="$?"
if [ $exitcode -ne 0 ]
then
    echo "Error: /usr/local/ is readonly"
    exit $exitcode
fi
rm -fr temp_install_$$

if [ -e "./$program_dir/tools/op/stop.sh" ]
then
    ./$program_dir/tools/op/stop.sh
fi

echo "$program_names" | while read program_name
do
    program_pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep $program_name | awk '{print $1}'`
    if [ "$program_pid" != "" ]
    then
        kill -9 $program_pid
    fi
done

rm -fr $program_dir
cd $packet_path
mv $program_dir /usr/local/
cd /usr/local/

ip=`/sbin/ifconfig eth1 | grep "inet addr" | awk '{print $2}' | awk -F: '{print $2}'`
if [ "$ip" != "" ]
then
    conf_file=`grep 'bind_ip' ./$program_dir/etc/*.conf 2>/dev/null | awk -F: '{print $1}'`
    if [ "$conf_file" != "" ]
    then
        sed -i "s/bind_ip = .*$/bind_ip = $ip/g" $conf_file
    fi
fi

if [ -e "./$program_dir/etc/config" ]
then
	if [ -n "$1" ]
	then
		job_server_info="js_server_info = $1"
		cat ./$program_dir/etc/config | grep -v 'js_server_info' > ./$program_dir/etc/config_tmp
		echo $job_server_info >> ./$program_dir/etc/config_tmp
		mv ./$program_dir/etc/config_tmp ./$program_dir/etc/config
	fi
fi



./$program_dir/tools/op/clean_ipc.sh
./$program_dir/tools/op/start.sh
./$program_dir/tools/cron/add_to_crontab.sh

#begin of modified by bryantyang 
#rm ./$program_dir/log/restart_flag_file
#end of modified by bryantyang



chmod 644 ./$program_dir/tools/op/install.sh
