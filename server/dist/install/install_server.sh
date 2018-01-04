#!/bin/sh

cd `dirname $0`

dir_name="http_server"
main_file_name="http_mcd_run.so"

if [ "$#" -lt 1 ]
then
	echo "Usage:"
	echo "install_server.sh ips [passwd] [job_server_config]"
	exit 1
fi

if [ "$#" -ge 2 ]
then
	job_server_info="$2"
fi
echo $job_server_info;

if [ "$#" -ge 3 ]
then
	passwd="$3"
else
	passwd="green@dog;;"
fi

ipsfile="ips_$$"
cp $1 $ipsfile

default_tar_file="$dir_name.tar"
tar_file=$default_tar_file

compare_file_date()
{
	local file1=$1
	local file2=$2
	local file1_modify=`stat $file1 2>&1 | grep Modify | awk '{print $2" "$3}' | awk -F. '{print $1}'`
	local file2_modify=`stat $file2 2>&1 | grep Modify | awk '{print $2" "$3}' | awk -F. '{print $1}'`

	local t1=`date -d "$file1_modify" +%s`
	local t2=`date -d "$file2_modify" +%s`

	if [ $t1 -gt $t2 ]
	then
		return 1	
	elif [ $t1 -eq $t2 ]
	then
		return 0
	else
		return -1
	fi
}

update_file()
{
    if [ -e "$default_tar_file" ]
    then
	    compare_file_date ../$dir_name/bin/$main_file_name $default_tar_file
	    result=$?
	    if [ $result -eq 1 ]
	    then
	    	tar_file=${default_tar_file}$$
	    	echo "tar $tar_file"
            cd ../
	    	tar -czf install/$tar_file $dir_name
            cd install/
	    fi
    else
        cd ../
        echo "tar $tar_file"
        tar -czf install/$tar_file $dir_name
        cd install/
    fi
}

update_file

./send_file_to_ips.sh "$tar_file" "/tmp/" "$ipsfile" "$passwd" 1 1
exitcode="$?"
if [ "$exitcode" -ne 0 ]
then
	echo "cp file to host is error"
	rm -fr $ipsfile
	exit $exitcode
fi

./exec_cmd_in_ips.sh "cd /tmp/;if [ ! -e '$tar_file' ];then exit 1;fi; tar -xzf $tar_file;sh ./$dir_name/tools/op/install.sh $job_server_info ;rm -fr $tar_file" "$ipsfile" "$passwd" 1 1
exitcode="$?"
if [ "$exitcode" -ne 0 ]
then
	echo "ssh cmd in host is error"
	rm -fr $ipsfile
	exit $exitcode
fi

if [ "$tar_file" != "$default_tar_file" ]
then
	mv $tar_file $default_tar_file
	echo "mv $tar_file $default_tar_file"
fi

rm -fr $ipsfile
