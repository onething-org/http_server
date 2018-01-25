#include "http_server.h"
#include "param_check.h"
//#include <gperftools/profiler.h>

extern int g_nLogStrLen;
static int r_code_200 = 200;
int g_nMaxPerPage;
int g_nMaxStdOut;
int g_nMaxCreateBatchCount;
int g_nMaxQueryBatchCount;
int g_nMaxQueryFieldBatchCount;
int g_nMaxQueryAgentOnlineCount;
time_t g_timeLastClearAsyncJob;
int g_nClearAsyncTimeInterval;
unsigned int g_nSerialServerIp;
unsigned short g_nSerialServerPort;
unsigned int   g_jobServerTimeOut;
unsigned int   g_sleepTime;
unsigned int   g_nSerialTimeInterval;
unsigned int   g_nHttpPacketMaxLen;
unsigned int g_nScanServerNum;
unsigned int g_nPortOpenPercent;
string g_strDefaultUrl;
string g_strAlarmMsg;
int g_nAlarmId;
unsigned int g_nCountToSend;

// RabbitMQ
string m_rmqhost;
int m_rmqport;
string m_rmquser;
string m_rmqpwd;
string m_rmqvhost;
string m_rmqexchange;
string m_rmqroutingkey;
int m_rmqdeliverymode;

time_t g_nNowTime;

static const string BAD_JSON_REQUEST_REASON = CAjsErrorNoToStr::ErrorNoToStr(BAD_JSON_REQUEST);
static const string INTERNET_SERVER_ERROR_REASON = CAjsErrorNoToStr::ErrorNoToStr(INTERNET_SERVER_ERROR);
static const string GATWAY_TIMEOUT_REASON = CAjsErrorNoToStr::ErrorNoToStr(GATWAY_TIMEOUT);
static const string METHOD_NOT_ALLOWED_REASON = CAjsErrorNoToStr::ErrorNoToStr(METHOD_NOT_ALLOWED);
static const string ACCESS_DENIED_REASON = CAjsErrorNoToStr::ErrorNoToStr(ACCESS_DENIED);

static char r_reason_200[] = "OK";

const string http_server_version = "http_server_version = 1.0.0";

void LogJsonObj(int level, const Json::Value & obj)
{ 
	Json::StyledWriter writer; 
	string strObj = writer.write(obj);
	Log(level, "Json::Value :\n%s", strObj.c_str());
}

int StringToLogLevel(const string& strLogLevel)
{
	if (strLogLevel == "LOG_LEVEL_ALL")
		return LOG_LEVEL_ALL;
	else if (strLogLevel == "LOG_LEVEL_LOWEST")
		return LOG_LEVEL_LOWEST;
	else if (strLogLevel == "LOG_LEVEL_OBJ_DEBUG")
		return LOG_LEVEL_OBJ_DEBUG;
	else if (strLogLevel == "LOG_LEVEL_DEBUG")
		return LOG_LEVEL_DEBUG;
	else if (strLogLevel == "LOG_LEVEL_INFO")
		return LOG_LEVEL_INFO;
	else if (strLogLevel == "LOG_LEVEL_WARNING")
		return LOG_LEVEL_WARNING;
	else if (strLogLevel == "LOG_LEVEL_ERROR")
		return LOG_LEVEL_ERROR;
	else return LOG_LEVEL_NONE;
}

bool CRotationScheduler::Init(const set<unsigned short> &job_server)
{
	m_jobServer_set = job_server;
	m_current_it = m_jobServer_set.begin();
	return true;
}

bool CRotationScheduler::GetAJobServer(unsigned short &job_server_id)
{
	if (m_jobServer_set.begin() == m_jobServer_set.end()){
		return false;
	} else if ( m_current_it != m_jobServer_set.end() ){
		job_server_id = *m_current_it;
	} else {
		m_current_it = m_jobServer_set.begin();
		job_server_id = *m_current_it;
	}

	m_current_it++;
	return true;
}

bool CRotationScheduler::GetAllJobServer(vector<unsigned short> &serverVct)
{
	serverVct.clear();
	for(set<unsigned short>::iterator it = m_jobServer_set.begin();
			it != m_jobServer_set.end();
			++it){
		serverVct.push_back(*it);
	}
	return true;
}

CHttpServerApp::CHttpServerApp()
	:m_pScheduler(new CRotationScheduler()) , m_pStatistic(new CStatistic(false))
	,m_logStatisticLastTime(0), m_logStatisticTimeInterval(0)
    ,m_adjustBufferLastTime(0), m_adjustBufferTimeInterval(0), m_adjustUniqueLastTime(0), m_adjustUniqueTimeInterval(0), m_nCcdRequests(0), m_nDccReceives(0)
	,m_nInvalidCcdRequest(0), m_nInvalidDccReceives(0), m_nTimeOutedDccReceives(0)
	,m_bPrintJobServerInfo(false),m_analyzeRiskLastTime(0)
{
	m_clientAuth = new CClientAuth(this);
	LoadCfg();

	//初始化调度类
	m_pScheduler->Init(m_schedulerJobServer_set);

	//初始化job id管理类
	g_nNowTime = time(0);
	g_timeLastClearAsyncJob = g_nNowTime;

	SetTimer(1);
}

CHttpServerApp::~CHttpServerApp()
{
	delete m_pScheduler;
	delete m_pStatistic;
	m_pScheduler = NULL;
	m_pStatistic = NULL;

	delete m_clientAuth;
	m_clientAuth = NULL;
}

