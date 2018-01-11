#ifndef __PARAM_CHECK_H__
#define __PARAM_CHECK_H__

#include "ajs_define.h"
#include "util.h"

static string g_strProxyType;

static int g_nDefaultClientModule;
static string g_strDefaultData;
static int g_nDefaultStep;
static int g_nDefaultErrDeal;
static int g_nDefaultBufSize;
static string g_strDefaultUser;
static int g_nDefaultTimeout;
static int g_nDefaultSignal;


static bool g_bAccessControl = true;

const char* ConvertErrorNoToStr(int nErrNo) {
	const char* pError = CAjsErrorNoToStr::ErrorNoToStr(nErrNo);
	return pError;
}

const char* ConvertStatusIntToStr(int nStatus) {
		if (nStatus == JOB_STATUS_NOT_START)
			return "not_start";
		else if (nStatus == JOB_STATUS_RUNNING)
			return "running";
		else if (nStatus == JOB_STATUS_PAUSE)
			return "pause";
		else if (nStatus == JOB_STATUS_STOP)
			return "stop";
		else if (nStatus == JOB_STATUS_NORMAL_FINISH)
			return "normal_finish";
		else if (nStatus == JOB_STATUS_UNDOED)
			return "undoed";
		else if (nStatus == JOB_STATUS_SERVER_CRASHED)
			return "crashed";
		else
			return "unknown";
	}

int ConvertStatusStrToInt(const string& strStatus) {
	if (strStatus == "not_start")
		return JOB_STATUS_NOT_START;
	else if (strStatus == "running")
		return JOB_STATUS_RUNNING;
	else if (strStatus == "pause")
		return JOB_STATUS_PAUSE;
	else if (strStatus == "stop")
		return JOB_STATUS_STOP;
	else if (strStatus == "normal_finish")
		return JOB_STATUS_NORMAL_FINISH;
	else if (strStatus == "undoed")
		return JOB_STATUS_UNDOED;
	else if (strStatus == "crashed")
		return JOB_STATUS_SERVER_CRASHED;
	else if (strStatus == "unknown") return JOB_STATUS_UNKNOWN;
	else
		return -1;
}

int CheckRunModule(const string& strRunModule)
{
	if (strRunModule == "continue")
		return 0;
	else if (strRunModule == "debug" )
		return AJS_FLAG_RUN_DEBUG_NOT_CONTINUE;
	else return -1;
}

int CheckDeleteModule(const string& strDeleteModule)
{
	if (strDeleteModule == "manual" )
		return 0;
	else if ( strDeleteModule == "auto" )
		return AJS_FLAG_DELETE_AUTO_NOT_MANUAL;
	else return -1;
}


int CheckNowRun(const string& strNowRun)
{
	if (strNowRun == "wait_set_action" )
		return 0;
	else if (strNowRun == "immediate")
		return AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL;
	else return -1;
}

int CheckOp(const string& strOp)
{
	if (strOp == "run")
		return JOB_ACTION_RUN;
	else if (strOp == "stop")
		return JOB_ACTION_STOP;
	else if (strOp == "pause")
		return JOB_ACTION_PAUSE;
	else if (strOp == "retry")
		return JOB_ACTION_RETRY;
	else if (strOp == "skip")
		return JOB_ACTION_SKIP;
	else if (strOp == "skip_to_step")
		return JOB_ACTION_SKIP_TO_STEP;
	else if (strOp == "back_one")
		return JOB_ACTION_BACK_ONE;
	else if (strOp == "undoed")
		return JOB_ACTION_UNDOED;
	else return -1;
}

int CheckField(const string& strField)
{
	if (strField == "status")
		return JOB_FIELD_STATUS;
	else if (strField == "step_now")
		return JOB_FIELD_STEP_NOW ;
	else if (strField == "step_all")
		return JOB_FIELD_STEP_ALL;
	else if (strField == "now_run")
		return JOB_FIELD_FLAG;
	else if (strField == "run_mode")
		return JOB_FIELD_FLAG;
	else if (strField == "delete_mode")
		return JOB_FIELD_FLAG;
	else if (strField == "create_time")
		return JOB_FIELD_CREATE_TIME ;
	else if (strField == "begin_time")
		return JOB_FIELD_BEGIN_TIME ;
	else if (strField == "last_running_time")
		return JOB_FIELD_LAST_RUNNING_TIME ;
	else if (strField == "end_time")
		return JOB_FIELD_END_TIME ;
	else if (strField == "client_module")
		return JOB_FIELD_CLIENT_MODULE ;
	else if (strField == "signal")
		return JOB_FIELD_SIGNAL ;
	else if (strField == "author")
		return JOB_FIELD_AUTHOR;
	else if (strField == "query_key")
		return JOB_FIELD_QUERY_KEY ;
	else if (strField == "job_type")
		return JOB_FIELD_JOB_TYPE ;
	else if (strField == "job_desc")
		return JOB_FIELD_JOB_DESC;
	else if (strField == "data")
		return JOB_FIELD_DATA ;
	else if (strField == "step_desc")
		return JOB_FIELD_STEP_DESC;
	else if (strField == "step_info")
		return JOB_FIELD_STEP_INFO;
	else if (strField == "step_time")
		return JOB_FIELD_STEP_TIME;
	else if (strField == "step_exit_code")
		return JOB_FIELD_STEP_EXIT_CODE;
	else if (strField == "step_out")
		return JOB_FIELD_STEP_OUT ;
	else if (strField == "step_err")
		return JOB_FIELD_STEP_ERR;
	else if (strField == "step_sys_err")
		return JOB_FIELD_STEP_SYS_ERR; 
	else if (strField == "file_trans_schedule")
		return JOB_FIELD_STEP_FILE_TRANS_SCHEDULE; 
	else if (strField == "file_trans_total_size")
		return JOB_FIELD_STEP_FILE_TRANS_TOTAL_SIZE; 
	else return -1;
}

