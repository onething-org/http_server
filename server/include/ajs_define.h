#ifndef __AJS_DEFINE_H__
#define __AJS_DEFINE_H__

#define AJS_OK 0
#define AJS_UNKNOWN_ERROR "unknown error"

#define WORK_ASN_BUFFER_AUTO_ADJUST_SECOND 7200

#define AJS_FLAG_NOT_LOG 1
#define AJS_FLAG_NOT_GET_OUT 2
#define AJS_FLAG_NOT_GET_ERR 4
#define AJS_FLAG_RUN_DEBUG_NOT_CONTINUE 8
#define AJS_FLAG_DELETE_AUTO_NOT_MANUAL 16
#define AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL 32
#define AJS_FLAG_DELETE_NOT_RETURN 64
#define AJS_FLAG_SET_ACTION_NOT_RETURN 128
#define AJS_FLAG_NOT_TO_DB 256
#define AJS_FLAG_FILE_TRANS_CREATE_2 512
#define AJS_FLAG_FILE_TRANS_TYPE_TRANSIT 1024
#define AJS_FLAG_FILE_TRANS_TYPE_TRANSIT_LAST_STEP 2048
#define AJS_FLAG_CREATE_SYNC 4096
#define AJS_FLAG_CREATE_PARENTS_DIR 8192
#define AJS_FLAG_FILE_TRANS_OUT_MAX_CON_RETRY 16384

#define WORK_FLAG_NEW 1
#define WORK_FLAG_DEAD 2

#define AJS_AGENT_TYPE_WS 1
#define AJS_AGENT_TYPE_TSC 2

#define AJS_REQ_RS_TYPE_ALL 1
#define AJS_REQ_RS_TYPE_SOME 2

#define AJS_WORK_COMMON_AGENT_IP 16777343
#define AJS_WORK_ALL_AGENT_IP 4294967295

#define AJS_ERROR_IP 1000

#define WORK_FAILED_AGENT_ID 102

#define WORK_AGENT_FAILED_WORK_ID 300
#define WORK_AGENT_CMD_CREATE_FAILED_NEW_WORK 301
#define WORK_AGENT_CREATE_FAILED_CONCURRENT_MAX 302
#define WORK_AGENT_CMD_SET_ACTION_FAILED_CREATE_PIPE 304
#define WORK_AGENT_CMD_SET_ACTION_FAILED_FORK 305
#define WORK_AGENT_CMD_SET_ACTION_FAILED_MALLOC_BUF 306
#define WORK_AGENT_CMD_SET_ACTION_FAILED_ERR_ACTION 309
#define WORK_AGENT_CONNECT_NEW_SERVER_ERROR 310
#define WORK_AGENT_FILE_SVR_CREATE_FAILED_BIND 313
#define WORK_AGENT_FILE_SVR_GET_FAILED_SRC_FILE 314
#define WORK_AGENT_FILE_SVR_GET_FAILED_SRC_WORK_ID 315
#define WORK_AGENT_FILE_SVR_GET_FAILED_BEGIN_POS 316
#define WORK_AGENT_FILE_CLIENT_FAILED_CONNECT_SVR 317
#define WORK_AGENT_FILE_CLIENT_FAILED_SVR_TIMEOUT 318
#define WORK_AGENT_FILE_CLIENT_FAILED_CREATE_FILE 320
#define WORK_AGENT_FILE_CLIENT_FAILED_NO_SPACE 321
#define WORK_AGENT_FILE_CLIENT_FAILED_EXCEPTION 322
#define WORK_AGENT_FAILED_WORK_ID_EXIST 323
#define WORK_AGENT_FAILED_TIMEOUT 324
#define WORK_AGENT_FAILED_WAITPID 325
#define WORK_AGENT_FAILED_SIGNAL 326
#define WORK_AGENT_FAILED_KILLED_BY_USER 327
#define WORK_AGENT_FILE_SVR_CREATE_FAILED_OPEN_FILE 328
#define WORK_AGENT_FILE_CLIENT_FILE_IN_CACHE_TRANSITED 329
#define WORK_AGENT_FILE_CLIENT_FILE_IN_CACHE_TRANSITING 330
#define WORK_SERVER_TTL_END 331
#define WORK_AGENT_FILE_CLIENT_FILE_CACHE_MAX 332
#define WORK_AGENT_FILE_SVR_GET_FAILED_MALLOC 333