void CHttpServerApp::LoadCfg()
{
	char szFile[255];
	pid_t pID = getpid();
	sprintf(szFile, "/proc/%d/cwd/../etc/config", (int)pID);

	//读取
	CIniFile cfgFile;
	if (cfgFile.Load(szFile) != 0)
	{
		LogError("CHttpServerApp::LoadCfg(config file load error)");
		exit(EXIT_FAILURE);
	}

	LogFile(cfgFile.GetIni("log_file"));
	unsigned int log_file_max_num = StringToInt(cfgFile.GetIni("log_file_max_num"));
	int log_file_max_size = StringToInt(cfgFile.GetIni("log_file_max_size"));
	//SetLogInfo(StringToInt(cfgFile.GetIni("log_file_max_num")), StringToInt(cfgFile.GetIni("log_file_max_size")));
	SetLogInfo(log_file_max_num, log_file_max_size);
	LogInfo("CHttpServerApp::LoadCfg(log_file_max_num: %u, log_file_max_size: %d)", log_file_max_num, log_file_max_size);
	LogLevel(StringToLogLevel(cfgFile.GetIni("log_level")));
	LogInfo("CHttpServerApp::LoadCfg(log_file: %s)", cfgFile.GetIni("log_file").c_str());

	//get db infos
	string strDb = cfgFile.GetIni("user_info_db", "ajs");
	string strHost = cfgFile.GetIni("user_info_host", "127.0.0.1");
	string strUser = cfgFile.GetIni("user_info_user", "root");
	string strPassword = cfgFile.GetIni("user_info_password", "");
	unsigned short nPort = StringToInt(cfgFile.GetIni("user_info_port", "3306"));
	m_clientAuth->SetDbInfo(strDb, strHost, strUser, strPassword, nPort);
	m_clientAuth->SetTimeInterval(StringToInt(cfgFile.GetIni("update_client_auth_time_interval", "300")));

	m_analyzeRiskTimeInterval = StringToInt(cfgFile.GetIni("analyze_risk_time_interval", "60"));
	LogInfo("CHttpServerApp::LoadCfg(m_analyzeRiskTimeInterval: %d)", m_analyzeRiskTimeInterval);

	g_nLogStrLen = StringToInt(cfgFile.GetIni("log_str_length", "1024"));
	g_nMaxPerPage = StringToInt(cfgFile.GetIni("max_per_page", "1000"));
	g_nMaxStdOut = StringToInt(cfgFile.GetIni("max_std_out", "6291456"));
	g_nMaxCreateBatchCount = StringToInt(cfgFile.GetIni("max_create_batch_count", "1000"));
	g_nMaxQueryBatchCount = StringToInt(cfgFile.GetIni("max_query_batch_count", "1000"));
	g_nMaxQueryFieldBatchCount = StringToInt(cfgFile.GetIni("max_query_field_batch_count", "2000"));
	g_nMaxQueryAgentOnlineCount = StringToInt(cfgFile.GetIni("max_query_agent_online_count", "2000"));

	string strIp;
	string strSerialIp = cfgFile.GetIni("serial_server_ip");
	if (EthnetGetIpAddress(strIp, strSerialIp) == 0 && !strIp.empty())
		strSerialIp = strIp;
	EthnetInfoDestroy();
	if (IpStringToInt(g_nSerialServerIp, strSerialIp) != 0)
	{
		LogWarning("CHttpServerApp::LoadCfg(serial_server_ip in config file is error, serial_server_ip: %s)", strSerialIp.c_str());
		//exit(0);
	}
	//g_nSerialServerIp = IpStringToInt(cfgFile.GetIni("serial_server_ip"));
	g_nSerialServerPort = StringToInt(cfgFile.GetIni("serial_server_port"));

	//客户端valid ip
	{
		string valid_ips = cfgFile.GetIni("valid_ip_info", "");
		LogInfo("CHttpServerApp::LoadCfg(valid_ip_info: %s)", valid_ips.c_str());

		vector<string> v_ip_vct;
		SplitDataToVector(v_ip_vct, valid_ips, "|");
		m_validClientIp_set.clear();
		for(size_t i = 0; i != v_ip_vct.size(); ++i){
			unsigned int ip;
			if ( IpStringToInt(ip, Trim(v_ip_vct[i])) == 0){
				m_validClientIp_set.insert(ip);
				LogInfo("CHttpServerApp::LoadCfg(ip: %s)", v_ip_vct[i].c_str());
			}
			else 
				LogError("CHttpServerApp::LoadCfg(invalid conf client ip: %s)", v_ip_vct[i].c_str());
		}
	}

	//job servers' infomations
	{
		string valid_ips = cfgFile.GetIni("js_server_info", "");
		LogInfo("CHttpServerApp::LoadCfg(js_server_info: %s)", valid_ips.c_str());

		vector<string> v_ip_vct;
		SplitDataToVector(v_ip_vct, valid_ips, "|");

		m_allJobServer_map.clear();
		m_schedulerJobServer_set.clear();
		m_clientModuleSpecializedJobServer.clear();

		for(size_t i = 0; i != v_ip_vct.size(); ++i){
			string info = Trim(v_ip_vct[i]);
			vector<string> info_v;
			SplitDataToVector(info_v, info, ":");
			if (info_v.size() < 3){
				LogError("LoadCfg(job_server config error: %s)", v_ip_vct[i].c_str());
				exit(EXIT_FAILURE);
			}

			unsigned int ip = 0;
			unsigned int jobServerId = (unsigned short)StringToInt(Trim(info_v[0]));
			string strIp;
			int ret = EthnetGetIpAddress(strIp, Trim(info_v[1]));
			if (ret != 0) strIp = Trim(info_v[1]);
			ret = IpStringToInt(ip, strIp);
			unsigned short port = (unsigned short)StringToInt(Trim(info_v[2]));

			if (ret == 0 ){
				//所有的jobserver
				m_allJobServer_map[jobServerId] = pair<unsigned int, unsigned short>(ip, port);
				m_jobServerIp2Id_map[ip] = jobServerId;

				if ( info_v.size() >= 4 ){
					//client_module专用的jobserver
					string client_modules = Trim(info_v[3]);
					vector<string> v_client_modules;
					SplitDataToVector(v_client_modules, client_modules, ",");
					for (size_t j = 0; j != v_client_modules.size(); j++) {
						int client_module = StringToInt(Trim(v_client_modules[j]));
						m_clientModuleSpecializedJobServer[client_module] = jobServerId;
						LogInfo("CHttpServerApp::LoadCfg(job_server id: %s, ip: %s, port: %s, client_module: %d)",
								info_v[0].c_str(), info_v[1].c_str(), info_v[2].c_str(), client_module);
					}
					continue;
				} else if (info_v.size() == 3){
					//参加调度的jobserver
					m_schedulerJobServer_set.insert(jobServerId);
					LogInfo("CHttpServerApp::LoadCfg(job_server id: %s, ip: %s, port: %s)",
							info_v[0].c_str(), info_v[1].c_str(), info_v[2].c_str());
				}

			}
			else
				LogError("CHttpServerApp::LoadCfg(invalid conf job server ip: %s)", v_ip_vct[i].c_str());
		}
	}

	// risky ports
	{
		string risky_ports = cfgFile.GetIni("risky_ports", "");
		LogInfo("CHttpServerApp::LoadCfg(risky_ports: %s)", risky_ports.c_str());

		vector<string> v_ports_s;
		SplitDataToVector(v_ports_s, risky_ports, "|");
		m_riskyPorts_set.clear();
		for(size_t i = 0; i != v_ports_s.size(); ++i)
		{
			int ipt = StringToInt(Trim(v_ports_s[i]));
			m_riskyPorts_set.insert(ipt);
			LogInfo("CHttpServerApp::LoadCfg(risky port: %s)", v_ports_s[i].c_str());
		}
	}

	// risky services
	{
		string risky_services = cfgFile.GetIni("risky_services", "");
		LogInfo("CHttpServerApp::LoadCfg(risky_services: %s)", risky_services.c_str());

		vector<string> v_services_s;
		SplitDataToVector(v_services_s, risky_services, "|");
		m_riskyServices_set.clear();
		for(size_t i = 0; i != v_services_s.size(); ++i)
		{
			m_riskyServices_set.insert(v_services_s[i]);
			LogInfo("CHttpServerApp::LoadCfg(risky service: %s)", v_services_s[i].c_str());
		}
	}

    // white list
    {
        string white_list = cfgFile.GetIni("white_list", "");
        LogInfo("CHttpServerApp::LoadCfg(white_list: %s)", white_list.c_str());

        vector<string> v_list_s;
        SplitDataToVector(v_list_s, white_list, "|");
        m_whiteList_set.clear();
        for(size_t i = 0; i != v_list_s.size(); ++i)
        {
            int ipt = StringToInt(Trim(v_list_s[i]));
            m_whiteList_set.insert(ipt);
            LogInfo("CHttpServerApp::LoadCfg(white port: %s)", v_list_s[i].c_str());
        }
    }

    g_nScanServerNum = StringToInt(cfgFile.GetIni("scan_server_num"));
    LogInfo("CHttpServerApp::LoadCfg(g_nScanServerNum: %d)", g_nScanServerNum);

    g_nPortOpenPercent = StringToInt(cfgFile.GetIni("port_open_percent"));
    LogInfo("CHttpServerApp::LoadCfg(g_nPortOpenPercent: %d)", g_nPortOpenPercent);

    g_strDefaultUrl = cfgFile.GetIni("default_alarm_url", "");
    LogInfo("CHttpServerApp::LoadCfg(g_strDefaultUrl: %s)", g_strDefaultUrl.c_str());

    g_strAlarmMsg = cfgFile.GetIni("white_alarm_msg", "");
    LogInfo("CHttpServerApp::LoadCfg(g_strAlarmMsg: %s)", g_strAlarmMsg.c_str());

    g_nAlarmId = StringToInt(cfgFile.GetIni("white_alarm_id"));
    LogInfo("CHttpServerApp::LoadCfg(g_nAlarmId: %d)", g_nAlarmId);

    g_nCountToSend = StringToInt(cfgFile.GetIni("count_to_send"));
    LogInfo("CHttpServerApp::LoadCfg(g_nCountToSend: %d)", g_nCountToSend);

    m_rmqhost = cfgFile.GetIni("rmqhost", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmqhost: %s)", m_rmqhost.c_str());

	m_rmqport = StringToInt(cfgFile.GetIni("rmqport"));
	LogInfo("CHttpServerApp::LoadCfg(m_rmqport: %d)", m_rmqport);

	m_rmquser = cfgFile.GetIni("rmquser", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmquser: %s)", m_rmquser.c_str());

	m_rmqpwd = cfgFile.GetIni("rmqpwd", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmqpwd: %s)", m_rmqpwd.c_str());

	m_rmqvhost = cfgFile.GetIni("rmqvhost", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmqvhost: %s)", m_rmqvhost.c_str());

	m_rmqexchange = cfgFile.GetIni("rmqexchange", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmqexchange: %s)", m_rmqexchange.c_str());

	m_rmqroutingkey = cfgFile.GetIni("rmqroutingkey", "");
	LogInfo("CHttpServerApp::LoadCfg(m_rmqroutingkey: %s)", m_rmqroutingkey.c_str());

	m_rmqdeliverymode = StringToInt(cfgFile.GetIni("rmqdeliverymode"));
	LogInfo("CHttpServerApp::LoadCfg(m_rmqdeliverymode: %d)", m_rmqdeliverymode);

	// json参数检查配置
	{
		g_nDefaultClientModule = StringToInt(cfgFile.GetIni("default_client_module"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultClientModule: %d)", g_nDefaultClientModule);
		g_nDefaultStep = StringToInt(cfgFile.GetIni("default_step"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultStep: %d)", g_nDefaultStep);
		g_nDefaultErrDeal = StringToInt(cfgFile.GetIni("default_err_deal"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultErrDeal: %d)", g_nDefaultErrDeal);
		g_nDefaultBufSize = StringToInt(cfgFile.GetIni("default_buf_size"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultBufSize: %d)", g_nDefaultBufSize);
		g_nDefaultTimeout = StringToInt(cfgFile.GetIni("default_time_out"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultTimeout: %d)", g_nDefaultTimeout);
		g_nDefaultSignal = StringToInt(cfgFile.GetIni("default_signal"));
		LogInfo("CHttpServerApp::LoadCfg(g_nDefaultSignal: %d)", g_nDefaultSignal);

		g_strDefaultData = cfgFile.GetIni("default_data");
		LogInfo("CHttpServerApp::LoadCfg(g_strDefaultData: %s)", g_strDefaultData.c_str());
		g_strDefaultUser = cfgFile.GetIni("default_user");
		LogInfo("CHttpServerApp::LoadCfg(g_strDefaultUser: %s)", g_strDefaultUser.c_str());
	}

	//job server回包的超时时间
	g_jobServerTimeOut = StringToInt(cfgFile.GetIni("job_server_time_out"));
	LogInfo("CHttpServerApp::LoadCfg(g_jobServerTimeOut: %d)", g_jobServerTimeOut);

	//发送query的时间间隔
	g_sleepTime = StringToInt(cfgFile.GetIni("create_sync_query_time_interval"));
	LogInfo("CHttpServerApp::LoadCfg(g_sleepTime: %d)", g_sleepTime);

	g_nSerialTimeInterval = StringToInt(cfgFile.GetIni("serial_server_schedule_time_interval", "0"));
	LogInfo("CHttpServerApp::LoadCfg(serial_server_schedule_time_interval: %d ms)", g_nSerialTimeInterval);

	g_nHttpPacketMaxLen = StringToInt(cfgFile.GetIni("max_http_packet_length", "5242880"));

	g_nCreateSyncReTryCount = StringToInt(cfgFile.GetIni("create_sync_query_retry_count", "10"));

	//调整buffer大小的时间间隔
	m_adjustBufferTimeInterval = StringToInt(cfgFile.GetIni("ajust_buffer_time_interval"));
	LogInfo("CHttpServerApp::LoadCfg(m_adjustBufferTimeInterval: %d)", m_adjustBufferTimeInterval);

    //调整unique id的时间间隔
    m_adjustUniqueTimeInterval = StringToInt(cfgFile.GetIni("ajust_unique_time_interval", "3600"));
    LogInfo("CHttpServerApp::LoadCfg(m_adjustUniqueTimeInterval: %d)", m_adjustUniqueTimeInterval);

	//打印统计信息的时间间隔
	m_logStatisticTimeInterval = StringToInt(cfgFile.GetIni("log_statistic_time_interval"));
	LogInfo("CHttpServerApp::LoadCfg(m_logStatisticTimeInterval: %d)", m_logStatisticTimeInterval);

	g_bAccessControl = (StringToInt(cfgFile.GetIni("no_access_control", "0")) == 1 ? false : true);
	LogInfo("CHttpServerApp::LoadCfg(g_bAccessControl: %s)", cfgFile.GetIni("no_access_control", "0").c_str());

	m_pStatistic->Inittialize(cfgFile.GetIni("stat_file", "../log/stat").c_str());

	g_nClearAsyncTimeInterval = StringToInt(cfgFile.GetIni("clear_async_job_time_interval", "600"));
	LogInfo("CHttpServerApp::LoadCfg(clear_async_job_time_interval: %d)", g_nClearAsyncTimeInterval);

	// 初始化 m_IpPort_Host_map
	for (int i = 10001; i < 10101; ++i)
	{
		string tmp = IntToString(i);
		m_IpPort_Host_map[tmp].insert("tmc01001");
	}
}

void CHttpServerApp::OnExpire(unsigned int nUniqueId)
{
	//同步任务向js的请求超时
    map<unsigned int, SyncReqInfo>::iterator clientIt = m_mapSyncReqInfo.find(nUniqueId);
	if (clientIt == m_mapSyncReqInfo.end())
	{
		LogWarning("CHttpServerApp::OnExpire(client id is not exist. flow id: %u)", nUniqueId);
		return;
	}

	if (clientIt->second.status == SyncReqInfo::CREATE_SYNC_WAIT_RSP_QUERY_FIELD && clientIt->second.retry_count-- > 0)
	{
		LogDebug("CHttpServerApp::OnExpire(we will send query packet next time. flow id: %u, left retry count: %d)", nUniqueId, clientIt->second.retry_count);
		clientIt->second.status = SyncReqInfo::CREATE_SYNC_WAIT_REQ_QUERY_FIELD;
	}
	else
	{
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
        m_mapSyncReqInfo.erase(nUniqueId);
        LogWarning("CHttpServerApp::OnExpire(flow id: %u)", nUniqueId);
	}
}

void CHttpServerApp::OnTimer(time_t cur)
{
	g_nNowTime = cur;
	//发送
	DealSyncTasks(cur);

	if (cur > g_timeLastClearAsyncJob)
	{
		if (cur - g_timeLastClearAsyncJob > g_nClearAsyncTimeInterval)
		{
			//LogDebug("CHttpServerApp::OnTimer(cur_time: %u, last clear time: %u, interval: %d)",
			//		(unsigned int)cur, (unsigned int)g_timeLastClearAsyncJob, g_nClearAsyncTimeInterval);
			DealAsyncTasks(cur);
			g_timeLastClearAsyncJob = cur;
		}
	}
	else
	{
		g_timeLastClearAsyncJob = cur;
	}

	//调整BUFFER大小
	AdjustAsnBuf(cur);

	//log统计信息
	LogStatisticInfo(cur);

	return;
}

void CHttpServerApp::DealSyncTasks(time_t cur)
{
	map<unsigned int, SyncReqInfo>::iterator it = m_mapSyncReqInfo.begin();
	while (it != m_mapSyncReqInfo.end())
	{
		if (cur > it->second.last_send_time)
		{
			if ((int)g_sleepTime > 0 && (int)cur - (int)it->second.last_send_time >= (int)g_sleepTime)
			{
				ReqJobQueryFieldList reqPacket;
				ReqJobQueryField *req_field = reqPacket.Append();

				req_field->jobId = it->second.job_id;
				req_field->step = g_nDefaultStep;
				AsnInt *field = req_field->field.Append();
				*field = JOB_FIELD_STATUS;

				SyncReqInfo &info = it->second;

				int ret = SendPacketToDCC(reqPacket, Ajs::reqJobQueryFieldListCid, info.uniq_id, it->second.ip, it->second.port);
				if (ret != 0)
				{
					SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, info.uniq_id);
					m_mapSyncReqInfo.erase(it++);
					continue;
				}
				info.status = SyncReqInfo::CREATE_SYNC_WAIT_RSP_QUERY_FIELD;
				info.last_send_time = g_nNowTime;

                AddToTimeoutQueue(info.uniq_id, g_jobServerTimeOut);
			}
		} 
		else
		{
			LogWarning("CHttpServerApp::DealSyncTasks(the time of system has been changed!) ");
			it->second.last_send_time = cur;
		}
		it++;
	}
}

void CHttpServerApp::DealAsyncTasks(time_t cur)
{
	map<unsigned int, AsyncReqInfo>::iterator it = m_mapAsyncReqInfo.begin();
	while (it != m_mapAsyncReqInfo.end())
	{
		if (cur > it->second.create_time)
		{
			if ((int)cur - (int)it->second.create_time >= (int)g_jobServerTimeOut)
			{
				LogWarning("CHttpServerApp::DealAsyncTasks(async job time out. flow id: %u, time_out: %d)", it->first, (int)(cur - it->second.create_time));
				m_mapAsyncReqInfo.erase(it++);
				continue;
			}
		}
		else
		{
			LogWarning("CHttpServerApp::DealAsyncTasks(the time of system has been changed!)");
			it->second.create_time = cur;
		}
		it++;
	}
}

void CHttpServerApp::AdjustAsnBuf(time_t cur)
{
	if (cur > m_adjustBufferLastTime)
	{
		if (m_adjustBufferTimeInterval > 0 && cur - m_adjustBufferLastTime >= m_adjustBufferTimeInterval)
		{
			AdjustAsnBufToDefault();
			m_adjustBufferLastTime = cur;
		}
		else
		{
			return;
		}
	} 
	m_adjustBufferLastTime = cur;
}

void CHttpServerApp::AdjustUniqueBuf(time_t cur)
{
    if(cur > m_adjustUniqueLastTime)
    {
        if( m_adjustUniqueTimeInterval > 0 && cur - m_adjustUniqueLastTime > m_adjustUniqueTimeInterval)
        {
            LogInfo("CHttpServerApp::AdjustUniqueBuf(before adjust, the size is : %d)", m_mapUniqueId.size());

            for (map<unsigned int, unsigned long long>::iterator it = m_mapUniqueId.begin(); it != m_mapUniqueId.end(); ++it)
            {
				if(m_mapSyncReqInfo.find(it->first) == m_mapSyncReqInfo.end() && m_mapAsyncReqInfo.find(it->first) == m_mapAsyncReqInfo.end())
                {
                    m_mapUniqueId.erase(it++);
                    continue;
                }
            }

            LogInfo("CHttpServerApp::AdjustUniqueBuf(after adjust, the size is : %d)", m_mapUniqueId.size());
            m_adjustUniqueLastTime = cur;
        }
        else
        {
            return;
        }
    }
    else
    {
        m_adjustUniqueLastTime = cur;
    }
}

void CHttpServerApp::LogStatisticInfo(time_t cur)
{
	if (m_logStatisticLastTime >= cur)
	{
		m_logStatisticLastTime = cur;
		return;
	}

	if (m_logStatisticTimeInterval > 0 && cur - m_logStatisticLastTime >= m_logStatisticTimeInterval)
	{
		m_pStatistic->AddStat("CcdRequests", 0, NULL, NULL, NULL, m_nCcdRequests);
		m_pStatistic->AddStat("DccReceives", 0, NULL, NULL, NULL, m_nDccReceives);

		m_pStatistic->AddStat("InvalidCcdRequest", 0, NULL, NULL, NULL, m_nInvalidCcdRequest);
		m_pStatistic->AddStat("InvalidDccReceives", 0, NULL, NULL, NULL, m_nInvalidDccReceives);

		m_pStatistic->AddStat("TimeOutedDccReceives", 0, NULL, NULL, NULL, m_nTimeOutedDccReceives);

		m_pStatistic->WriteToFile();
		m_pStatistic->ClearStat();

		m_logStatisticLastTime = cur;

		m_nCcdRequests = 0;
		m_nDccReceives = 0;
		m_nInvalidCcdRequest = 0;
		m_nInvalidDccReceives = 0;
		m_nTimeOutedDccReceives = 0;
	}
}

void CHttpServerApp::OnSignalUser1()
{
	LogInfo("RECEIVE SIGNAL USR1");
	LoadCfg();

	if (m_bPrintJobServerInfo)
	{
		PrintJobServerInfos();
		m_bPrintJobServerInfo = false;
		return;
	}

	// 初始化调度类
	m_pScheduler->Init(m_schedulerJobServer_set);

	return;
}

void CHttpServerApp::PrintJobServerInfos()
{
	map<unsigned short, set<int> > jobServerId2ClientModules;

	for(map<int, unsigned short>::iterator it = m_clientModuleSpecializedJobServer.begin();
			it != m_clientModuleSpecializedJobServer.end();
			it++){
		jobServerId2ClientModules[it->second].insert(it->first);
	}

	for(map<unsigned short, pair<unsigned int, unsigned short> >::iterator it = m_allJobServer_map.begin();
			it != m_allJobServer_map.end();
			it++){
		unsigned short job_server_id = it->first;
		unsigned int   job_server_ip = it->second.first;
		unsigned short job_server_port = it->second.second;
		string   job_server_ip_str = IpIntToString(job_server_ip);

		cout << "job_server_id:[" << job_server_id << "]\tjob_server_ip:[" << job_server_ip_str << "][" << job_server_ip << "]\tjob_server_port:[" << job_server_port << "]\tclient_modules:[";
		for (set<int>::iterator it1 = jobServerId2ClientModules[job_server_id].begin();
				it1 != jobServerId2ClientModules[job_server_id].end();
				it1++){
			if (it1 != jobServerId2ClientModules[job_server_id].begin())
				cout << ",";
			cout << *it1;
		}
		cout << "]" << endl;
	}
}

void CHttpServerApp::OnSignalUser2()
{
	LogInfo("RECEIVE SIGNAL USR2");
	//ProfilerStop();
	exit(0);
}

void CHttpServerApp::ReceiveDataDCC2MCD(AjsPacket &packet, unsigned int nIp, unsigned short nPort)
{
	m_nDccReceives++;

    unsigned int nUniqueId = packet.append;
	Ajs *body = packet.body;

	LogDebug("CHttpServerApp::ReceiveDataDCC2MCD(AjsPacket append id: %u)",
            nUniqueId);

    map<unsigned int, SyncReqInfo>::iterator syncIt = m_mapSyncReqInfo.find(nUniqueId);
    map<unsigned int, AsyncReqInfo>::iterator asyncIt = m_mapAsyncReqInfo.find(nUniqueId);
	switch(body->choiceId){
	case Ajs::rspJobCreateListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()) {
			//异步任务
			switch(asyncIt->second.type){
			case AsyncReqInfo::CREATE:
                RspCreate(body->rspJobCreateList, nIp, nPort, nUniqueId);
				break;
			case AsyncReqInfo::CREATE2:
                RspCreate2(body->rspJobCreateList, nIp, nPort, nUniqueId);
				break;
			case AsyncReqInfo::CREATE_MANY:
			case AsyncReqInfo::CREATE2_BATCH:
                RspCreateBatch(body->rspJobCreateList, nIp, nPort, nUniqueId);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobCreateListCid, async error type. ccd flow id: %u, type: %d)",
                                    nUniqueId, asyncIt->second.type);
			}
		} else if (syncIt != m_mapSyncReqInfo.end()){
			//同步任务
            DeleteFromTimeoutQueue(nUniqueId);
			if (syncIt->second.type == SyncReqInfo::CREATE_SYNC || syncIt->second.type == SyncReqInfo::CREATE_SYNC2)
                RspCreateSync2((void *)body->rspJobCreateList, nIp, nPort, nUniqueId, body->choiceId);
			else
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobCreateListCid, sync error type. ccd flow id: %u, type: %d)",
                                                    nUniqueId, syncIt->second.type);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobCreateListCid, can not find ccd flow in both map. ccd flow id: %u)",
                    nUniqueId);
		}
		break;
	case Ajs::rspJobSetActionListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
			switch (asyncIt->second.type){
			case AsyncReqInfo::SETACTION:
                RspSetAction(body->rspJobSetActionList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::SETACTION_BATCH:
                RspSetActionBatch(body->rspJobSetActionList, nUniqueId, nIp);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobSetActionListCid, async error type. ccd flow id: %u, type: %d)",
                                            nUniqueId, asyncIt->second.type);
			}
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobSetActionListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobUpdateListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
			switch (asyncIt->second.type){
			case AsyncReqInfo::UPDATE:
                RspUpdate(body->rspJobUpdateList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::UPDATE_BATHC:
                RspUpdateBatch(body->rspJobUpdateList, nUniqueId, nIp);
				break;
			}
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobUpdateListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobQueryAllListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()) {
			//异步任务
			switch(asyncIt->second.type){
			case AsyncReqInfo::QUERY:
                RspQuery(body->rspJobQueryAllList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::QUERY2:
                RspQuery2(body->rspJobQueryAllList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::QUERY2_BATCH:
                RspQueryBatch(body->rspJobQueryAllList, nUniqueId, nIp);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryAllListCid, async error type. ccd flow id: %u, type: %d)",
                                    nUniqueId, asyncIt->second.type);
			}
		} else if (syncIt != m_mapSyncReqInfo.end()){
			//同步任务
            DeleteFromTimeoutQueue(nUniqueId);
			if (syncIt->second.type == SyncReqInfo::CREATE_SYNC || syncIt->second.type == SyncReqInfo::CREATE_SYNC2)
                RspCreateSync2((void *)body->rspJobCreateList, nIp, nPort, nUniqueId, body->choiceId);
			else
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryAllListCid, sync error type. ccd flow id: %u, type: %d)",
                                nUniqueId, syncIt->second.type);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryAllListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobQueryFieldListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()) {
			//异步任务
			switch(asyncIt->second.type){
			case AsyncReqInfo::QUERY_FIELD:
                RspQueryField(body->rspJobQueryFieldList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::QUERY_FIELD_BATCH:
                RspQueryFieldBatch(body->rspJobQueryFieldList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::QUERY_TIME:
                RspQueryTime(body->rspJobQueryFieldList, nUniqueId, nIp);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryFieldListCid, async error type. ccd flow id: %u, type: %d)",
                                                    nUniqueId, asyncIt->second.type);
			}

		} else if (syncIt != m_mapSyncReqInfo.end()){
			//同步任务
            DeleteFromTimeoutQueue(nUniqueId);
			if (syncIt->second.type == SyncReqInfo::CREATE_SYNC || syncIt->second.type == SyncReqInfo::CREATE_SYNC2)
                RspCreateSync2((void *)body->rspJobCreateList, nIp, nPort, nUniqueId, body->choiceId);
			else
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryFieldListCid, sync error type. ccd flow id: %u, type: %d)",
                                nUniqueId, syncIt->second.type);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQueryFieldListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobQuerySelectCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
			switch(asyncIt->second.type){
			case AsyncReqInfo::QUERY_SELECT:
                RspQuerySelect(body->rspJobQuerySelect, nUniqueId, nIp);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQuerySelectCid, async error type. ccd flow id: %u, type: %d)",
                                                                    nUniqueId, asyncIt->second.type);
			}
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobQuerySelectCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobDeleteListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
			switch(asyncIt->second.type){
			case AsyncReqInfo::DELETE:
                RspDelete(body->rspJobDeleteList, nUniqueId, nIp);
				break;
			case AsyncReqInfo::DELETE_BATHC:
                RspDeleteBatch(body->rspJobDeleteList, nUniqueId, nIp);
				break;
			default:
				LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobDeleteListCid, async error type. ccd flow id: %u, type: %d)",
                                                                                    nUniqueId, asyncIt->second.type);
			}
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspJobDeleteListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspJobQueryAJSAgentCid:
        RspQueryAJSAgent(body->rspJobQueryAJSAgent, nUniqueId, nIp);
		break;
	case Ajs::rspJobQueryTSCAgentCid:
        RspQueryTSCAgent(body->rspJobQueryTSCAgent, nUniqueId, nIp);
		break;
	case Ajs::rspSerialJobCreateListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
            RspSequenceCreate(body->rspSerialJobCreateList, nUniqueId, nIp);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspSerialJobCreateListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspSerialJobEraseListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
            RspSequenceErase(body->rspSerialJobEraseList, nUniqueId, nIp);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspSerialJobEraseListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	case Ajs::rspSerialJobDeleteListCid:
		if (asyncIt != m_mapAsyncReqInfo.end()){
			//异步任务
            RspSequenceDelete(body->rspSerialJobDeleteList, nUniqueId, nIp);
		} else {
			LogWarning("CHttpServerApp::ReceiveDataDCC2MCD(Ajs::rspSerialJobDeleteListCid, can not find ccd flow in both map. ccd flow id: %u)",
                            nUniqueId);
		}
		break;
	default:
		break;
	}
}

