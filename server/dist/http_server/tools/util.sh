#!/bin/sh

if [ "$mtnc_util_no" ]
then
    return
fi

ulimit -c unlimited

set_only_source() {
    export mtnc_util_no="util.sh"
}

unset_only_source() {
    unset mtnc_util_no
}

set_only_source

get_program_status() {
    program_name="$1"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
    	echo "$program_name $pid"
    else
    	echo "$program_name is not running now"
    fi
}

start_program() {
    # 去掉上面设置的环境变量,防止被要启动的进程继承
    unset_only_source

    program_name="$1"
    program_cfg="$2"
    if [ "$program_cfg" = "" ]
    then
    	program_cfg="../etc/$program_name.conf"
    fi
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    exit_code="$?"
    if [ $exit_code -ne 0 ]
    then
        echo "ps $program_name was exception"
        return 2
    fi

    if [ "$pid" = "" ]
    then
        if [ "$program_cfg" != "no_cfg" ]
        then
            ./$program_name "$program_cfg"
        else
            ./$program_name
        fi

        #等待进程启动
        count=10
        pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
        while [ "$pid" = "" ] && [ $count -gt 0 ]
        do 
            let count--
            pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
            sleep 1
        done
        
        #检查进程是否启动
        if [ "$pid" != "" ]
        then
        	echo "$program_name is start ok"
        else
        	echo "$program_name is not running in 10s, please check it"
            set_only_source
            return 1
        fi
    else
        echo "$program_name is running now"
    fi

    set_only_source
    return 0
}

stop_program() {
    program_name="$1"
    stop_signal="$2"
    if [ "$stop_signal" = "" ]
    then
        stop_signal=12
    fi
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
    	kill -$stop_signal $pid
    
        #等待进程退出
        count=10
        pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
        while [ "$pid" != "" ] && [ $count -gt 0 ]
        do 
            let count--
            pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
            sleep 1
        done
        
        #检查进程是否退出
        if [ "$pid" = "" ]
        then
        	echo "$program_name is stop ok"
        else
        	echo "$program_name is not exit in 10s, please check it"
            return 2
        fi
    else
        echo "$program_name is not running now"
    fi

    return 0
}

stop_program_use_cfg_need_exit() {
    program_name="$1"
    program_cfg="$2"
    if [ "$program_cfg" = "" ]
    then
    	program_cfg="../../etc/$program_name.conf"
    fi
    need_exit_key="need_exit"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
        sed -i "s/^.*$need_exit_key.*$/$need_exit_key = 1/g" $program_cfg
    
        kill -10 $pid
    
        #等待进程退出
        count=10
        pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
        while [ "$pid" != "" ] && [ $count -gt 0 ]
        do 
            let count--
            pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
            sleep 1
        done
    
        sed -i "s/^.*$need_exit_key = 1.*$/#$need_exit_key = 1/g" $program_cfg
        
        #检查进程是否退出
        if [ "$pid" = "" ]
        then
        	echo "$program_name is stop ok"
        else
        	echo "$program_name is not exit in 10s, please check it"
            return 2
        fi
    else
        echo "$program_name is not running now"
    fi
}

kill_program() {
    program_name="$1"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
    	kill -9 $pid
    
        #等待进程退出
        count=10
        pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
        while [ "$pid" != "" ] && [ $count -gt 0 ]
        do 
            let count--
            pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
            sleep 1
        done
        
        #检查进程是否退出
        if [ "$pid" = "" ]
        then
        	echo "$program_name is kill ok"
        else
        	echo "$program_name is not exit in 10s, please check it"
            return 3
        fi
    else
        echo "$program_name is not running now"
    fi

    return 0
}

get_server_info() {
    program_name="$1"
    server_info_key="$2"
    server_info_file="$3"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
        modify_pre=`stat $server_info_file 2>&1 | grep Modify`
    
        #修改配置文件中的server_info_key为1
        sed -i "s/^.*$server_info_key.*$/$server_info_key = 1/g" ../../etc/config
        
        #发信号
    	kill -10 $pid
        
        #等待server_info_file修改日期发生变化
        count=10
        modify_after=`stat $server_info_file 2>&1 | grep Modify`
        while [ "$modify_pre" = "$modify_after" ] && [ $count -gt 0 ]
        do 
            let count--
            modify_after=`stat $server_info_file 2>&1 | grep Modify`
            sleep 1
        done
    
        #将配置文件中的server_info_key注释掉
        sed -i "s/^.*$server_info_key = 1.*$/#$server_info_key = 1/g" ../../etc/config
    
        #看server_info_file是否已经修改了
        if [ "$modify_pre" != "$modify_after" ]
        then
    	    echo "get server info is ok, please see the file '$server_info_file'"
        else
    	    echo "get server info is failed"
            return 4
        fi
    else
    	echo "$program_name is not running now"
    fi

    return 0
}