#define WORK_STATUS_NOT_BEGIN 0
#define WORK_STATUS_RUNNING 1
#define WORK_STATUS_FINISHED 2
#define WORK_STATUS_DELETED 3
#define WORK_STATUS_ERROR 4

#define WORK_FILE_TRANS_TYPE_COMMON 0
#define WORK_FILE_TRANS_TYPE_MASTER 1
#define WORK_FILE_TRANS_TYPE_SLAVE 2

#define COMMON_WORK_ACTION_RUN 1

#define JOB_ACTION_RUN 0
#define JOB_ACTION_STOP 1
#define JOB_ACTION_PAUSE 2
#define JOB_ACTION_RETRY 3
#define JOB_ACTION_SKIP 4
#define JOB_ACTION_SKIP_TO_STEP 5
#define JOB_ACTION_BACK_ONE 7
#define JOB_ACTION_UNDOED 8

#define JOB_STATUS_UNKNOWN 0
#define JOB_STATUS_NOT_START 1
#define JOB_STATUS_RUNNING 2
#define JOB_STATUS_PAUSE 3
#define JOB_STATUS_STOP 4
#define JOB_STATUS_NORMAL_FINISH 5
#define JOB_STATUS_UNDOED 7
#define JOB_STATUS_SERVER_CRASHED 8

#define JOB_WORK_ERROR_NEXT_STEP_PAUSE 0
#define JOB_WORK_ERROR_NEXT_STEP_NEXT 1
#define JOB_WORK_ERROR_NEXT_STEP_STOP 2

#define JOB_QUERY_SYS_ALL_JOB 0
#define JOB_QUERY_SYS_RUNNING_JOB 1
#define JOB_QUERY_SYS_FINISH_JOB 2
#define JOB_QUERY_SYS_NOT_START_JOB 3

#define JOB_SELECT_SORT_NONE 0
#define JOB_SELECT_SORT_ASC 1
#define JOB_SELECT_SORT_DESC 2

#define JOB_QUERY_STEP_NOW 0
#define JOB_CLIENT_MODULE_ALL 0

#define JOB_FIELD_STATUS 1
#define JOB_FIELD_STEP_NOW 2
#define JOB_FIELD_STEP_ALL 3
#define JOB_FIELD_FLAG 4
#define JOB_FIELD_CREATE_TIME 5
#define JOB_FIELD_BEGIN_TIME 6
#define JOB_FIELD_LAST_RUNNING_TIME 7
#define JOB_FIELD_END_TIME 8
#define JOB_FIELD_CLIENT_MODULE 9
#define JOB_FIELD_SIGNAL 10
#define JOB_FIELD_AUTHOR 11
#define JOB_FIELD_QUERY_KEY 12
#define JOB_FIELD_JOB_TYPE 13
#define JOB_FIELD_JOB_DESC 14
#define JOB_FIELD_DATA 15
#define JOB_FIELD_STEP_DESC 16
#define JOB_FIELD_STEP_INFO 17
#define JOB_FIELD_STEP_TIME 18
#define JOB_FIELD_STEP_EXIT_CODE 19
#define JOB_FIELD_STEP_OUT 20
#define JOB_FIELD_STEP_ERR 21
#define JOB_FIELD_STEP_SYS_ERR 22
#define JOB_FIELD_STEP_FILE_TRANS_SCHEDULE 23
#define JOB_FIELD_STEP_FILE_TRANS_TOTAL_SIZE 24
#define JOB_FIELD_STEP_TIME2 25

#define JOB_SERVER_FAILED_STOP_SERVICES 500
#define JOB_SERVER_FAILED_STEP_ERR_DEAL_TYPE 502
#define JOB_SERVER_FAILED_INSERT_INTO_DB 504
#define JOB_SERVER_FAILED_EMPTY_STEP 505
#define JOB_SERVER_FAILED_STEP_NOT_EXIST 506
#define JOB_SERVER_FAILED_JOB_ID_NOT_EXIST 507
#define JOB_SERVER_FAILED_STATUS_ERROR 508
#define JOB_SERVER_FAILED_STEP_NUM 510
#define JOB_SERVER_FAILED_STEP_UNKNOWN 511
#define JOB_SERVER_FAILED_FIELD_TYPE 512
#define JOB_SERVER_FAILED_QUERY_SELECT_FIELD 513
#define JOB_SERVER_FAILED_SERVER_CRASHED 515
#define JOB_SERVER_FAILED_AGENT_NOT_ONLINE 516