void CHttpServerApp::ReceiveDataCCD2MCD(CHttpReqPkt &packet, 
		unsigned int nFlow,
		unsigned int nIp,
		unsigned short nPort)
{
	m_nCcdRequests++;
    LogLowest("CHttpServerApp::ReceiveDataCCD2MCD(flow %u, ip %u, port %u)", nFlow, nIp, nPort); 

	if (g_bAccessControl && !AccessControl(nIp, nPort)) {
		m_nInvalidCcdRequest++;
		string ip;
		IpIntToString(ip, nIp);
		LogWarning("CHttpServerApp::ReceiveDataCCD2MCD(unauthorized client ip: %s, flow id: %u)", ip.c_str(), nFlow);
		SendErrHttpRsp(ACCESS_DENIED, ACCESS_DENIED_REASON + "Ip Forbidden", nFlow);
		return;
	}

	if ((int)g_nHttpPacketMaxLen < packet.GetHeadLength() + packet.GetBodyLength()) {
		//HTTP包长度控制
		LogWarning("CHttpServerApp::ReceiveDataCCD2MCD(http packet is too long. flow id: %u)", nFlow);
		SendErrHttpRsp(ACCESS_DENIED, ACCESS_DENIED_REASON + "packet is too long", nFlow);
		return;
	}
	
	Json::Value request;

	int method = packet.GetMethod();
	if (method == HTTP_GET) {
		const char *qs = packet.GetQueryString();
		int qs_len = packet.GetQueryStringLength();

		if (CJsonHelper::toJson(qs, qs_len, request)) {
			//LogJsonObj(LOG_LEVEL_OBJ_DEBUG, request);
			HandleJsonRequest(request, nFlow);
		} else {
			LogJsonObj(LOG_LEVEL_LOWEST, request);
			SendErrHttpRsp(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON, nFlow);
		}
	} else if (method == HTTP_POST) {
		const char *body = packet.GetBodyContent();
		int body_len = packet.GetBodyLength();

		if (CJsonHelper::toJson(body, body_len, request)) {
			//LogJsonObj(LOG_LEVEL_OBJ_DEBUG, request);
			HandleJsonRequest(request, nFlow);
		} else {
			LogJsonObj(LOG_LEVEL_LOWEST, request);
			SendErrHttpRsp(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON, nFlow);
		}
	} else {
		LogDebug("CHttpServerApp::ReceiveDataCCD2MCD( INVALID METHOD )");
		SendErrHttpRsp(METHOD_NOT_ALLOWED, METHOD_NOT_ALLOWED_REASON, nFlow);
	}
}