int CheckOrderBy(const string& strOrderBy)
{
	if (strOrderBy != "create_time" &&
			strOrderBy != "begin_time" &&
			strOrderBy != "last_running_time" &&
			strOrderBy != "end_time" &&
			strOrderBy != "asc" &&
			strOrderBy != "desc" )
		return -1;
	else return 0;
}

int CheckCmdType(const string& strCmdType)
{
	if (strCmdType != "risky_port" &&
		strCmdType != "white_list" &&
		strCmdType != "normal_port" &&
			strCmdType != "create" &&
			strCmdType != "create2" &&
			strCmdType != "create_sync" &&
			strCmdType != "create_sync2" &&
			strCmdType != "op" &&
			strCmdType != "update" &&
			strCmdType != "query" &&
			strCmdType != "query2" &&
			strCmdType != "query_field" &&
			strCmdType != "query_select" &&
			strCmdType != "delete" &&
			strCmdType != "create2_batch" &&
			strCmdType != "op_batch" &&
			strCmdType != "update_batch" &&
			strCmdType != "query2_batch" &&
			strCmdType != "query_field_batch" &&
			strCmdType != "delete_batch" &&
			strCmdType != "sequence_create" &&
			strCmdType != "sequence_erase" &&
			strCmdType != "sequence_delete" &&
            strCmdType != "create_many" &&
            strCmdType != "query_time" &&
            strCmdType != "query_ajs_agent" &&
            strCmdType != "query_tsc_agent")
		return -1;
	else return 0;
}

int CheckNotLog(int nNotLog)
{
	if ( nNotLog == 0 )
		return 0;
	else if ( nNotLog == 1 )
		return AJS_FLAG_NOT_LOG;
	else return -1;
}

int CheckNotGetOut(int nNotGetOut)
{
	if ( nNotGetOut == 0 )
		return 0;
	else if ( nNotGetOut == 1 )
		return AJS_FLAG_NOT_GET_OUT;
	else return -1;
}

int CheckNotGetErr(int nNotGetErr)
{
	if ( nNotGetErr == 0 )
		return 0;
	else if ( nNotGetErr == 1 )
		return AJS_FLAG_NOT_GET_ERR;
	else return -1;
}

int CheckFileOutMaxConRetry(int nFileOutMaxConRetry)
{
    if ( nFileOutMaxConRetry == 0 )
        return 0;
    else if ( nFileOutMaxConRetry == 1 )
        return AJS_FLAG_FILE_TRANS_OUT_MAX_CON_RETRY;
    else return -1;
}
    
int CheckStringToInt(const string& strInt, int & nInt)
{
	int ret = StringToInt(strInt);
	string tmp = IntToString(ret);

	if (strInt == tmp) {
		nInt = ret;
		return 0;
	}
	else return -1;
}

string ConvertFieldInt2Str(int nField)
{
	if (nField == JOB_FIELD_STATUS)
		return string("status");
	else if (nField == JOB_FIELD_STEP_NOW )
		return string("step_now");
	else if (nField == JOB_FIELD_STEP_ALL)
		return string("step_all");
	else if (nField == JOB_FIELD_FLAG)
		return string("flag");
	else if (nField == JOB_FIELD_CREATE_TIME )
		return string("create_time");
	else if (nField == JOB_FIELD_BEGIN_TIME )
		return string("begin_time");
	else if (nField == JOB_FIELD_LAST_RUNNING_TIME )
		return string("last_running_time");
	else if (nField == JOB_FIELD_END_TIME )
		return string("end_time");
	else if (nField == JOB_FIELD_CLIENT_MODULE)
		return string("client_module");
	else if (nField == JOB_FIELD_SIGNAL )
		return string("signal");
	else if (nField == JOB_FIELD_AUTHOR)
		return string("author");
	else if (nField == JOB_FIELD_QUERY_KEY )
		return string("query_key");
	else if (nField == JOB_FIELD_JOB_TYPE )
		return string("job_type");
	else if (nField == JOB_FIELD_JOB_DESC)
		return string("job_desc");
	else if (nField == JOB_FIELD_DATA )
		return string("data");
	else if (nField == JOB_FIELD_STEP_DESC)
		return string("step_desc");
	else if (nField == JOB_FIELD_STEP_INFO)
		return string("step_info");
	else if (nField == JOB_FIELD_STEP_TIME)
		return string("step_time");
	else if (nField == JOB_FIELD_STEP_EXIT_CODE)
		return string("step_exit_code");
	else if (nField == JOB_FIELD_STEP_OUT )
		return string("step_out");
	else if (nField == JOB_FIELD_STEP_ERR)
		return string("step_err");
	else if (nField == JOB_FIELD_STEP_SYS_ERR)
		return string("step_sys_err"); 
	else if (nField == JOB_FIELD_STEP_FILE_TRANS_SCHEDULE)
		return string("file_trans_schedule"); 
	else if (nField == JOB_FIELD_STEP_FILE_TRANS_TOTAL_SIZE)
		return string("file_trans_total_size"); 
	else return string("unknown");
}
#endif