#define JOB_SERVER_FAILED_STEP_WS_FILE_DATA_EMPTY 540
#define JOB_SERVER_FAILED_STEP_WS_FILE_SYSTEM 546
#define JOB_SERVER_FAILED_STEP_WS_FILE_TIMEOUT 547

#define JOB_SERVER_FAILED_STEP_RS_UFT_SYSTEM 550
#define JOB_SERVER_FAILED_STEP_RS_UFT_NOW_STEP_ERROR 551
#define JOB_SERVER_FAILED_STEP_RS_UFT_PATH_FAILED 552
#define JOB_SERVER_FAILED_STEP_RS_UFT_TIMEOUT 553
#define JOB_SERVER_FAILED_STEP_JS_UFT_CACHE_DIR 554
#define JOB_SERVER_FAILED_STEP_WA_UFT_RET_ERROR 555

#define JOB_SERVER_FAILED_STEP_WS_CMD_DATA_EMPTY 560
#define JOB_SERVER_FAILED_STEP_WS_CMD_SYSTEM 566
#define JOB_SERVER_FAILED_STEP_WS_CMD_TIMEOUT 567

#define JOB_SERVER_FAILED_STEP_TSC_CMD_DATA_EMPTY 580
#define JOB_SERVER_FAILED_STEP_TSC_CMD 581
#define JOB_SERVER_FAILED_STEP_TSC_CMD_TIMEOUT 582
#define JOB_SERVER_FAILED_STEP_TSC_CMD_CRASHED 583

#define JOB_SERVER_FAILED_STEP_TSC_FILE_DATA_EMPTY 585
#define JOB_SERVER_FAILED_STEP_TSC_FILE 586
#define JOB_SERVER_FAILED_STEP_TSC_FILE_TIMEOUT 587
#define JOB_SERVER_FAILED_STEP_TSC_FILE_CRASHED 588

#define JOB_SERVER_FAILED_STEP_NEED_CHANGE_TO_TSC 600
#define JOB_SERVER_FAILED_FILE_STEP_NEED_CHANGE_TO_UFT 601

//access server define
#define BAD_JSON_REQUEST 10001
#define INTERNET_SERVER_ERROR 10002
#define GATWAY_TIMEOUT 10003
#define METHOD_NOT_ALLOWED 10004
#define ACCESS_DENIED 10005

#define AJS_ERRNO 1
#define TSC_ERRNO 2
#define USR_IP_NOT_EXIST_ERRNO 3
#define USR_FILE_PATH_ERRNO 4
#define USR_STEP_TIME_OUT_ERRNO 5
#define USR_OTHER_ERRNO 6

//serial server define
#define SERIAL_SERVER_FAILED_RECEIVE_FROM_AS 700
#define SERIAL_SERVER_FAILED_INSERT_TO_DB 701
#define SERIAL_SERVER_FAILED_TASK_ID_NOT_EXIST 702