void CHttpServerApp::ChildAction()
{
	LogDebug("CHttpServerApp::ChildAction()");
	m_logStatisticLastTime = g_nNowTime;
	m_adjustBufferLastTime = g_nNowTime;

	ReqJobServerLoad();
	assert(m_clientAuth->Start() == 0);
	//ProfilerStart("CPUProfile");
}

void CHttpServerApp::die_on_error(int x, char const *context)
{
    if (x < 0) {
        LogError("%s: %s", context, amqp_error_string2(x));
        return;
    }
}

void CHttpServerApp::die_on_amqp_error(amqp_rpc_reply_t x, char const *context)
{
    switch (x.reply_type) {
    case AMQP_RESPONSE_NORMAL:
        return;
  
    case AMQP_RESPONSE_NONE:
    	LogError("%s: missing RPC reply type!", context);
        break;
  
    case AMQP_RESPONSE_LIBRARY_EXCEPTION:
        LogError("%s: %s", context, amqp_error_string2(x.library_error));
        break;
  
    case AMQP_RESPONSE_SERVER_EXCEPTION:
        switch (x.reply.id) {
        case AMQP_CONNECTION_CLOSE_METHOD: {
            amqp_connection_close_t *m = (amqp_connection_close_t *) x.reply.decoded;
            LogError("%s: server connection error %uh, message: %.*s",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
            break;
        }
        case AMQP_CHANNEL_CLOSE_METHOD: {
            amqp_channel_close_t *m = (amqp_channel_close_t *) x.reply.decoded;
            LogError("%s: server channel error %uh, message: %.*s",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
            break;
        }
        default:
            LogError("%s: unknown server error, method id 0x%08X", context, x.reply.id);
            break;
        }
        break;
    }

    return;
}

void CHttpServerApp::TimeoutHandler()
{
	time_t cur_time = time(NULL);
	if (cur_time - m_analyzeRiskLastTime > m_analyzeRiskTimeInterval)
	{
		LogInfo("%s|%s|%d cur_time=%d, analyze_risk_last_time=%d, analyze_risk_time_interval=%d", __FILE__, __FUNCTION__, __LINE__, cur_time, m_analyzeRiskLastTime, m_analyzeRiskTimeInterval);
		m_analyzeRiskLastTime = cur_time;
		AnalyzeRisk();
	}
}

void CHttpServerApp::AnalyzeRisk()
{
	LogDebug("CHttpServerApp::AnalyzeRisk()");

	if (!m_riskPortTypeInfo.empty())
	{
		for (map<string, PortType>::iterator it = m_riskPortTypeInfo.begin(); it != m_riskPortTypeInfo.end(); ++it)
		{
			LogInfo("CHttpServerApp::AnalyzeRisk(). Risky info: ip: %s, port: %d, type %s, host: %s", it->first.c_str(), it->second.port, it->second.type.c_str(), it->second.hostname.c_str());
		}
	}

	if (!m_riskIpPortType.empty())
	{
		for (set<IpPortType>::iterator it = m_riskIpPortType.begin(); it != m_riskIpPortType.end(); ++it)
		{
			LogInfo("CHttpServerApp::AnalyzeRisk(). m_riskIpPortType. Risky info: ip: %s, port: %d, type %s, host: %s", (*it).ip.c_str(), (*it).port, (*it).type.c_str(), (*it).hostname.c_str());
		}
	}

	// if (!m_riskIpPortType.empty())
	// {
	// 	for (set<IpPortType>::iterator it = m_riskIpPortType.begin(); it != m_riskIpPortType.end(); ++it)
	// 	{
	// 		if (14700 == (*it).port || "tcp" == (*it).type)
	// 		{
	// 			LogInfo("Risk Alarm! Risky info: ip: %s, port: %d, type %s, host: %s", (*it).ip.c_str(), (*it).port, (*it).type.c_str(), (*it).hostname.c_str());
	// 		}
	// 	}
	// }

    LogInfo("Size of m_riskyPorts_s: %d", m_riskyPorts_set.size());
	if (!m_riskyPorts_set.empty())
	{
		for (set<unsigned int>::iterator iti = m_riskyPorts_set.begin(); iti != m_riskyPorts_set.end(); ++iti)
		{
			for (set<IpPortType>::iterator it = m_riskIpPortType.begin(); it != m_riskIpPortType.end(); ++it)
			{
				if ((*iti) == (*it).port)
				{
					LogInfo("Risk Alarm! Risky info: ip: %s, port: %d, type %s, host: %s", (*it).ip.c_str(), (*it).port, (*it).type.c_str(), (*it).hostname.c_str());
				}
			}
		}
	}

    LogInfo("Size of m_riskyServices_set: %d", m_riskyServices_set.size());
	if (!m_riskyServices_set.empty())
	{
		for (set<string>::iterator its = m_riskyServices_set.begin(); its != m_riskyServices_set.end(); ++its)
		{
			for (set<IpPortType>::iterator it = m_riskIpPortType.begin(); it != m_riskIpPortType.end(); ++it)
			{
				if ((*its) == (*it).type)
				{
					LogInfo("Risk Alarm! Risky info: ip: %s, port: %d, type %s, host: %s", (*it).ip.c_str(), (*it).port, (*it).type.c_str(), (*it).hostname.c_str());
				}
			}
		}
	}

	/*
    LogInfo("Size of m_whiteList_set: %d", m_whiteList_set.size());
    if (!m_whiteList_set.empty())
    {
        for (set<unsigned int>::iterator iti = m_whiteList_set.begin(); iti != m_whiteList_set.end(); ++iti)
        {
            int count = 0;
            for (set<IpPortType>::iterator it = m_riskIpPortType.begin(); it != m_riskIpPortType.end(); ++it)
            {
                if ((*iti) == (*it).port)
                {
                    count++;
                    // TODO: save to log file
                }
            }

            if ((unsigned int)((float)count / (float)g_nScanServerNum * 100) < g_nPortOpenPercent)
            {
                LogInfo("Risk Alarm! Port: %d is not open enough!", *iti);
            }
        }
    }
    */

    /*
    LogInfo("Size of m_IpPort_Host_map: %d", m_IpPort_Host_map.size());
    if (!m_IpPort_Host_map.empty())
    {
        for (map<string, set<string> >::iterator it = m_IpPort_Host_map.begin(); it != m_IpPort_Host_map.end(); ++it)
        {
            LogInfo("m_IpPort_Host_map: ipport: %s, hosts: %d", it->first.c_str(), it->second.size());
            if ((unsigned int)((float)it->second.size() / (float)g_nScanServerNum * 100) < g_nPortOpenPercent)
            {
                LogInfo("Risk Alarm! IP Port: %s is not open enough!", it->first.c_str());
                Json::Value fields;
                fields["object"] = it->first;
                fields["content"] = g_strAlarmMsg;
                fields["host"] = it->first;
                fields["id"] = g_nAlarmId;      // 白名单端口不通告警ID
                Json::FastWriter writer;
                string s_fields = writer.write(fields);
                PostUrl(g_strDefaultUrl, s_fields);
            }
        }

        m_IpPort_Host_map.clear();
    }
    */

    // curl multi
    if (!m_IpPort_Host_map.empty())
    {
        LogInfo("Size of m_IpPort_Host_map: %d", m_IpPort_Host_map.size());
        SendDataToRMQ();
        m_IpPort_Host_map.clear();
    }
}

void CHttpServerApp::PostUrl(string strurl, string strfields)
{
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);

    curl = curl_easy_init();
    if (curl)
    {
        curl_slist *plist = curl_slist_append(NULL, "Content-Type:application/json;charset=UTF-8");		// 指定发送内容的格式为JSON
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, plist);
        curl_easy_setopt(curl, CURLOPT_URL, strurl.c_str());				// 指定url
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, strfields.c_str());		// 指定post内容

        res = curl_easy_perform(curl);
        if (CURLE_OK != res)
        {
            LogError("CHttpServerApp::PostUrl(): CURLcode: %d", res);
        }
        curl_easy_cleanup(curl);
    }
}

void CHttpServerApp::CurlMInit(CURLM *cm, string str)
{
    CURL *eh = curl_easy_init();
    // curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, cb);
    curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
    curl_slist *plist = curl_slist_append(NULL, "Content-Type:application/json;charset=UTF-8");		// 指定发送内容的格式为JSON
    curl_easy_setopt(eh, CURLOPT_HTTPHEADER, plist);
    curl_easy_setopt(eh, CURLOPT_URL, g_strDefaultUrl.c_str());
    curl_easy_setopt(eh, CURLOPT_PRIVATE, g_strDefaultUrl.c_str());
    curl_easy_setopt(eh, CURLOPT_POSTFIELDS, str.c_str());
    curl_easy_setopt(eh, CURLOPT_VERBOSE, 0L);
    curl_multi_add_handle(cm, eh);
}

void CHttpServerApp::CurlMPrepare(CURLM *cm)
{
    for (map<string, set<string> >::iterator it = m_IpPort_Host_map.begin(); it != m_IpPort_Host_map.end(); ++it)
    {
        LogInfo("m_IpPort_Host_map: ipport: %s, hosts: %d", it->first.c_str(), it->second.size());
        if ((unsigned int)((float)it->second.size() / (float)g_nScanServerNum * 100) < g_nPortOpenPercent)
        {
            LogInfo("Risk Alarm! IP Port: %s is not open enough!", it->first.c_str());
            Json::Value fields;
            fields["object"] = it->first;
            fields["content"] = g_strAlarmMsg;
            fields["host"] = it->first;
            fields["id"] = g_nAlarmId;      // 白名单端口不通告警ID
            Json::FastWriter writer;
            string s_fields = writer.write(fields);
                
            CurlMInit(cm, s_fields);
        }
    }
}