modify_server_cfg_and_reload() {
    program_name="$1"
    server_info_key="$2"
    key_new_value="$3"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
        #修改配置文件中的server_info_key为1
        sed -i "s/^.*$server_info_key.*$/$server_info_key = $key_new_value/g" ../../etc/config
        
        #发信号
    	kill -10 $pid
    else
    	echo "$program_name is not running now"
    fi

    return 0
}

reload_cfg() {
    program_name="$1"
    pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
    if [ "$pid" != "" ]
    then
    	kill -10 $pid
    	echo "$program_name is reload ok"
    else
    	echo "$program_name is not running now"
        return 5
    fi
}

check_program() {
    program_name="$1"
    program_cfg="$2"
    if [ "$program_cfg" = "" ]
    then
    	program_cfg="../etc/$program_name.conf"
    fi
    pid=`ps -e -o cmd | awk '{print $1}' | grep -w $program_name | grep -v grep`
    exit_code="$?"
    if [ $exit_code -ne 0 ] && [ $exit_code -ne 1 ]
    then
        echo "ps $program_name was exception"
        return
    fi
    
    if [ "$pid" != "" ]
    then
        echo "$program_name is running now"
    else
        # 去掉上面设置的环境变量,防止被要启动的进程继承
        unset_only_source
        if [ "$program_cfg" != "no_cfg" ]
        then
            ./$program_name "$program_cfg"
        else
            ./$program_name
        fi
        echo "$program_name is start ok"
        set_only_source
    fi
}

#begin of bryantyang
#检查mcd程序是否重启
check_mcd_restart() {
	program_name="$1"
	pid_file="$2";
	restart_flag_file="$3";
	restart_record="$4";
	if [ "$pid_file" = "" ]
	then
		pid_file="../log/mcd_pid_file.pid"
	fi

	if [ "$restart_flag_file" = "" ]
	then
		restart_flag_file="../log/restart_flag_file"
	fi

	if [ "$restart_record" = "" ]
	then
		restart_record="../log/restart_record.log"
	fi

	pre_pid="`tail -n 1 $pid_file`"
	if [ $? -ne 0 ]
	then
		pre_pid=0
	elif [ "$pre_pid" = "" ]
	then
		pre_pid=0
	fi

	#now_pid="`ps -ef| grep -w $program_name | grep -v grep | awk '{print $2}'`";
	now_pid=`ps -e -o pid,cmd | awk '{print $1,$2}' | grep -w $program_name | awk '{print $1}'`
	if [ $pre_pid -eq 0 ]
	then
		echo $now_pid > $pid_file
	elif [ "$now_pid" != "$pre_pid" ]
	then
		#mcd挂了
		local time_now="`date '+%Y-%m-%d %H:%M:%S'`"
		echo $now_pid > $pid_file
		local ccd_conf_filename="`cat ../tools/program_main_name.conf | awk -F '_' '{print $1 "_" $2}'`_ccd.conf"
		local ip="`cat ../etc/$ccd_conf_filename | grep -v '#' | grep 'bind_ip' | awk -F ' ' '{print $3}'`"
		local feature_id="`cat ../tools/restart_alert_feature_id.conf`"
		local module_name=`cat ../tools/program_main_name.conf | awk -F '_' '{print $1 " " $2}'`

		#记录重启信息
		echo "$time_now $ip $module_name $now_pid $pre_pid" >> $restart_record

		#调用部门网管字符串告警
		cd /usr/local/apdtools/agent/reportexception;
		./reportexception reportException.conf $feature_id "$module_name is crashed, ip: $ip"
	fi
}
#end of bryantyang

add_to_crontab() {
    crontab_name="$1"
    tmp_file="util_crontab_tmp$$"
    crontab -l | grep -v "$crontab_name" | grep -v "DO NOT EDIT THIS FILE" | grep -v "vixie Exp" | grep -v "installed on" > $tmp_file
    echo "* * * * * $crontab_name > /dev/null 2>&1" >> $tmp_file
    crontab $tmp_file
    rm -fr $tmp_file
}

delete_from_crontab() {
    crontab_name="$1"
    tmp_file="util_crontab_tmp$$"
    crontab -l | grep -v "$crontab_name" | grep -v "DO NOT EDIT THIS FILE" | grep -v "vixie Exp" | grep -v "installed on" > $tmp_file
    crontab $tmp_file
    rm -fr $tmp_file
}

clean_ipc() {
    Ids="$1"
    for Id in $Ids
    do
        #clean share memory
        SHM_ID=`ipcs -m | grep $Id | grep -v grep`
        if [ ! -z "$SHM_ID" ]
        then
            echo "ipcrm -M $Id"
            ipcrm -M $Id
        fi
    
        #clean sem
        SEM_ID=`ipcs -s | grep $Id | grep -v grep`
        if [ ! -z "$SEM_ID" ]
        then
            echo "ipcrm -S $Id"
            ipcrm -S $Id
        fi
    done
}