class CAjsErrorNoToStr {
public:
    /*
     * 将错误号转换成文字
     */
    static const char* ErrorNoToStr(int nErrorNo) {
        switch (nErrorNo) {
        case AJS_OK:
            return "ok";
            break;
        case WORK_FAILED_AGENT_ID:
            return "agent id is error";
            break;
        case WORK_AGENT_FAILED_WORK_ID:
            return "the work id is error";
            break;
        case WORK_AGENT_CMD_CREATE_FAILED_NEW_WORK:
            return "create new work is error in the work agent";
            break;
        case WORK_AGENT_CREATE_FAILED_CONCURRENT_MAX:
            return "concurrent is too large now in the work agent";
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_CREATE_PIPE:
            return "the work agent create pipe is error";
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_FORK:
            return "the work agent fork the process is error";
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_MALLOC_BUF:
            return "the work agent cmd execute malloc buf is error";
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_ERR_ACTION:
            return "the work agent set action is error because action error";
            break;
        case WORK_AGENT_CONNECT_NEW_SERVER_ERROR:
            return "the work agent connect new server is error";
            break;
        case WORK_AGENT_FILE_SVR_CREATE_FAILED_BIND:
            return "bind a port was error in src file transport agent";
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_SRC_FILE:
            return "open the file was error in src file transport agent";
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_SRC_WORK_ID:
            return "check src work id of dst agent specified was error in src file transport agent";
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_BEGIN_POS:
            return "check the begin pos of dst agent get was error in src file transport agent";
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_CONNECT_SVR:
            return "connect the src file agent was error in dst file transport agent";
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_SVR_TIMEOUT:
            return "wait the src file agent rsp data was timeout in dst file transport agent";
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_CREATE_FILE:
            return "create the file is error in dst file transport agent";
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_NO_SPACE:
            return "no enough space in dst file transport agent";
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_EXCEPTION:
            return "some execption was happend in dst file transport agent";
            break;
        case WORK_AGENT_FAILED_WORK_ID_EXIST:
            return "the work id was exist in agent";
            break;
        case WORK_AGENT_FAILED_TIMEOUT:
            return "the task was timeout";
            break;
        case WORK_AGENT_FAILED_WAITPID:
            return "the task wait pid was error";
            break;
        case WORK_AGENT_FAILED_SIGNAL:
            return "the task was killed by signal of out";
            break;
        case WORK_AGENT_FAILED_KILLED_BY_USER:
            return "the task was killed by user";
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_MALLOC:
            return "malloc was error in src file transport agent";
            break;

        case JOB_SERVER_FAILED_STOP_SERVICES:
            return "the job server is in stop service status";
            break;
        case JOB_SERVER_FAILED_STEP_ERR_DEAL_TYPE:
            return "the step next deal type is error when occur err";
            break;
        case JOB_SERVER_FAILED_INSERT_INTO_DB:
            return "insert into db is error";
            break;
        case JOB_SERVER_FAILED_EMPTY_STEP:
            return "the step is empty";
            break;
        case JOB_SERVER_FAILED_STEP_NOT_EXIST:
            return "the step id is not exist";
            break;
        case JOB_SERVER_FAILED_JOB_ID_NOT_EXIST:
            return "the job id is not exist";
            break;
        case JOB_SERVER_FAILED_STATUS_ERROR:
            return "the job status is error";
            break;
        case JOB_SERVER_FAILED_STEP_NUM:
            return "the step num is error in now work";
            break;
        case JOB_SERVER_FAILED_STEP_UNKNOWN:
            return "the step type is unknown(need cmd, or file)";
            break;
        case JOB_SERVER_FAILED_FIELD_TYPE:
            return "field type is error";
            break;
        case JOB_SERVER_FAILED_QUERY_SELECT_FIELD:
            return "query select field is error";
            break;
        case JOB_SERVER_FAILED_SERVER_CRASHED:
            return "job server was crashed, so can not update this field now";
            break;
        case JOB_SERVER_FAILED_AGENT_NOT_ONLINE:
            return "the agent not online now";
            break;

        case JOB_SERVER_FAILED_STEP_WS_FILE_DATA_EMPTY:
            return "work filesvr system is failed(the cmd work data is empty)";
            break;
        case JOB_SERVER_FAILED_STEP_WS_FILE_SYSTEM:
            return "work filesvr system is failed(filesvr system is error)";
            break;
        case JOB_SERVER_FAILED_STEP_WS_FILE_TIMEOUT:
            return "work filesvr system is failed(connect is timeout)";
            break;

		case JOB_SERVER_FAILED_STEP_RS_UFT_SYSTEM:
			return "";
			break;
		case JOB_SERVER_FAILED_STEP_RS_UFT_NOW_STEP_ERROR:
			return "";
			break;
		case JOB_SERVER_FAILED_STEP_RS_UFT_PATH_FAILED:
			return "";
			break;
		case JOB_SERVER_FAILED_STEP_RS_UFT_TIMEOUT:
			return "";
			break;
		case JOB_SERVER_FAILED_STEP_JS_UFT_CACHE_DIR:
			return "";
			break;
		case JOB_SERVER_FAILED_STEP_WA_UFT_RET_ERROR:
			return "";
			break;
	
        case JOB_SERVER_FAILED_STEP_WS_CMD_DATA_EMPTY:
            return "work cmd execute system is failed(the file work data is empty)";
            break;
        case JOB_SERVER_FAILED_STEP_WS_CMD_SYSTEM:
            return "work cmd execute system is failed(cmd system is error)";
            break;
        case JOB_SERVER_FAILED_STEP_WS_CMD_TIMEOUT:
            return "work cmd execute system is failed(connect is timeout)";
            break;

        case JOB_SERVER_FAILED_STEP_TSC_CMD_DATA_EMPTY:
            return "tsc system is failed(the cmd work data is empty)";
            break;
        case JOB_SERVER_FAILED_STEP_TSC_CMD:
            return "tsc system is failed(cmd system is error)";
            break;
        case JOB_SERVER_FAILED_STEP_TSC_CMD_TIMEOUT:
            return "tsc system is failed(connect is timeout)";
            break;

        case JOB_SERVER_FAILED_STEP_TSC_FILE_DATA_EMPTY:
            return "tsc system is failed(the file work data is empty)";
            break;
        case JOB_SERVER_FAILED_STEP_TSC_FILE:
            return "tsc system is failed(filesvr system is error)";
            break;
        case JOB_SERVER_FAILED_STEP_TSC_FILE_TIMEOUT:
            return "tsc system is failed(connect is timeout)";
            break;

        case BAD_JSON_REQUEST:
        	return "Bad Json Request: ";
        	break;
        case INTERNET_SERVER_ERROR:
        	return "Internet Server Error: send packet failed";
        	break;
        case GATWAY_TIMEOUT:
        	return "Gateway Time Out: job server response time out";
        	break;
        case METHOD_NOT_ALLOWED:
        	return "Method Not Allowed: please use http get or post method";
        	break;
        case ACCESS_DENIED:
        	return "Access denied: ";
        	break;

        default:
            break;
        }

        return AJS_UNKNOWN_ERROR;
    }