void CHttpServerApp::CurlMPerform()
{
    CURLM *cm = NULL;
    CURL *eh = NULL;
    CURLMsg *msg = NULL;
    CURLcode return_code = CURLE_OK;
    int still_running = 0, msgs_left = 0;
    int http_status_code;
    const char *szUrl;

    curl_global_init(CURL_GLOBAL_ALL);

    cm = curl_multi_init();

    CurlMPrepare(cm);

    curl_multi_perform(cm, &still_running);

    do {
        int numfds = 0;
        int res = curl_multi_wait(cm, NULL, 0, MAX_WAIT_MSECS, &numfds);
        if (res != CURLM_OK)
        {
            LogError("CHttpServerApp::CurlMPerform() curl_multi_wait() returned: %d", res);
            return;
        }
        /*
        if (!numfds) {
           LogError("CHttpServerApp::CurlMPerform() curl_multi_wait() numfds = %d", numfds);
           return;
        }
        */
        curl_multi_perform(cm, &still_running);

    } while (still_running);

    while ((msg = curl_multi_info_read(cm, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE) {
            eh = msg->easy_handle;

            return_code = msg->data.result;
            if(return_code!=CURLE_OK) {
                LogError("CHttpServerApp::CurlMPerform() CURL error code: %d\n", msg->data.result);
                continue;
            }

            // Get HTTP status code
            http_status_code=0;
            szUrl = NULL;

            curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
            curl_easy_getinfo(eh, CURLINFO_PRIVATE, &szUrl);

            if (http_status_code == 200)
            {
                LogInfo("CHttpServerApp::CurlMPerform() 200 OK for %s", szUrl);
            } else {
                LogError("CHttpServerApp::CurlMPerform() GET of %s returned http status code %d", szUrl, http_status_code);
            }

            curl_multi_remove_handle(cm, eh);
            curl_easy_cleanup(eh);
        } else {
            LogError("CHttpServerApp::CurlMPerform() after curl_multi_info_read(), CURLMsg = %d", msg->msg);
        }
    }

    curl_multi_cleanup(cm);
}

void CHttpServerApp::SendDataToRMQ()
{
	if(m_IpPort_Host_map.empty())
	{
		return;
	}

	int status;
	amqp_socket_t *socket = NULL;
	amqp_connection_state_t conn;

	conn = amqp_new_connection();
	socket = amqp_tcp_socket_new(conn);
	if (!socket) {
		LogError("error occur amqp creating TCP socket");
	}

	LogError("Log to remove %s|%d|%s, host and port: %s | %d", __FILE__, __LINE__, __FUNCTION__, m_rmqhost.c_str(), m_rmqport);
	status = amqp_socket_open(socket, m_rmqhost.c_str(), m_rmqport);
	if (status) {
		LogError("error occur amqp opening TCP socket");
	}

	LogError("Log to remove %s|%d|%s, user and password: %s | %d", __FILE__, __LINE__, __FUNCTION__, m_rmquser.c_str(), m_rmqpwd.c_str());
	die_on_amqp_error(amqp_login(conn, m_rmqvhost.c_str(), AMQP_DEFAULT_MAX_CHANNELS, AMQP_DEFAULT_FRAME_SIZE, 0, AMQP_SASL_METHOD_PLAIN, m_rmquser.c_str(), m_rmqpwd.c_str()), "Logging in");
	amqp_channel_open(conn, 1);
	die_on_amqp_error(amqp_get_rpc_reply(conn), "Opening channel");

//	amqp_confirm_select(conn, 1);	/* turn publish confirm on */

    string data2send = "";
    unsigned int cnt = 0;
    for (map<string, set<string> >::iterator it = m_IpPort_Host_map.begin(); it != m_IpPort_Host_map.end(); ++it)
    {
        LogInfo("m_IpPort_Host_map: ipport: %s, hosts: %d", it->first.c_str(), it->second.size());
        if ((unsigned int)((float)it->second.size() / (float)g_nScanServerNum * 100) < g_nPortOpenPercent)
        {
            cnt++;
            LogInfo("Risk Alarm! IP Port: %s is not open enough!", it->first.c_str());

            if ("" != data2send)
            {
                data2send += "\n";
            }
            data2send += it->first;

            if (cnt % g_nCountToSend == 0)
            {
                SendDataToRMQ(conn, data2send);
                data2send = "";
            }
        }
    }
    SendDataToRMQ(conn, data2send);
    data2send = "";

	die_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS), "Closing channel");
	die_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS), "Closing connection");
	die_on_error(amqp_destroy_connection(conn), "Ending connection");
}

void CHttpServerApp::SendDataToRMQ(amqp_connection_state_t conn, string &data)
{
	if("" == data)
	{
		return;
	}

	char const *messagebody;
	messagebody = data.c_str();

	{
		amqp_basic_properties_t props;
		props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
		props.content_type = amqp_cstring_bytes("text/plain");
		props.delivery_mode = m_rmqdeliverymode;	// persistent delivery mode

    	die_on_error(amqp_basic_publish(conn,
                                    	1,
                                    	amqp_cstring_bytes(m_rmqexchange.c_str()),
                                    	amqp_cstring_bytes(m_rmqroutingkey.c_str()),
                                    	0,
                                    	0,
                                    	&props,
                                    	amqp_cstring_bytes(messagebody)),
                	"Publishing");
	}

	// die_on_amqp_error(amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS), "Closing channel");
	// die_on_amqp_error(amqp_connection_close(conn, AMQP_REPLY_SUCCESS), "Closing connection");
	// die_on_error(amqp_destroy_connection(conn), "Ending connection");
}

void CHttpServerApp::HandleJsonRequest(Json::Value &request, unsigned int nFlow)
{
	LogJsonObj(LOG_LEVEL_LOWEST, request);

	if (!request.isMember("cmd_type"))
	{
		LogWarning("CHttpServerApp::HandleJsonRequest(Json:: cmd_type is null, flow id: %u)", nFlow);
		SendErrHttpRsp(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'cmd_type', error: 'empty value'", nFlow);
		return;
	}

	string cmd_type = Trim(request["cmd_type"].asString());
	int ret = CheckCmdType(cmd_type);
	if (ret == -1)
	{
		LogWarning("CHttpServerApp::HandleJsonRequest(Json:: cmd_type is invalid, flow id: %u)", nFlow);
		SendErrHttpRsp(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'cmd_type', error: 'invalid value'", nFlow);
		return;
	}

	g_strProxyType = cmd_type;

	if (request["append"].isInt())
	{
		m_nNowAppend = request["append"].asUInt();
	}
	else
	{
		m_nNowAppend = 0;
	}

    m_nFlow = nFlow;
    m_nUniqueId = GetEmptyUniqueID(m_nFlow, m_nNowAppend);
    LogError("CHttpServerApp::HandleJsonRequest(flow %u, append %u, unique %u)", m_nFlow, m_nNowAppend, m_nUniqueId);

	LogDebug("CHttpServerApp::HandleJsonRequest(flow id: %u, cmd_type: %s, append: %u)", nFlow, cmd_type.c_str(), m_nNowAppend);

	// 端口监控
	if (cmd_type == "risky_port") {
		ReqRiskyPort(request, m_nUniqueId);
	} else if (cmd_type == "white_list") {
		ReqWhiteList(request, m_nUniqueId);
	} else if (cmd_type == "normal_port") {
		ReqNormalPort(request, m_nUniqueId);
	}
}

void CHttpServerApp::SendErrHttpRsp(int code, const string &reason, unsigned int &nFlow)
{
	Json::Value response;
	response["errno"] = code;
	response["error"] = reason;
	Json::StyledWriter writer;
	string rsp = writer.write(response);

	CHttpRspPkt http_pkg(r_code_200, r_reason_200, rsp.c_str(), rsp.length());
	SendPacketToCCD(http_pkg, nFlow);
	m_nInvalidCcdRequest++;
}

void CHttpServerApp::SendErrHttpRspByUniqueId(int code, const string &reason, unsigned int &nUniqueId)
{
    map<unsigned int, unsigned long long>::iterator it = m_mapUniqueId.find(nUniqueId);
    if(it == m_mapUniqueId.end())
    {
        LogError("CHttpServerApp::SendErrHttpRspByUniqueId(can not find unique id, unique id :%d)", nUniqueId);
        return;
    }

    unsigned int flow = int(it->second >> 32);
    SendErrHttpRsp(code, reason, flow);

    m_mapUniqueId.erase(it);
}

void CHttpServerApp::ReqRiskyPort(Json::Value &req, unsigned int nUniqueId)
{
	// to be deleted
	LogInfo("CHttpServerApp::ReqRiskyPort()");

	ResultInfo result_info;
	string iptmp;
	IpPortType ipth;

	try {

		if (req["json_job"].isNull())
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json::json_job is null, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];

		if (!request.isObject())
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json::json_job is not an object, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("id") && request["id"].isInt())
		{
			result_info.id = request["id"].asInt();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json:: id is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'id', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("ip") && request["ip"].isString())
		{
			result_info.ip = request["ip"].asString();		// 需转换为整型？
			iptmp = request["ip"].asString();
			ipth.ip = request["ip"].asString();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json:: ip is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'ip', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("port") && request["port"].isInt())
		{
			result_info.port = request["port"].asInt();
			m_riskPortTypeInfo[iptmp].port = request["port"].asInt();
			ipth.port = request["port"].asInt();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json:: port is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'port', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("type") && request["type"].isString())
		{
			result_info.type = request["type"].asString();
			m_riskPortTypeInfo[iptmp].type = request["type"].asString();
			ipth.type = request["type"].asString();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json:: type is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'type', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("host") && request["host"].isString())
		{
			result_info.host = request["host"].asString();
			m_riskPortTypeInfo[iptmp].hostname = request["host"].asString();
			ipth.hostname = request["host"].asString();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqRiskyPort(Json:: host is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'host', error: 'invalid value'", nUniqueId);
			return;
		}

		m_riskIpPortType.insert(ipth);

	} catch (exception &e) {
		LogWarning("CHttpServerApp::ReqRiskyPort(catch an exception, flow id: %u)", nUniqueId);
		SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON + e.what(), nUniqueId);
		return;
	}

	LogInfo("CHttpServerApp::ReqRiskyPort(). result_info: id: %d, ip: %s, port: %d, type %s, host: %s", result_info.id, result_info.ip.c_str(), result_info.port, result_info.type.c_str(), result_info.host.c_str());

	Json::Value response;
	response["errno"] = 0;
	response["error"] = "ok";
	SendHttpRspByUniqueId(response, nUniqueId);
}

void CHttpServerApp::ReqWhiteList(Json::Value &req, unsigned int nUniqueId)
{
	// to be deleted
	LogInfo("CHttpServerApp::ReqWhiteList()");

	try {

		if (req["json_job"].isNull())
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json::json_job is null, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];
		string ip_port;
		string hostname;

		if (!request.isObject())
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json::json_job is not an object, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("ip") && request["ip"].isString())
		{
			ip_port = request["ip"].asString();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json:: ip is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'ip', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("port") && request["port"].isInt())
		{
			ip_port += ":";
			ip_port += IntToString(request["port"].asInt());
		}
		else
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json:: port is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'port', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("host") && request["host"].isString())
		{
			hostname = request["host"].asString();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json:: host is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'host', error: 'invalid value'", nUniqueId);
			return;
		}

		m_IpPort_Host_map[ip_port].insert(hostname);

	} catch (exception &e) {
		LogWarning("CHttpServerApp::ReqWhiteList(catch an exception, flow id: %u)", nUniqueId);
		SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON + e.what(), nUniqueId);
		return;
	}

	Json::Value response;
	response["errno"] = 0;
	response["error"] = "ok";
	SendHttpRspByUniqueId(response, nUniqueId);
}

