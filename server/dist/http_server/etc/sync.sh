#!/bin/sh

cd `dirname $0`
pid=$$
http_server="10.198.131.41"
mysql -h172.25.10.24 ajs -Nse "select ip from tb_module_infos where module_name = 'AS'" > as_list_$pid

while read line
do
	/usr/local/bin/ajs_client_scp 10.198.131.41 /usr/local/access_server/etc/config $line /usr/local/access_server/etc/config
	if [ $? -ne 0 ]
	then
		echo ajs_client_scp $line error!
	else
		/usr/local/bin/ajs_client_cmd "/usr/local/access_server/tools/op/reload_cfg.sh" $line
	fi
	
done < as_list_$pid