    static int CheckSysErrno(int nErrorNo) {
        switch (nErrorNo) {
        //OK
        case AJS_OK:
        	return 0;
        	break;

        //AJS SYS ERR
        case WORK_AGENT_FAILED_WORK_ID:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CMD_CREATE_FAILED_NEW_WORK:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CREATE_FAILED_CONCURRENT_MAX:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_CREATE_PIPE:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_FORK:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_MALLOC_BUF:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_CMD_SET_ACTION_FAILED_ERR_ACTION:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_FILE_SVR_CREATE_FAILED_BIND:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_SRC_WORK_ID:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_FAILED_WORK_ID_EXIST:
            return AJS_ERRNO;
            break;
        case WORK_AGENT_FAILED_WAITPID:
            return AJS_ERRNO;
            break;
        case JOB_SERVER_FAILED_STOP_SERVICES:
            return AJS_ERRNO;
            break;
        case JOB_SERVER_FAILED_INSERT_INTO_DB:
            return AJS_ERRNO;
            break;
        case JOB_SERVER_FAILED_SERVER_CRASHED:
            return AJS_ERRNO;
            break;
        case JOB_SERVER_FAILED_STEP_WS_FILE_SYSTEM:
            return AJS_ERRNO;
            break;
        case JOB_SERVER_FAILED_STEP_WS_CMD_SYSTEM:
            return AJS_ERRNO;
            break;

        //TSC SYS ERR
        case JOB_SERVER_FAILED_STEP_TSC_CMD:
            return TSC_ERRNO;
            break;
        case JOB_SERVER_FAILED_STEP_TSC_CMD_TIMEOUT:
            return TSC_ERRNO;
            break;
        case JOB_SERVER_FAILED_STEP_TSC_FILE:
            return TSC_ERRNO;
            break;
        case JOB_SERVER_FAILED_STEP_TSC_FILE_TIMEOUT:
            return TSC_ERRNO;
            break;

        //USER ERR
        //ip not exist
        case WORK_FAILED_AGENT_ID:
            return USR_IP_NOT_EXIST_ERRNO;
            break;
        case WORK_AGENT_CONNECT_NEW_SERVER_ERROR:
            return USR_IP_NOT_EXIST_ERRNO;
            break;
        case JOB_SERVER_FAILED_AGENT_NOT_ONLINE:
            return USR_IP_NOT_EXIST_ERRNO;
            break;
        //file path err
        case WORK_AGENT_FILE_SVR_GET_FAILED_SRC_FILE:
            return USR_FILE_PATH_ERRNO;
            break;
        case WORK_AGENT_FILE_SVR_GET_FAILED_BEGIN_POS:
            return USR_FILE_PATH_ERRNO;
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_CREATE_FILE:
            return USR_FILE_PATH_ERRNO;
            break;
        case WORK_AGENT_FILE_CLIENT_FAILED_NO_SPACE:
            return USR_FILE_PATH_ERRNO;
            break;
        //step time out
        case WORK_AGENT_FAILED_TIMEOUT:
            return USR_STEP_TIME_OUT_ERRNO;
            break;
        //others
        default:
        	return USR_OTHER_ERRNO;
            break;
        }
        return -1;
    }
};

#endif