void CHttpServerApp::ReqNormalPort(Json::Value &req, unsigned int nUniqueId)
{
	ReqJobCreateList reqPacket;
	ReqJobCreate *packet = reqPacket.Append();

	packet->flag = 0;

	try {
		if (req["json_job"].isNull())
		{
            LogWarning("CHttpServerApp::ReqNormalPort(Json::json_job is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];

		if (!request.isObject())
		{
            LogWarning("CHttpServerApp::ReqNormalPort(Json::json_job is not an object, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'invalid value'", nUniqueId);
			return;
		}
	} catch (exception &e) {
        LogWarning("CHttpServerApp::ReqNormalPort(catch an exception, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON + e.what(), nUniqueId);
		return;
	}

	Json::Value response;
	response["errno"] = 0;
	response["error"] = "ok";
	SendHttpRspByUniqueId(response, nUniqueId);
}

bool CHttpServerApp::GetJobServerByClientModule(int client_module, pair<unsigned int, unsigned short> &server_info)
{

	map<int, unsigned short>::iterator it = m_clientModuleSpecializedJobServer.find(client_module);
	if ( it == m_clientModuleSpecializedJobServer.end() ){
		//进行调度
		if (!GetAJobServerInScheduler(server_info))
			return false;
	} else {
		//client_module对应的专用jobserver
		unsigned short job_server_id = it->second;
		map<unsigned short, pair<unsigned int, unsigned short> >::iterator it1 = m_allJobServer_map.find(job_server_id);
		if (it1 != m_allJobServer_map.end()){
			server_info.first = it1->second.first;
			server_info.second = it1->second.second;
		} else {
			LogWarning("CHttpServerApp::GetJobServerByClientModule(invalid job_server_id, client_module: %d)", client_module);
			return false;
		}
	}
	return true;
}

bool CHttpServerApp::GetAJobServerInScheduler(pair<unsigned int, unsigned short> & host)
{
	LogLowest("CHttpServerApp::GetAJobServerInScheduler()");
	unsigned short job_server_id;
	if (m_pScheduler->GetAJobServer(job_server_id)){
		map<unsigned short, pair<unsigned int, unsigned short> >::iterator it1 = m_allJobServer_map.find(job_server_id);
		if (it1 != m_allJobServer_map.end()){
			host.first = it1->second.first;
			host.second = it1->second.second;
		} else {
			LogDebug("CHttpServerApp::GetAJobServerInScheduler(invalid job_server_id)");
			return false;
		}

	} else {
		LogDebug("CHttpServerApp::GetAJobServerInScheduler(there is no job server)");	
		return false;
	}
	return true;
}

bool CHttpServerApp::GetAllJobServerInScheduler(vector<pair<unsigned int, unsigned short> > & hosts)
{
	LogLowest("CHttpServerApp::GetAllJobServerInScheduler()");
	hosts.clear();

	vector<unsigned short> job_servers_id;
	if(m_pScheduler->GetAllJobServer(job_servers_id)){
		for (size_t i = 0; i != job_servers_id.size(); i++){
			unsigned int job_server_id = job_servers_id[i];
			pair<unsigned int, unsigned short> host;
			host.first = m_allJobServer_map[job_server_id].first;
			host.second = m_allJobServer_map[job_server_id].second;
			hosts.push_back(host);
		}
		return true;
	}
	return false;
}

bool CHttpServerApp::ClientModuleAuth(int client_module, const string &password)
{
	CAutoLock lock(m_lock);
	map<int, UserInfo>::iterator it = m_clientAuth_map.find(client_module);
	if (it != m_clientAuth_map.end()){
		if (it->second.passwd == password && it->second.enable)
			return true;
	}
	return false;
}

void CHttpServerApp::UpdateModuleAuth(const map<int, UserInfo> &infos)
{
	CAutoLock lock(m_lock);
	m_clientAuth_map.clear();
	m_clientAuth_map.insert(infos.begin(), infos.end());
}

bool CHttpServerApp::GetJobServerInfoById(unsigned short job_server_id, pair<unsigned int, unsigned short> &info)
{
	LogLowest("CHttpServerApp::GetJobServerInfoById()");
	map<unsigned short, pair<unsigned int, unsigned short> >::iterator it = m_allJobServer_map.find(job_server_id);
	if ( it != m_allJobServer_map.end()) {
		info.first = it->second.first;
		info.second = it->second.second;
	} else {
		LogLowest("CHttpServerApp::GetJobServerInfoById(job_server_id is invalid: %d)", job_server_id);
		return false;
	}
	return true;
}

void CHttpServerApp::SendHttpRsp(const Json::Value& response, unsigned int nFlow )
{
	//LogJsonObj(LOG_LEVEL_LOWEST, response);
	Json::StyledWriter writer;
	string rsp = writer.write(response);

	PrintStr("CHttpServerApp::SendHttpRsp(response data: ", rsp, g_nLogStrLen, nFlow);

	CHttpRspPkt http_pkg(r_code_200, r_reason_200, rsp.c_str(), rsp.length());
	SendPacketToCCD(http_pkg, nFlow);

	//将客户的请求信息删除
	//m_mapAsyncReqInfo.erase(nFlow);

	LogDebug("CHttpServerApp::SendHttpRsp(send http response ok. flow id: %u, response data len: %u)",
			nFlow, (unsigned int)rsp.length());
}

void CHttpServerApp::SendHttpRspByUniqueId(const Json::Value& response, unsigned int nUniqueId )
{
    map<unsigned int, unsigned long long>::iterator it = m_mapUniqueId.find(nUniqueId);
    if(it == m_mapUniqueId.end())
    {
        LogError("CHttpServerApp::SendHttpRspByUniqueId(can not find unique id, unique id :%d)", nUniqueId);
        return;
    }

    unsigned int flow = (unsigned int)(it->second >> 32);
    LogError("CHttpServerApp::SendHttpRspByUniqueId(flow :%d append %d)", flow, (unsigned int)(it->second));
    SendHttpRsp(response, flow);

    m_mapUniqueId.erase(it);
}


void CHttpServerApp::RspCreate(const RspJobCreateList* packet,
        unsigned int nIp,
        unsigned short nPort,
        unsigned int nUniqueId)
{

	RspJobCreate* it = packet->First();
	Json::Value response;
	int ret = (int)it->ret;

	response["errno"] = (int)ret;
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;

	if (ret == 0){
		int job_id = (int)it->jobId;
		unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		response["error"] = ObjToString<unsigned long long>(return_id);
		response["job_id"] = ObjToString<unsigned long long>(return_id);
	} else {
		response["error"] = ConvertErrorNoToStr(ret);
		response["job_id"] = "0";
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
    LogDebug("CHttpServerApp::RspCreate(flow id: %u)", nUniqueId);
}

void CHttpServerApp::RspCreate2(const RspJobCreateList* packet,
        unsigned int nIp,
        unsigned short nPort,
        unsigned int nUniqueId)
{

	RspJobCreate* it = packet->First();
	Json::Value response;
	int ret = (int)it->ret;

	response["errno"] = (int)ret;
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;

	if (ret == 0){
		int job_id = (int)it->jobId;
		unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		response["error"] = "";
		response["job_id"] = ObjToString<unsigned long long>(return_id);
	} else {
		response["error"] = ConvertErrorNoToStr(ret);
		response["job_id"] = "0";
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
    LogDebug("CHttpServerApp::RspCreate2(flow id: %u)", nUniqueId);
}

void CHttpServerApp::SendQueryAll(unsigned int nFlow){
    map<unsigned int, SyncReqInfo>::iterator it = m_mapSyncReqInfo.find(nFlow);
    if (it != m_mapSyncReqInfo.end()){
        ReqJobQueryAllList reqPacket;
        ReqJobQueryAll* packet = reqPacket.Append();

        packet->jobId = it->second.job_id;
        packet->step = 0;

        int ret = SendPacketToDCC(
                reqPacket,
                Ajs::reqJobQueryAllListCid,
                nFlow,
                it->second.ip,
                it->second.port
                );

        if (ret != 0){
            LogWarning("CHttpServerApp::SendQueryAll(SendPacketToDCC failed! flow id: %u, server ip: %s, server port: %hu)", 
                    nFlow, IpIntToString(it->second.port).c_str(), it->second.port);
            SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nFlow);
            return;
        }
        
        it->second.status = SyncReqInfo::CREATE_SYNC_WAIT_RSP_QUERY_FIELD;
        LogDebug("CHttpServerApp::SendQueryAll(ok. flow id: %u, server ip: %s, server port: %hu)", 
                nFlow, IpIntToString(it->second.ip).c_str(), it->second.port);

    } else {
        LogWarning("CHttpServerApp::SendQueryAll(can not find element in m_mapSyncReqInfo, flow id: %u)", nFlow);
    }
}

void
CHttpServerApp::RspCreateSync2(const void* packet,
        unsigned int nIp,
        unsigned short nPort,
        unsigned int nUniqueId,
        enum Ajs::ChoiceIdEnum nType)
{
    SyncReqInfo &info = m_mapSyncReqInfo[nUniqueId];
	unsigned int nJobId = 0;
	int nStep = 0;

	switch(nType){
		case Ajs::rspJobCreateListCid:
			//创建任务返回
			{
				if (info.status != SyncReqInfo::CREATE_SYNC_WAIT_RSP_CREATE){
					//状态错误
                    LogWarning("CHttpServerApp::RspCreateSync2(status error. return type: rspJobCreateListCid, now status: %d, flow id: %u)", info.status, nUniqueId);
                    SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				}

				RspJobCreateList* p_pkt = (RspJobCreateList*)packet;
				RspJobCreate* rsp_sigle = p_pkt->First();
				if(rsp_sigle == NULL){
                    LogWarning("CHttpServerApp::RspCreateSync2(Ajs::rspJobCreateListCid, RspJobCreate pointer is NULL,flow id: %u)", nUniqueId);
                    SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				}

				int ret = (int)rsp_sigle->ret;
				if (ret != 0){
                    LogWarning("CHttpServerApp::RspCreateSync2(Ajs::rspJobCreateListCid, CreateList failed, flow id: %u)", nUniqueId);
					Json::Value response;
					response["errno"] = (int)ret;
					response["error"] = ConvertErrorNoToStr(ret);
					response["job_id"] = "0";
                    response["append"] = m_mapSyncReqInfo[nUniqueId].append;
                    SendHttpRspByUniqueId(response, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				} else {
					int job_id = (int)rsp_sigle->jobId;

                    info.ip = nIp;
                    info.port = nPort;
                    info.job_id = job_id;

					info.status = SyncReqInfo::CREATE_SYNC_WAIT_REQ_QUERY_FIELD;
                    LogDebug("CHttpServerApp::RspCreateSync2(Ajs::rspJobCreateListCid, flow id: %u, job id: %u)",nUniqueId, job_id);
				}
				break;
			}
		case Ajs::rspJobQueryFieldListCid:
			//查询状态返回 
			{
				if (info.status != SyncReqInfo::CREATE_SYNC_WAIT_RSP_QUERY_FIELD){
					//状态错误
                    LogWarning("CHttpServerApp::RspCreateSync2(status error. return type: rspJobQueryFieldListCid, now status: %d, flow id: %u)", info.status, nUniqueId);
                    SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				}

				RspJobQueryFieldList* p_pkg = (RspJobQueryFieldList*)packet;
				RspJobQueryField* rsp_sigle = p_pkg->First();

				if(rsp_sigle == NULL){
                    LogWarning("CHttpServerApp::RspCreateSync2(Ajs::rspJobQueryFieldListCid, RspJobQueryField pointer is NULL, flow id: %u)", nUniqueId);
                    SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				}

				int ret = (int)rsp_sigle->ret;
				if (ret != 0){
					//查询状态失败
                    LogWarning("CHttpServerApp::RspCreateSync2(Ajs::rspJobQueryFieldListCid, QueryField failed, flow id: %u)", nUniqueId);

					int job_id = (int)rsp_sigle->jobId;
					nJobId = job_id;
					nStep = g_nDefaultStep;

                    SendQueryAll(nUniqueId);
					return;
				} else {
					//查询状态成功
					QueryFieldResultSingle* res_single = rsp_sigle->data.First();
					int status = (int) (res_single->retNum->value);
					int job_id = (int)rsp_sigle->jobId;
					
					if ( status == JOB_STATUS_RUNNING ) {
						//任务还在运行中，一段时间后继续查询状态
						info.status = SyncReqInfo::CREATE_SYNC_WAIT_REQ_QUERY_FIELD;

					} else {
						//任务状态不为RUNNING，立即查询所有结果
						nJobId = job_id;
						nStep = g_nDefaultStep;

                        SendQueryAll(nUniqueId);
					}
                    LogDebug("CHttpServerApp::RspCreateSync2(Ajs::rspJobQueryFieldListCid, QueryField ok, flow id: %u, status: %d, job id: %d)", nUniqueId, status, job_id);
				}
				break;
			}
		case Ajs::rspJobQueryAllListCid:
			//查询所有结果返回 
			{
				RspJobQueryAllList* p_pkg = (RspJobQueryAllList*)packet;
				RspJobQueryAll* rsp_sigle = p_pkg->First();

				if(rsp_sigle == NULL){
                    LogWarning("CHttpServerApp::RspCreateSync2(Ajs::rspJobQueryAllListCid, RspJobQueryAll pointer is NULL, flow id: %u)", nUniqueId);
                    SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
                    m_mapSyncReqInfo.erase(nUniqueId);
					return;
				}

				int ret = (int)rsp_sigle->ret;
				int job_id = (int)rsp_sigle->jobId;
				unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
				unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
                
                //查询失败
                Json::Value response;
                response["errno"] = (int)ret;
                response["append"] = m_mapSyncReqInfo[nUniqueId].append;
                string error = (const char*)rsp_sigle->error;
                response["job_id"] = ObjToString<unsigned long long>(return_id);
                if (info.type == SyncReqInfo::CREATE_SYNC2){
                	response["error"] = error.empty() ? ConvertErrorNoToStr(ret) : error;
                    FillQueryResult(response["result"], rsp_sigle, nIp);
                } else
                    FillQueryResult(response["error"], rsp_sigle, nIp);
                SendHttpRspByUniqueId(response, nUniqueId);
                m_mapSyncReqInfo.erase(nUniqueId);

                LogDebug("CHttpServerApp::RspCreateSync2(Ajs::rspJobQueryAllListCid, QueryAll ok, flow id: %u, job id: %llu)", nUniqueId, js_id);
				break;
			}
		default:
            LogWarning("CHttpServerApp::RspCreateSync2(default, flow id: %u)", nUniqueId);
			break;
	}
}

void 
CHttpServerApp::FillQueryResult(Json::Value& root, const RspJobQueryAll* pSeq, unsigned int nIp)
{
	int jobId = (int) pSeq->jobId;
	int flag = (int)pSeq->flag;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)jobId;
	root["job_id"] = ObjToString<unsigned long long>(return_id);;
	root["status"] = ConvertStatusIntToStr((int) pSeq->status);
	root["step_now"] = (int) pSeq->stepNow;
	root["step_all"] = (int) pSeq->stepAll;
	root["run_mode"] = (flag & AJS_FLAG_RUN_DEBUG_NOT_CONTINUE) ? "debug" : "continue";
	root["delete_mode"] = ( flag & AJS_FLAG_DELETE_AUTO_NOT_MANUAL) ? "auto" : "manual";
	root["now_run"] = ( flag & AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL) ? "immediate" : "wait_set_action";
	root["create_time"] = ConvertUnixTimeToStr((int) pSeq->createTime).c_str();
	root["begin_time"] = ConvertUnixTimeToStr((int) pSeq->beginTime).c_str();
	root["last_running_time"] = ConvertUnixTimeToStr((int) pSeq->lastRunningTime).c_str();
	root["end_time"] = ConvertUnixTimeToStr((int) pSeq->endTime).c_str();
	root["client_module"] = (int) pSeq->clientModule;
	root["signal"] = (int) pSeq->userSignal;
	root["author"] = (const char*) pSeq->author;
	root["query_key"] = (const char*) pSeq->queryKey;
	root["job_type"] = (const char*) pSeq->jobType;
	root["job_desc"] = (const char*) pSeq->jobDesc;
	root["data"] = (const char*) pSeq->userData;
	root["step_desc"] = (const char*) pSeq->stepDesc;
	root["step_info"] = (const char*) pSeq->stepInfo;
	root["step_time"] = (int) pSeq->stepTime;
	root["exit_code"] = (int) pSeq->exitCode;
	root["out"] = (const char*) pSeq->stdOut;
	root["err"] = (const char*) pSeq->stdErr;
	root["file_trans_schedule"] = pSeq->fileTransSchedule / (float)100;
	root["file_trans_total_size"] = (int) pSeq->fileTransTotalSize;
}

void
CHttpServerApp::RspSetAction(const RspJobSetActionList* packet,
        unsigned int nUniqueId, unsigned int nIp)
{
	RspJobSetAction* rspSetAction = packet->First();
    unsigned int flow = nUniqueId;

	if(rspSetAction == NULL){
		LogWarning("CHttpServerApp::RspSetAction(RspJobSetAction pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspSetAction->ret;
	int jobId = (int)rspSetAction->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)jobId;

	Json::Value response;
	response["errno"] = ret;
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;
	response["job_id"] = ObjToString<unsigned long long>(return_id);
	if (ret != 0 )
		response["error"] = ConvertErrorNoToStr(ret);
	else response["error"] = "";

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspSetAction(ok. flow id: %u, job id: %d)", flow, jobId);
}

void
CHttpServerApp::RspUpdate(const RspJobUpdateList* packet, unsigned int nUniqueId, unsigned int nIp)
{
	RspJobUpdate* rspUpdate = packet->First();
    unsigned int flow = nUniqueId;

	if(rspUpdate == NULL){
		LogWarning("CHttpServerApp::RspUpdate(RspUpdate pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspUpdate->ret;
	int jobId = (int)rspUpdate->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)jobId;

	Json::Value response;
	response["errno"] = ret;
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;
	response["job_id"] = ObjToString<unsigned long long>(return_id);;
	if (ret != 0 )
		response["error"] = ConvertErrorNoToStr(ret);
	else response["error"] = "";

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
    LogDebug("CHttpServerApp::RspUpdate(ok. flow id: %u, client id: %u, job id: %d)", flow, nUniqueId, jobId);
}

void
CHttpServerApp::RspQuery(const RspJobQueryAllList* packet, unsigned int nUniqueId, unsigned int nIp)
{
	RspJobQueryAll* rspQueryAll = packet->First();
    unsigned int flow = nUniqueId;

	if(rspQueryAll == NULL){
		LogWarning("CHttpServerApp::RspQuery(RspQuery pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspQueryAll->ret;
	int job_id = (int)rspQueryAll->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)job_id;

	Json::Value result;
	if (ret != 0) {
		//查询失败
		result["errno"] = ret;
		string error = (const char*)rspQueryAll->error;
		result["error"] = error.empty() ? ConvertErrorNoToStr(ret) : error;
		result["job_id"] = ObjToString<unsigned long long>(return_id);;
	} else {
		//查询成功，返回结果

		result["errno"] = 0;
		result["job_id"] = ObjToString<unsigned long long>(return_id);;
		FillQueryResult(result["error"], rspQueryAll, nIp);
	}
    result["append"] = m_mapAsyncReqInfo[nUniqueId].append;
    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQuery(ok. flow id: %u, job id: %d)", flow, job_id);
}

void
CHttpServerApp::RspQuery2(const RspJobQueryAllList* packet, unsigned int nUniqueId, unsigned int nIp)
{
	RspJobQueryAll* rspQueryAll = packet->First();
    unsigned int flow = nUniqueId;

	if(rspQueryAll == NULL){
		LogWarning("CHttpServerApp::RspQuery2(RspQuery2 pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspQueryAll->ret;
	int job_id = (int)rspQueryAll->jobId;


	Json::Value result;
	result["errno"] = (int)ret;
	if (ret != 0) {
		//查询失败
		string error = (const char*)rspQueryAll->error;
		result["error"] = error.empty() ? ConvertErrorNoToStr(ret) : error;
	} else {
		//查询成功，返回结果
		result["error"] = "";
	}
    result["append"] = m_mapAsyncReqInfo[nUniqueId].append;
	FillQueryResult(result["result"], rspQueryAll, nIp);
    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQuery2(ok. flow id: %u, job id: %d)", flow, job_id);
}

void
CHttpServerApp::RspQueryField(const RspJobQueryFieldList* packet, unsigned int nUniqueId, unsigned int nIp)
{
	RspJobQueryField* rspQueryField = packet->First();
    unsigned int flow = nUniqueId;

	if(rspQueryField == NULL){
		LogWarning("CHttpServerApp::RspQueryField(RspQueryField pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspQueryField->ret;
	int job_id = (int)rspQueryField->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)job_id;

	Json::Value result;
	result["errno"] = (int)ret;
	result["error"] = ret == 0 ? "":ConvertErrorNoToStr(ret);
	result["job_id"] = ObjToString<unsigned long long>(return_id);
    result["append"] = m_mapAsyncReqInfo[nUniqueId].append;

	QueryFieldResultSingle* it = rspQueryField->data.First();
	rspQueryField->data.SetCurrToFirst();
	while(it != NULL){
		switch(it->choiceId){
			case QueryFieldResultSingle::retNumCid:
				{
					int field = (int)(it->retNum->field);
					string name = ConvertFieldInt2Str(field);
					int retNum = (int)(it->retNum->value);
					if(name == "flag"){
						result["now_run"] = (retNum & AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL )?"immediate":"wait_set_action";
						result["delete_mode"] = (retNum & AJS_FLAG_DELETE_AUTO_NOT_MANUAL )?"auto":"manual";
						result["run_mode"] = (retNum & AJS_FLAG_RUN_DEBUG_NOT_CONTINUE )?"debug":"continue";
					} else {
						if ( name == "file_trans_schedule")
							result[name] = retNum/(float)100;
						else if ( name == "status" )
							result[name] = ConvertStatusIntToStr(retNum);
						else if ( name == "create_time" || name == "begin_time" || name == "last_running_time"
								|| name == "end_time"){
							result[name] = ConvertUnixTimeToStr((int) retNum).c_str();
						}
						else result[name] = retNum;
					}
					break;
				}
			case QueryFieldResultSingle::retStrCid:
				{
					int field = (int)(it->retNum->field);
					string retStr = (const char*)(it->retStr->value);
					string name = ConvertFieldInt2Str(field);
					result[name] = retStr;
					break;
				}
			default:
				break;
		}
		it = rspQueryField->data.GoNext();
	}

    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQueryField(ok. flow id: %u, job id: %d)", flow, job_id);
}

void
CHttpServerApp::RspQuerySelect(RspJobQuerySelect* packet, unsigned int nUniqueId, unsigned int nIp)
{
    unsigned int flow = nUniqueId;

	if(packet == NULL){
		LogWarning("CHttpServerApp::RspQuerySelect(RspQuerySelect pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)packet->ret;

	Json::Value result;
	if (ret != 0) {
		result["errno"] = ret;
		result["error"] = ConvertErrorNoToStr(ret);
	} else {
		result["errno"] = 0;
		result["error"] = "";
		result["total_count"] = (int)packet->totalCount;

		RspJobQueryAll* it = packet->workResultList.First();
		packet->workResultList.SetCurrToFirst();
		while(it != NULL){
			Json::Value job;
			FillQueryResult(job, it, nIp);
			result["job_list"].append(job);

			it = packet->workResultList.GoNext();
		}
	}
    result["append"] = m_mapAsyncReqInfo[nUniqueId].append;
    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQueryField(ok. flow id: %u)", flow);
}

void
CHttpServerApp::RspDelete(const RspJobDeleteList* packet, unsigned int nUniqueId, unsigned int nIp)
{
	RspJobDelete* rspDelete = packet->First();
    unsigned int flow = nUniqueId;

	if(rspDelete == NULL){
		LogInfo("CHttpServerApp::RspDelete(RspDelete pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspDelete->ret;
	int job_id = (int)rspDelete->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)job_id;

	Json::Value result;
	if (ret != 0) {
		result["errno"] = (int)ret;
		result["error"] = ConvertErrorNoToStr(ret);
		result["job_id"] = ObjToString<unsigned long long>(return_id);
	} else {
		result["errno"] = 0;
		result["error"] = "";
		result["job_id"] = ObjToString<unsigned long long>(return_id);
	}

    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);

	LogDebug("CHttpServerApp::RspDelete(ok. flow id: %u)", flow);
}

/*
void
CHttpServerApp::RspQuerySys(RspJobQuerySys* packet, unsigned int id, unsigned int nIp) 
{
	if(packet == NULL){
		LogDebug("CHttpServerApp::RspQuerySys(RspQuerySys pointer is NULL)");
		m_mapAsyncReqInfo.erase(id);
		return;
	}

	AsnInt* it = packet->First(); 
	packet->SetCurrToFirst();
	while (it != NULL){
		unsigned short port = m_pScheduler->GetJSPort(nIp);
		//m_pJobManager->SetServerInfoByJobId((int)(*it), pair<unsigned int, unsigned short>(nIp, port));

		it = packet->GoNext();
	}
	m_mapAsyncReqInfo.erase(id);
}
*/

void CHttpServerApp::RspCreateBatch(RspJobCreateList* packet,
        unsigned int nIp,
        unsigned short nPort,
        unsigned int nUniqueId
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobCreate *it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspCreateBatch( RspJobCreateList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	while (it != NULL){
		Json::Value ele;
		int ret = (int)it->ret;
		ele["errno"] = ret;
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;

		if (ret == 0){
			int job_id = (int)it->jobId;
			unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
			ele["error"] = "";
			ele["job_id"] = ObjToString<unsigned long long>(return_id);
		} else {
			ele["error"] = ConvertErrorNoToStr(ret);
			ele["job_id"] = "0";
		}

		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspCreate2Batch(ok! flow id: %u)", flow);
}

void CHttpServerApp::RspSetActionBatch(RspJobSetActionList* packet,
        unsigned int nUniqueId,
        unsigned int ip
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobSetAction* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspSetActionBatch( RspJobSetActionList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[ip];
	while (it != NULL){
		Json::Value ele;
		int ret = (int)it->ret;
		ele["errno"] = ret;
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;
		int job_id = (int)it->jobId;
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		ele["job_id"] = ObjToString<unsigned long long>(return_id);

		if (ret == 0)
			ele["error"] = "";
		else
			ele["error"] = ConvertErrorNoToStr(ret);


		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspSetActionBatch(ok! flow id: %u)", flow);

}

void CHttpServerApp::RspUpdateBatch(RspJobUpdateList* packet,
        unsigned int nUniqueId,
        unsigned int ip
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobUpdate* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspUpdateBatch( RspJobUpdateList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[ip];
	while (it != NULL){
		Json::Value ele;
		int ret = (int)it->ret;
		ele["errno"] = ret;
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;
		int job_id = (int)it->jobId;
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		ele["job_id"] = ObjToString<unsigned long long>(return_id);

		if (ret == 0)
			ele["error"] = "";
		else
			ele["error"] = ConvertErrorNoToStr(ret);


		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspUpdateBatch(ok! flow id: %u)", flow);

}

void CHttpServerApp::RspQueryBatch(RspJobQueryAllList* packet,
        unsigned int nUniqueId,
        unsigned int ip
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobQueryAll* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspQueryBatch( RspJobQueryAllList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[ip];
	while (it != NULL){
		Json::Value ele;
		int ret = (int)it->ret;
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;
		ele["errno"] = ret;
		int job_id = (int)it->jobId;
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		ele["job_id"] = ObjToString<unsigned long long>(return_id);

		if (ret == 0)
			ele["error"] = "";
		else {
			string error = (const char *)it->error;
			ele["error"] = error.empty()? ConvertErrorNoToStr(ret) : error;
		}
		FillQueryResult(ele["result"], it, ip);

		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQueryBatch(ok! flow id: %u)", flow);

}

void CHttpServerApp::RspQueryFieldBatch(RspJobQueryFieldList* packet,
        unsigned int nUniqueId,
        unsigned int ip
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobQueryField* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspQueryFieldBatch( RspJobQueryFieldList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[ip];
	while (it != NULL){
		Json::Value ele;

		{
			int ret = (int)it->ret;
			int job_id = (int)it->jobId;
			unsigned long long return_id = (js_id << 56) + (unsigned)job_id;

			ele["errno"] = (int)ret;
            ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;
			ele["error"] = ret == 0 ? "":ConvertErrorNoToStr(ret);
			ele["job_id"] = ObjToString<unsigned long long>(return_id);

			QueryFieldResultSingle* it1 = it->data.First();
			it->data.SetCurrToFirst();
			while(it1 != NULL){
				switch(it1->choiceId){
					case QueryFieldResultSingle::retNumCid:
						{
							int field = (int)(it1->retNum->field);
							string name = ConvertFieldInt2Str(field);
							int retNum = (int)(it1->retNum->value);
							if(name == "flag"){
								ele["now_run"] = (retNum & AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL )?"immediate":"wait_set_action";
								ele["delete_mode"] = (retNum & AJS_FLAG_DELETE_AUTO_NOT_MANUAL )?"auto":"manual";
								ele["run_mode"] = (retNum & AJS_FLAG_RUN_DEBUG_NOT_CONTINUE )?"debug":"continue";
							} else {
								if ( name == "file_trans_schedule")
									ele[name] = retNum/(float)100;
								else if ( name == "status" )
									ele[name] = ConvertStatusIntToStr(retNum);
								else if ( name == "create_time" || name == "begin_time" || name == "last_running_time"
										|| name == "end_time"){
									ele[name] = ConvertUnixTimeToStr((int) retNum).c_str();
								}
								else ele[name] = retNum;
							}
							break;
						}
					case QueryFieldResultSingle::retStrCid:
						{
							int field = (int)(it1->retNum->field);
							string retStr = (const char*)(it1->retStr->value);
							string name = ConvertFieldInt2Str(field);
							ele[name] = retStr;
							break;
						}
					default:
						break;
				}
				it1 = it->data.GoNext();
			}
		}

		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQueryFieldBatch(ok! flow id: %u)", flow);
}

void CHttpServerApp::RspDeleteBatch(RspJobDeleteList* packet,
        unsigned int nUniqueId,
        unsigned int ip
        ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspJobDelete* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspDeleteBatch( RspJobDeleteList pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[ip];
	while (it != NULL){
		Json::Value ele;
		int ret = (int)it->ret;
		ele["errno"] = ret;
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;
		int job_id = (int)it->jobId;
		unsigned long long return_id = (js_id << 56) + (unsigned)job_id;
		ele["job_id"] = ObjToString<unsigned long long>(return_id);

		if (ret == 0)
			ele["error"] = "";
		else
			ele["error"] = ConvertErrorNoToStr(ret);

		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspDeleteBatch(ok! flow id: %u)", flow);
}

void CHttpServerApp::RspSequenceCreate(RspSerialJobCreateList* packet,
            unsigned int nUniqueId,
            unsigned int ip
            ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspSerialJobCreate* it = packet->First();

	if (it == NULL){
		LogWarning("CHttpServerApp::RspSequenceCreate(RspSerialJobCreate pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	response["errno"] = (int)it->ret;
	response["error"] = (const char*)it->error;
	response["sequence_id"] = IntToString((int)it->taskId);
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspSequenceCreate(ok! flow id: %u", flow);
}

void CHttpServerApp::RspSequenceErase(RspSerialJobEraseList* packet,
                unsigned int nUniqueId,
                unsigned int ip
                ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspSerialJobErase* it = packet->First();

	if (it == NULL){
		LogWarning("CHttpServerApp::RspSequenceErase(RspSerialJobErase pointer is NULL, flow id: %u)",
                flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	response["errno"] = (int)it->ret;
	response["error"] = (const char*)it->error;
	response["sequence_id"] = IntToString((int)it->taskId);
    response["append"] = m_mapAsyncReqInfo[nUniqueId].append;

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspSequenceErase(ok! flow id: %u", flow);
}

void CHttpServerApp::RspSequenceDelete(RspSerialJobDeleteList* packet,
                unsigned int nUniqueId,
                unsigned int ip
                ) {

    unsigned int flow = nUniqueId;
	Json::Value response;
	RspSerialJobDelete* it = packet->First();
	packet->SetCurrToFirst();

	if (it == NULL) {
		LogWarning(
				"CHttpServerApp::RspSequenceDelete(RspSerialJobErase pointer is NULL, flow id: %u)",
				flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON,
				flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	while (it != NULL) {
		Json::Value ele;
		ele["errno"] = (int) it->ret;
		ele["error"] = (const char*) it->error;
		ele["sequence_id"] = IntToString((int) it->taskId);
        ele["append"] = m_mapAsyncReqInfo[nUniqueId].append;

		response.append(ele);
		it = packet->GoNext();
	}

    SendHttpRspByUniqueId(response, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspSequenceDelete(ok! flow id: %u", flow);
}

void CHttpServerApp::RspQueryTime(const RspJobQueryFieldList* packet,
        unsigned int nUniqueId,
        unsigned int nIp){
	RspJobQueryField* rspQueryField = packet->First();
    unsigned int flow = nUniqueId;

	if(rspQueryField == NULL){
		LogWarning("CHttpServerApp::RspQueryTime(RspQueryField pointer is NULL, flow id: %u)", flow);
		SendErrHttpRsp(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, flow);
        m_mapAsyncReqInfo.erase(nUniqueId);
		return;
	}

	int ret = (int)rspQueryField->ret;
	int job_id = (int)rspQueryField->jobId;
	unsigned long long js_id = (unsigned long long)m_jobServerIp2Id_map[nIp];
	unsigned long long return_id = (js_id << 56) + (unsigned)job_id;

	Json::Value result;
	result["errno"] = (int)ret;
	result["error"] = ret == 0 ? "":ConvertErrorNoToStr(ret);
	result["job_id"] = ObjToString<unsigned long long>(return_id);
    result["append"] = m_mapAsyncReqInfo[nUniqueId].append;

	QueryFieldResultSingle* it = rspQueryField->data.First();
	rspQueryField->data.SetCurrToFirst();
	if (it != NULL){
		int field = (int) (it->retNum->field);
		int retNum = (int) (it->retNum->value);

		if (field != JOB_FIELD_STEP_TIME2){
			LogWarning("CHttpServerApp::RspQueryTime(field is not JOB_FIELD_STEP_TIME2, job id: %u)", job_id);
		}

		int flag = (retNum & 0x80000000) >> 31;
		if (flag == 0){
			result["type"] = "ajs step";
			result["ajs_time"] = (int)(retNum & 0x7FFFFFFF);
		} else {
			result["type"] = "tsc step";
			result["tsc_time"] = (int)(retNum & 0x001FFFFF);
			result["ajs_time"] = (int)((retNum & 0x7FE00000) >> 21);
		}
		LogDebug("CHttpServerApp::RspQueryTime(job id: %u, field: %d, ret num: %d)",  field, retNum);
	} else {
		LogWarning("CHttpServerApp::RspQueryTime(field is NULL, flow id: %u)", flow);
	}

    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
	LogDebug("CHttpServerApp::RspQueryTime(ok. flow id: %u, job id: %d)", flow, job_id);

}

void CHttpServerApp::RspQueryAJSAgent(RspJobQueryAJSAgent* packet,
        unsigned int nUniqueId,
        unsigned int ip){

	int ret = packet->ret;

	Json::Value result;
	result["errno"] = ret;
	result["error"] = "QUERY AJS AGENT";
	if ( ret == 0 ) {
		Json::Value ipInfoList, ipInfo;
		OnlineInfo* info =  packet->details.First();
		packet->details.SetCurrToFirst();
		while (info != NULL){
			int agentId = (int)info->agentId;
			int online = (int)info->online;
			ipInfo["ip"] = IpIntToString(agentId);
			ipInfo["online"] = online;
			ipInfoList.append(ipInfo);
			info = packet->details.GoNext();
		}
		result["ip_info"] = ipInfoList;
	}

    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
    LogDebug("CHttpServerApp::RspQueryTSCAgent(ok. flow id: %u)", nUniqueId);
}

void CHttpServerApp::RspQueryTSCAgent(RspJobQueryTSCAgent* packet,
        unsigned int nUniqueId,
        unsigned int ip){

	int ret = packet->ret;

	Json::Value result;
	result["errno"] = ret;
	result["error"] = "QUERY TSC AGENT";
	if ( ret == 0 ) {
		Json::Value ipInfoList, ipInfo;
		OnlineInfo* info =  packet->details.First();
		packet->details.SetCurrToFirst();
		while (info != NULL){
			int agentId = (int)info->agentId;
			int online = (int)info->online;
			ipInfo["ip"] = IpIntToString(agentId);
			ipInfo["online"] = online;
			ipInfoList.append(ipInfo);
			info = packet->details.GoNext();
		}
		result["ip_info"] = ipInfoList;
	}

    SendHttpRspByUniqueId(result, nUniqueId);
    m_mapAsyncReqInfo.erase(nUniqueId);
    LogDebug("CHttpServerApp::RspQueryTSCAgent(ok. flow id: %u)", nUniqueId);
}

unsigned int CHttpServerApp::GetEmptyUniqueID(unsigned int nClientId, unsigned int nSessionId) {
    static unsigned int g_nNowUniqueId = (unsigned int)time(0);

    map<unsigned int, unsigned long long>::iterator result;
    do {
        ++g_nNowUniqueId;
        if (g_nNowUniqueId == 0)
            ++g_nNowUniqueId;
    } while ((result = m_mapUniqueId.find(g_nNowUniqueId)) != m_mapUniqueId.end());

    m_mapUniqueId[g_nNowUniqueId] = ((unsigned long long)nClientId << 32) | (nSessionId);

    return g_nNowUniqueId;
}

int CHttpServerApp::SendPacketToCCD(CHttpRspPkt& packet, unsigned int nFlow)
{
	int ret = SendDataMCD2CCD(packet.Head(), packet.Length(), nFlow);
	if (ret != 0){
		LogWarning("CHttpServerApp::SendPacketToCCD(SendDataMCD2CCD() failed)");
		return ret;
	}
	return 0;
}

template<typename Type> 
int CHttpServerApp::SendPacketToDCC(Type& obj, enum Ajs::ChoiceIdEnum nType, unsigned int nAppend, unsigned int nIp, unsigned short nPort)
{
	LogAsnObj(LOG_LEVEL_LOWEST, obj);
	AjsPacket jobPacket;

	jobPacket.version = 4;
	jobPacket.append = nAppend;
	jobPacket.body = new Ajs;
	jobPacket.body->choiceId = nType;
	jobPacket.body->reqJobCreateList = (ReqJobCreateList*)&obj;

	int ret = SendDataMCD2DCC(jobPacket, nIp, nPort);
	jobPacket.body->reqJobCreateList = NULL;

	if ( ret != 0){
		LogWarning("send packet to dcc failed! append: %u, server_ip: %s)", nAppend, IpIntToString(nIp).c_str());
		return ret;
	}

	LogDebug("CHttpServerApp::SendPacketToDCC(send packet to dcc ok! append: %u, server_ip: %s, server_port: %hu)",
			nAppend, IpIntToString(nIp).c_str(), nPort);

	return 0;
}

extern "C" {
	tfc::cache::CacheProc* create_app() {
		return new CHttpServerApp();
	}
}
