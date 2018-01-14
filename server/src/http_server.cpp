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
	if (cfgFile.Load(szFile) != 0){
		LogError("CHttpServerApp::LoadCfg(config file load error)");
		exit(EXIT_FAILURE);
	}

	LogFile(cfgFile.GetIni("log_file"));
	unsigned int log_file_max_num = StringToInt(cfgFile.GetIni("log_file_max_num"));
	int log_file_max_size = StringToInt(cfgFile.GetIni("log_file_max_size"));
//	SetLogInfo(StringToInt(cfgFile.GetIni("log_file_max_num")), StringToInt(cfgFile.GetIni("log_file_max_size")));
	SetLogInfo(log_file_max_num, log_file_max_size);
	LogInfo("CHttpServerApp::LoadCfg(log_file_max_num :%u, log_file_max_size :%d)", log_file_max_num, log_file_max_size);
	LogLevel(StringToLogLevel(cfgFile.GetIni("log_level")));
	LogInfo("CHttpServerApp::LoadCfg(log_file :%s)", cfgFile.GetIni("log_file").c_str());

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
	if (IpStringToInt(g_nSerialServerIp, strSerialIp) != 0) {
		LogWarning(
				"CHttpServerApp::LoadCfg(serial_server_ip in config file is error, serial_server_ip: %s)",
				strSerialIp.c_str());
		//exit(0);
	}
	//g_nSerialServerIp = IpStringToInt(cfgFile.GetIni("serial_server_ip"));
	g_nSerialServerPort = StringToInt(cfgFile.GetIni("serial_server_port"));

	//客户端valid ip
	{
		string valid_ips = cfgFile.GetIni("valid_ip_info", "");
		LogInfo("CHttpServerApp::LoadCfg(valid_ip_info :%s)", valid_ips.c_str());

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
		LogInfo("CHttpServerApp::LoadCfg(js_server_info :%s)", valid_ips.c_str());

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
						LogInfo("CHttpServerApp::LoadCfg( job_server id: %s, ip: %s, port: %s, client_module: %d)",
								info_v[0].c_str(), info_v[1].c_str(), info_v[2].c_str(), client_module);
					}
					continue;
				} else if (info_v.size() == 3){
					//参加调度的jobserver
					m_schedulerJobServer_set.insert(jobServerId);
					LogInfo("CHttpServerApp::LoadCfg( job_server id: %s, ip: %s, port: %s)",
							info_v[0].c_str(), info_v[1].c_str(), info_v[2].c_str());
				}

			}
			else
				LogError("CHttpServerApp::LoadCfg(invalid conf job server ip : %s)", v_ip_vct[i].c_str());
		}

	}

	// risky ports
	{
		string risky_ports = cfgFile.GetIni("risky_ports", "");
		LogInfo("CHttpServerApp::LoadCfg(risky_ports :%s)", risky_ports.c_str());

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
		LogInfo("CHttpServerApp::LoadCfg(risky_services :%s)", risky_services.c_str());

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
        LogInfo("CHttpServerApp::LoadCfg(white_list :%s)", white_list.c_str());

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

	else if (cmd_type == "create" || cmd_type == "create2" || cmd_type == "create_sync2" || cmd_type == "create_sync") {

        ReqCreate(request, m_nUniqueId);
	} else if (cmd_type == "op") {

        ReqSetAction(request, m_nUniqueId);
	} else if (cmd_type == "update") {

        ReqUpdate(request, m_nUniqueId);
	} else if (cmd_type == "query" || cmd_type == "query2") {

        ReqQuery(request, m_nUniqueId);
	} else if (cmd_type == "query_field") {
		
        ReqQueryField(request, m_nUniqueId);
	} else if (cmd_type == "query_select") {

        ReqQuerySelect(request, m_nUniqueId);
	} else if (cmd_type == "delete") {

        ReqDelete(request, m_nUniqueId);
	} else if ( cmd_type == "create2_batch") {

        ReqCreateBatch(request, m_nUniqueId);
	} else if ( cmd_type == "op_batch") {

        ReqSetActionBatch(request, m_nUniqueId);
	} else if ( cmd_type == "update_batch") {

        ReqUpdateBatch(request, m_nUniqueId);
	} else if ( cmd_type == "query2_batch") {

        ReqQueryBatch(request, m_nUniqueId);
	} else if ( cmd_type == "query_field_batch") {

        ReqQueryFieldBatch(request, m_nUniqueId);
	} else if ( cmd_type == "delete_batch") {

        ReqDeleteBatch(request, m_nUniqueId);
	} else if ( cmd_type == "sequence_create") {

        ReqSequenceCreate(request, m_nUniqueId);
	} else if ( cmd_type == "sequence_erase") {

        ReqSequenceErase(request, m_nUniqueId);
	} else if ( cmd_type == "sequence_delete") {

        ReqSequenceDelete(request, m_nUniqueId);
	} else if ( cmd_type == "create_many") {

        ReqCreateMany(request, m_nUniqueId);
	} else if ( cmd_type == "query_time") {

        ReqQueryTime(request, m_nUniqueId);
	} else if ( cmd_type == "query_ajs_agent") {

        ReqQueryAJSAgent(request, m_nUniqueId);
	} else if ( cmd_type == "query_tsc_agent") {

        ReqQueryTSCAgent(request, m_nUniqueId);
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

	ConfirmInfo confirm_info;

	try {

		if (req["json_job"].isNull())
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json::json_job is null, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];

		if (!request.isObject())
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json::json_job is not an object, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'json_job', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("id") && request["id"].isInt())
		{
			confirm_info.id = request["id"].asInt();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json:: id is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'id', error: 'invalid value'", nUniqueId);
			return;
		}

		if (request.isMember("stat") && request["stat"].isInt())
		{
			confirm_info.stat = request["stat"].asInt();
		}
		else
		{
			LogWarning("CHttpServerApp::ReqWhiteList(Json:: stat is invalid, flow id: %u)", nUniqueId);
			SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param: 'stat', error: 'invalid value'", nUniqueId);
			return;
		}

	} catch (exception &e) {
		LogWarning("CHttpServerApp::ReqWhiteList(catch an exception, flow id: %u)", nUniqueId);
		SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON + e.what(), nUniqueId);
		return;
	}

	LogInfo("CHttpServerApp::ReqWhiteList(). confirm_info: id: %d, stat: %d", confirm_info.id, confirm_info.stat);

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

void CHttpServerApp::ReqCreate(Json::Value &req, unsigned int nUniqueId)
{
	ReqJobCreateList reqPacket;
	ReqJobCreate *packet = reqPacket.Append();

	packet->flag = 0;

	try{

		if (req["json_job"].isNull())
		{
            LogWarning("CHttpServerApp::ReqCreate(Json:: json_job is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];

		if (!request.isObject())
		{
            LogWarning("CHttpServerApp::ReqCreate(Json:: json_job is not a object, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'invalid value'", nUniqueId);
			return;
		}

		// 检查client_module
		if (request.isMember("client_module") && request["client_module"].isInt())
		{
			packet->clientModule = request["client_module"].asInt();
		}
		else
		{
            LogWarning("CHttpServerApp::ReqCreate(Json:: client_module is invalid, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_module',error:'invalid value'", nUniqueId);
			return;
		}

		string passwd;
		if (request.isMember("client_passwd") && request["client_passwd"].isString())
		{
			passwd = request["client_passwd"].asString();
		}
		else
		{
            LogWarning("CHttpServerApp::ReqCreate(Json:: client_passwd is invalid, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_passwd',error:'invalid value'", nUniqueId);
			return;
		}


		if (!ClientModuleAuth((int)(packet->clientModule), passwd))
		{
            LogWarning("CHttpServerApp::ReqCreate(client_passwd is not correct, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(ACCESS_DENIED, ACCESS_DENIED_REASON + "param:'client_passwd',error:'incorrect password'", nUniqueId);
			return;
		}

		// 检查run_mode
		if (request["run_mode"].isString())
		{ 
			string run_mode = Trim(request["run_mode"].asString());
			int ret = CheckRunModule(run_mode);
			if (ret != -1)
			{
				packet->flag = (int)(packet->flag) | ret;
			}
			else
			{
                LogWarning("CHttpServerApp::ReqCreate(run_mode is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'run_mode',error:'invalid value'", nUniqueId);
				return;
			}
		}
		else if (!request["run_mode"].isNull())
		{
            LogWarning("CHttpServerApp::ReqCreate(run_mode is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'run_mode',error:'invalid value'", nUniqueId);
			return;
		}

		// 检查delete_mode
		if (request["delete_mode"].isString())
		{ 
			string delete_mode = Trim(request["delete_mode"].asString());
			int ret = CheckDeleteModule(delete_mode);
			if (ret != -1)
			{
				packet->flag = (int)(packet->flag) | ret;
			}
			else
			{
                LogWarning("CHttpServerApp::ReqCreate(delete_mode is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'delete_mode',error:'invalid value'", nUniqueId);
				return;
			}
		}
		else if (!request["delete_mode"].isNull())
		{
            LogWarning("CHttpServerApp::ReqCreate(delete_mode is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON +  "param:'delete_mode',error:'invalid value'", nUniqueId);
			return;
		}

		// 检查now_run
		if (request["now_run"].isString())
		{ 
			string now_run = Trim(request["now_run"].asString());
			int ret = CheckNowRun(now_run);
			if (ret != -1)
			{
				packet->flag = (int)(packet->flag) | ret;
			}
			else
			{
                LogWarning("CHttpServerApp::ReqCreate(now_run is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'now_run',error:'invalid value'", nUniqueId);
				return;
			}
		}
		else if (!request["now_run"].isNull())
		{
            LogWarning("CHttpServerApp::ReqCreate(now_run is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'now_run',error:'invalid value'", nUniqueId);
			return;
		}
		else
		{
			packet->flag = (int)(packet->flag) | AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL; 
		}

		if (g_strProxyType == "create_sync" || g_strProxyType == "create_sync2")
		{
			packet->flag = (int)(packet->flag) | AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL;
			packet->flag = (int)(packet->flag) | AJS_FLAG_CREATE_SYNC;
		}

		if (request["signal"].isInt())
			packet->userSignal = request["signal"].asInt();
		else
			packet->userSignal = g_nDefaultSignal;	

		if (request["author"].isString())
			packet->author = request["author"].asString().c_str();
		else 
			packet->author = "";

		if (request["query_key"].isString())
			packet->queryKey = request["query_key"].asString().c_str();
		else 
			packet->queryKey = "";

		if (request["job_type"].isString())
			packet->jobType = request["job_type"].asString().c_str();
		else
			packet->jobType = "";

		if (request["job_desc"].isString())
			packet->jobDesc = request["job_desc"].asString().c_str();
		else
			packet->jobDesc = "";

		if (request["data"].isString())
			packet->userData = request["data"].asString().c_str();
		else 
			packet->userData = "";

		//检查step_list
		if (request["step_list"].isNull()) { 
            LogWarning("CHttpServerApp::ReqCreate(step_list is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list',error:'empty value'", nUniqueId);
			return;
		} else if (request["step_list"].isArray()) {
			Json::Value &step_list = request["step_list"];

			if (step_list.size() == 0) {
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list',error:'empty value'", nUniqueId);
				return;
			} else {
				for (Json::Value::ArrayIndex i = 0; i != step_list.size(); ++i) {
					Json::Value &step = step_list[i];
					JobStepSingle *sigle = packet->stepList.Append();

					// 检查type
					if (!step.isObject()) {
                        LogWarning("CHttpServerApp::ReqCreate(step %u is not object, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list',error:'invalid value'", nUniqueId);
						return;
					} else if (step["type"].isNull()) {
                        LogWarning("CHttpServerApp::ReqCreate('type' of step %u is null, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list type',error:'empty value'", nUniqueId);
						return;
					} else if (step["type"].isInt()) {
						int type = step["type"].asInt();
						if (0 == type) {
							// 执行命令
							unsigned int nCmdIp = 0;
							if (step["ip"].isString()) {
								string strTemp = step["ip"].asString();
								if (IpStringToInt(nCmdIp, strTemp) != 0) {
									nCmdIp = StringToInt(strTemp);
									string strTemp2 = IntToString(nCmdIp);
									if (strTemp != strTemp2)
										nCmdIp = 0;
								}
								if (nCmdIp == 0) {
                                    LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list ip',error:'invalid value'", nUniqueId);
									return;
								}

							} else {
                                LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list ip',error:'value is not string'", nUniqueId);
								return;
							}

							sigle->choiceId = JobStepSingle::jobStepShellCmdCid; 
							sigle->jobStepShellCmd = new JobStepShellCmd;
							JobStepShellCmd *cmd = sigle->jobStepShellCmd;
							cmd->flag = 0;

							cmd->agentId = nCmdIp;

							if (step["time_out"].isInt())
								cmd->timeout = step["time_out"].asInt();
							else
								cmd->timeout = g_nDefaultTimeout;

							if (step["user"].isString())
								cmd->execUser = step["user"].asString().c_str();
							else 
								cmd->execUser = g_strDefaultUser.c_str();

							if (step["not_log"].isInt()) {
								int not_log = step["not_log"].asInt();
								int ret = CheckNotLog(not_log);
								if (ret != -1) {
									cmd->flag = (int)(cmd->flag) | ret;
								} else {
                                    LogWarning("CHttpServerApp::ReqCreate('not_log' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list not_log',error:'value is not int'", nUniqueId);
									return;
								}
							}

							if (step["not_getout"].isInt()) {
								int not_getout = step["not_getout"].asInt();
								int ret = CheckNotGetOut(not_getout);
								if (ret != -1) {
									cmd->flag = (int)(cmd->flag) | ret;
								} else {
                                    LogWarning("CHttpServerApp::ReqCreate('not_getout' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list not_getout',error:'value is not int'", nUniqueId);
									return;
								}
							} 

							if (step["not_geterr"].isInt()) {
								int not_geterr = step["not_geterr"].asInt();
								int ret = CheckNotGetOut(not_geterr);
								if (ret != -1) {
									cmd->flag = (int)(cmd->flag) | ret;
								} else {
                                    LogWarning("CHttpServerApp::ReqCreate('not_geterr' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list not_geterr',error:'value is not int'", nUniqueId);
									return;
								}
							} 

							if (step["buf_size"].isInt()) {
								int nBufSize = step["buf_size"].asInt();
								if (g_nMaxStdOut < nBufSize ) {
                                    LogWarning("CHttpServerApp::ReqCreate('buf_size' of step %u is greater than 6M, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list buf_size',error:'value is too large(>6M)'", nUniqueId);
									return;
								}
								cmd->bufSize = nBufSize;
							} else {
								cmd->bufSize = g_nDefaultBufSize;
							}

							if (step["cmd"].isString())
								cmd->cmd = step["cmd"].asString().c_str();
							else 
								cmd->cmd = "";

							if (step["err_deal"].isInt())
								cmd->errDeal = step["err_deal"].asInt();
							else
								cmd->errDeal = g_nDefaultErrDeal;

							if (step["info"].isString())
								cmd->info = step["info"].asString().c_str();
							else
								cmd->info = "";

							if (step["desc"].isString())
								cmd->desc = step["desc"].asString().c_str();
							else
								cmd->desc = "";


						} else if (1 == type) {
							// 传文件
							sigle->choiceId = JobStepSingle::jobStepFileSvrCid; 
							sigle->jobStepFileSvr = new JobStepFileSvr;
							JobStepFileSvr *file = sigle->jobStepFileSvr;
							file->flag = 0;

							if (step["src_ip"].isString()) {
								string src_ip = Trim(step["src_ip"].asString());
								unsigned int ip;
								int nCmdIp = -1;
								if (IpStringToInt(ip ,src_ip) != 0) {
									nCmdIp = StringToInt(src_ip);
									string strTemp2 = IntToString(nCmdIp);
									if (src_ip != strTemp2)
										nCmdIp = 0;
									else
										ip = (unsigned)nCmdIp;
								}

								if (nCmdIp == 0) {
                                    LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list src_ip',error:'invalid ip'", nUniqueId);
									return;
								} else 
									file->srcAgentId = ip;

							} else {
                                LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list src_ip',error:'value is not string'", nUniqueId);
								return ;
							}

							if (step["src_path"].isString()) {
								file->srcFile = step["src_path"].asString().c_str();
							} else {
                                LogWarning("CHttpServerApp::ReqCreate('src_path' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list src_path',error:'empty value'", nUniqueId);
								return;
							}

							if (step["dest_ip"].isString()) {
								string dest_ip = Trim(step["dest_ip"].asString());
								unsigned int ip;
								int nCmdIp = -1;
								if (IpStringToInt(ip ,dest_ip) != 0) {
									nCmdIp = StringToInt(dest_ip);
									string strTemp2 = IntToString(nCmdIp);
									if (dest_ip != strTemp2)
										nCmdIp = 0;
									else
										ip = (unsigned)nCmdIp;
								}

								if (nCmdIp == 0) {
                                    LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list dest_ip',error:'invalid ip'", nUniqueId);
									return;
								} else {
									file->dstAgentId = ip;
								}
							} else {
                                LogWarning("CHttpServerApp::ReqCreate('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list dest_ip',error:'value is not string'", nUniqueId);
								return;
							}

							if (step["dest_path"].isString()) {
								file->dstPath = step["dest_path"].asString().c_str();
							} else {
                                LogWarning("CHttpServerApp::ReqCreate('dest_path' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list dest_path',error:'empty value'", nUniqueId);
								return;
							}

							if (step["desc"].isString())
								file->desc = step["desc"].asString().c_str();
							else 
								file->desc = "";

							if (step["info"].isString())
								file->info = step["info"].asString().c_str();
							else
								file->info = "";

							if (step["err_deal"].isInt())
								file->errDeal = step["err_deal"].asInt();
							else
								file->errDeal = g_nDefaultErrDeal;

							if (step["time_out"].isInt())
								file->timeout = step["time_out"].asInt();
							else
								file->timeout = g_nDefaultTimeout;
                            
                            if (step["out_max_con_retry"].isInt()) {
								int ret = CheckFileOutMaxConRetry(step["out_max_con_retry"].asInt());
								if (ret != -1)
									file->flag = (int)(file->flag) | ret;
							}
                            
						} else {
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list type',error:'invalid value'", nUniqueId);
							return;
						}

					} else {
                        LogWarning("CHttpServerApp::ReqCreate('type' of step %u is not int, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list type',error:'invalid value'", nUniqueId);
						return;
					}
				}
			}
		} else {
            LogWarning("CHttpServerApp::ReqCreate(step_list is not a array, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'step_list',error:'value is not array'", nUniqueId);
			return;
		}
	} catch (exception &e) {
        LogWarning("CHttpServerApp::ReqCreate(catch a exception, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + e.what(), nUniqueId);
		return;
	}

	// 发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (GetJobServerByClientModule((int)packet->clientModule, server_info))
	{
        if (g_strProxyType == "create_sync" || g_strProxyType == "create_sync2")
        {
            SyncReqInfo info;
            info.ccd_flow = m_nFlow;
            info.append = m_nNowAppend;
            info.uniq_id = m_nUniqueId;
            info.status = SyncReqInfo::CREATE_SYNC_WAIT_RSP_CREATE;
            info.ip = server_info.first;
            info.port = server_info.second;
            info.last_send_time = g_nNowTime;

            if (g_strProxyType == "create_sync")
                info.type = SyncReqInfo::CREATE_SYNC;
            else
            	info.type = SyncReqInfo::CREATE_SYNC2;

            m_mapSyncReqInfo[nUniqueId] = info;
            // 同步任务需要加入超时队列
            AddToTimeoutQueue(nUniqueId, g_jobServerTimeOut);
        }
        else
        {
            AsyncReqInfo info;
            info.ccd_flow = m_nFlow;
            info.append = m_nNowAppend;
            info.uniq_id = m_nUniqueId;
            info.create_time = g_nNowTime;

            if (g_strProxyType == "create")
            	info.type = AsyncReqInfo::CREATE;
            else
            	info.type = AsyncReqInfo::CREATE2;
            
            m_mapAsyncReqInfo[nUniqueId] = info;
        }

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobCreateListCid, nUniqueId, server_info.first, server_info.second);
		if (ret != 0)
		{
            LogWarning("CHttpServerApp::ReqCreate(SendPacketToDCC failed! flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
            if (g_strProxyType == "create_sync" || g_strProxyType == "create_sync2")
            {
            	// 如果同步任务发送失败，需要清楚信息
                m_mapSyncReqInfo.erase(nUniqueId);
                DeleteFromTimeoutQueue(nUniqueId);
            }
            else 
            {
            	m_mapAsyncReqInfo.erase(nUniqueId);
            }
			
			return;
		}
	} else {
        LogWarning("CHttpServerApp::ReqCreate(get job server by client module failed!, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	}
    LogDebug("CHttpServerApp::ReqCreate(create ok! flow id: %u)", nUniqueId);
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

void CHttpServerApp::ReqSetAction(Json::Value &request, unsigned int nUniqueId)
{
	unsigned short js_id = 0;
    unsigned int nJobId = 0;
    int nNewStep = 0;
    int nAction = 0;

	if( request["job_id"].isString() ){
		string strJobId = request["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)){
            LogWarning("CHttpServerApp::ReqSetAction(trans job_id failed, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short)(ullJobId >> 56);
		nJobId = (int)ullJobId ;
	} else {
        LogWarning("CHttpServerApp::ReqSetAction(job_id is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
		return;
	}

	if(request["op"].isString()){
		string op = Trim(request["op"].asString());
		int ret = CheckOp(op);
		if (ret != -1){
			nAction = ret;
		} else {
            LogWarning("CHttpServerApp::ReqSetAction(op is invalid, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'op',error:'invalid value'", nUniqueId);
			return;
		}
	}else {
        LogWarning("CHttpServerApp::ReqSetAction(op is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'op',error:'invalid value'", nUniqueId);
		return;
	}

	if (request["new_step"].isInt()){
		nNewStep = request["new_step"].asInt();
	} else 
		nNewStep = 0;

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqSetAction(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

	//将json转换为asn格式并发送到dcc
    ReqJobSetActionList reqPacket;
    ReqJobSetAction* packet = reqPacket.Append();

    packet->jobId = nJobId;
    nAction |= (nNewStep << 16) ;
    packet->action = nAction;

    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;
    info.type = AsyncReqInfo::SETACTION;

    int ret = SendPacketToDCC(reqPacket, 
            Ajs::reqJobSetActionListCid,
            nUniqueId,
            server_info.first,
            server_info.second
            );

    if ( ret != 0 ){
	    LogDebug("CHttpServerApp::ReqSetAction(Ajs::reqJobSetActionListCid, SendPacketToDCC() failed! job id: %u, job server id: %hu flow id: %u)",
                nJobId, js_id, nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
        return;
    }

    //发送成功，将客户端信息加入map
    m_mapAsyncReqInfo[nUniqueId] = info;

	LogDebug("CHttpServerApp::ReqSetAction(set action ok! job id: %u, job server id: %hu flow id: %u)",
            nJobId, js_id, nUniqueId);
}

void CHttpServerApp::ReqUpdate(Json::Value &request, unsigned int nUniqueId)
{
	unsigned short js_id = 0;
    unsigned int nJobId = 0;
    int nField = 0;
    int nStep = 0;
    string strData;

	if( request["job_id"].isString() ){
		string strJobId = request["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)){
            LogWarning("CHttpServerApp::ReqUpdate(trans job_id failed, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short)(ullJobId >> 56);
		nJobId = (int)ullJobId ;
	} else {
        LogWarning("CHttpServerApp::ReqUpdate(job_id is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'empty value'", nUniqueId);
		return;
	}

	if(request["field"].isString()){
		string field = Trim(request["field"].asString());

		int ret = CheckField(field);

		if ( ret != -1 ){
			nField = ret;
		}
		else {
            LogWarning("CHttpServerApp::ReqUpdate(field is invalid, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'field',error:'invalid value'", nUniqueId);
			return;
		}

	}else {
        LogWarning("CHttpServerApp::ReqUpdate(Json: field is null, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'field',error:'empty value'", nUniqueId);
		return;
	}

	if (request["step"].isInt()) {
		nStep = request["step"].asInt();

	} else {
		nStep = g_nDefaultStep;
	}

	//检查data字段

	if (request["data"].isString()){
		strData = request["data"].asString();
	}
	else 
		strData = g_strDefaultData;
	
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqUpdate(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}
	//将json转换为asn格式并发送到dcc
    
    ReqJobUpdateList reqPacket;
    ReqJobUpdate* packet = reqPacket.Append();

    //填充包
    packet->jobId = nJobId;
    packet->field = nField;
    packet->step = nStep;
    packet->data = strData.c_str();

    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;
    info.type = AsyncReqInfo::UPDATE;

    int ret = SendPacketToDCC(reqPacket, 
            Ajs::reqJobUpdateListCid,
            nUniqueId,
            server_info.first,
            server_info.second
            );

    if ( ret != 0 ){
        LogDebug("CHttpServerApp::ReqUpdate(SendPacketToDCC failed! job id: %u, job server id: %hu, flow id: %u)",
            nJobId, js_id, nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
        return;
    }

    //发送成功，将客户端信息加入map
    m_mapAsyncReqInfo[nUniqueId] = info;

	LogDebug("CHttpServerApp::ReqUpdate(update ok! job id: %u, job server id: %hu, flow id: %u)",
            nJobId, js_id, nUniqueId);
}

void CHttpServerApp::ReqQuery(Json::Value &request, unsigned int nUniqueId)
{
	unsigned short js_id = 0;
    unsigned int nJobId = 0;
    int nStep = 0;

	if( request["job_id"].isString() ){
		string strJobId = request["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)){
            LogWarning("CHttpServerApp::ReqQuery(trans job_id failed, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short)(ullJobId >> 56);
		nJobId = (int)ullJobId ;
	} else {
        LogWarning("CHttpServerApp::ReqQuery(job_id is null, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'empty value'", nUniqueId);
		return;
	}

	if (request["query_step"].isInt()){
		nStep = request["query_step"].asInt();
	} else {
		nStep = g_nDefaultStep;
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqQuery(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

	//将json转换为asn格式并发送到dcc
    ReqJobQueryAllList reqPacket;
    ReqJobQueryAll* packet = reqPacket.Append();

    //填充包
    packet->jobId = nJobId;
    packet->step = nStep;

    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;
    if (g_strProxyType == "query")
    	info.type = AsyncReqInfo::QUERY;
    else
    	info.type = AsyncReqInfo::QUERY2;

    int ret = SendPacketToDCC(reqPacket, 
            Ajs::reqJobQueryAllListCid,
            nUniqueId,
            server_info.first,
            server_info.second
            );

    if ( ret != 0 ){
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
        return;
    }

    //发送成功，将客户端信息加入map
    m_mapAsyncReqInfo[nUniqueId] = info;

    LogDebug("CHttpServerApp::ReqQuery(update ok! job id: %u, job server id: %hu flow id: %u)",nJobId, js_id, nUniqueId);
}

void CHttpServerApp::ReqQueryField(Json::Value &request, unsigned int nUniqueId)
{
	unsigned short js_id = 0;
    unsigned int nJobId = 0;
    int nStep = 0;
    set<int> field_set;

	if( request["job_id"].isString() ){
		string strJobId = request["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)){
            LogWarning("CHttpServerApp::ReqQueryField(trans job_id failed, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short)(ullJobId >> 56);
		nJobId = (int)ullJobId ;
	} else {
        LogWarning("CHttpServerApp::ReqQueryField(job_id is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'empty value'", nUniqueId);
		return;
	}

	//检查field字段
	if (request["field"].isString()){

		field_set.clear();

		string field = Trim(request["field"].asString());
		//bool check_step = false;

		if (Trim(field) == "*"){
			for (int i = JOB_FIELD_STATUS; i <= JOB_FIELD_STEP_FILE_TRANS_TOTAL_SIZE; ++i) {
				field_set.insert(i);
			}
			//check_step = true;
		} else {
			vector<string> fields;
			SplitDataToVector(fields, field, "|");
			//检查每个field是否合法
			for (size_t i = 0; i != fields.size(); ++i) {
				string f = Trim(fields[i]);

				int ret = CheckField(f);
				if (ret != -1) {
					field_set.insert(ret);
				}

				//if (f.find("step") != string::npos ) {
				//	check_step = true;
				//}
			}
		}

		//检查step字段
		if (request["step"].isInt())
			nStep = request["step"].asInt();
		else
			nStep = g_nDefaultStep;
	} else {
        LogWarning("CHttpServerApp::ReqQueryField(field is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'field',error:'empty value'", nUniqueId);
		return;
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqQueryField(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}
	//将json转换为asn格式并发送到dcc
    ReqJobQueryFieldList reqPacket;
    ReqJobQueryField* packet = reqPacket.Append();

    //填充包
    packet->jobId = nJobId;
    packet->step = nStep;
    for (set<int>::iterator it = field_set.begin();
            it != field_set.end();
            ++it) {
        AsnInt *elmt = packet->field.Append();
        *elmt = *it;
    }

    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;
    info.type = AsyncReqInfo::QUERY_FIELD;

    int ret = SendPacketToDCC(reqPacket, 
            Ajs::reqJobQueryFieldListCid,
            nUniqueId,
            server_info.first,
            server_info.second
            );

    if ( ret != 0 ){
	    LogDebug("CHttpServerApp::ReqQueryField(SendPacketToDCC failed! job id: %u, job server id: %hu flow id: %u)",
                 nJobId, js_id, nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
        return;
    }

    //发送成功，将客户端信息加入map
    m_mapAsyncReqInfo[nUniqueId] = info;

	LogDebug("CHttpServerApp::ReqQueryField(update ok! job id: %u, job server id: %hu flow id: %u)",
            nJobId, js_id, nUniqueId);
}

void CHttpServerApp::ReqQuerySelect(Json::Value &request, unsigned int nUniqueId)
{
    int nClientModule = 0;
	if(request["client_module"].isInt()){
		nClientModule = request["client_module"].asInt();
	} else { 
        LogWarning("CHttpServerApp::ReqQuerySelect(client_module is not int, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'client_module',error:'invalid value'", nUniqueId);
		return;
	}
	
	set<string> selectField_set;

	if(request["status_in"].isString()){
		string status_in = Trim(request["status_in"].asString());
		vector<string> vecStatus;
		vector<string> status;
		SplitDataToVector(status, status_in, "|");
		for (size_t i = 0; i != status.size(); ++i) {
			string s = Trim(status[i]);
			int ret = ConvertStatusStrToInt(s);

			if ( ret != -1 )
				vecStatus.push_back(IntToString(ret));
			else {
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'status_in',error:'invalid value'", nUniqueId);
				return;
			}
		}
		string value = "status_in=";
		value += JoinDataToStr(vecStatus, "|");

		selectField_set.insert(value);

	}

	if(request["order_by"].isString()){
		string order_by = Trim(request["order_by"].asString());
		string value = "order_by=";
		value += order_by;
		selectField_set.insert(value);
	}

	if(request["job_type_in"].isString()){
		string job_type = Trim(request["job_type_in"].asString());
		string value = "job_type_in=";
		value += job_type;
		selectField_set.insert(value);
	}

	if(request["query_key"].isString()){
		string query_key = Trim(request["query_key"].asString());
		string value = "query_key=";
		value += query_key;
		selectField_set.insert(value);
	}

	if(request["author"].isString()){
		string author = Trim(request["author"].asString());
		string value = "author=";
		value += author;
		selectField_set.insert(value);
	}

	if(request["cur_page"].isInt()){
		int nCurPage = request["cur_page"].asInt();
		if (nCurPage == 0){
			//限制cur_page不能为0，用户意图为输出第一页
            LogWarning("CHttpServerApp::ReqQuerySelect(cur_page is invalid, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'cur_page',error:'invalid value'", nUniqueId);
			return;
		}
		string cur_page = IntToString(nCurPage);
		string value = "cur_page=";
		value += cur_page;
		selectField_set.insert(value);
	} else {
		//用户不能不指定cur_page
        LogWarning("CHttpServerApp::ReqQuerySelect(cur_page is invalid, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'cur_page',error:'invalid value'", nUniqueId);
		return;
	}

	if(request["per_page"].isInt()){
		int nPerPage = request["per_page"].asInt();
		if (nPerPage > g_nMaxPerPage) {
			//限制每页的记录数
            LogWarning("CHttpServerApp::ReqQuerySelect(per_page is too large, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'per_page',error:'too large value'", nUniqueId);
			return;
		}
		string per_page = IntToString(nPerPage);
		string value = "per_page=";
		value += per_page;
		selectField_set.insert(value);
	} else {
		//用户不能不指定per_page
        LogWarning("CHttpServerApp::ReqQuerySelect(per_page is invalid, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'per_page',error:'invalid value'", nUniqueId);
		return;
	}

	//将json转换为asn格式并发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if(GetJobServerByClientModule(nClientModule, server_info)){
        ReqJobQuerySelect reqPacket;

        //填充包
        reqPacket.clientModule = nClientModule;
        for(set<string>::iterator it = selectField_set.begin();
                it != selectField_set.end();
                ++it){
            AsnOcts *elmt = reqPacket.selectField.Append();
            *elmt = (*it).c_str();
        }

        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::QUERY_SELECT;

        int ret = SendPacketToDCC(reqPacket, 
                Ajs::reqJobQuerySelectCid,
                nUniqueId,
                server_info.first,
                server_info.second
                );

        if ( ret != 0 ){
            SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
            return;
        }

        //发送成功，将客户端信息加入map
        m_mapAsyncReqInfo[nUniqueId] = info;
	    LogDebug("AccessServerApp::ReqQuerySelect(query select ok, client_module: %d, job_server: %s, flow id: %u)",
                  nClientModule, IpIntToString(server_info.first).c_str(), nUniqueId);

	} else {
        LogWarning("AccessServerApp::ReqQuerySelect(no job server, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	}
}

void CHttpServerApp::ReqDelete(Json::Value &request, unsigned int nUniqueId)
{
	unsigned short js_id = 0;
    unsigned int nJobId = 0;
	if( request["job_id"].isString() ){
		string strJobId = request["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)){
            LogWarning("CHttpServerApp::ReqDelete(trans job id failed, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short)(ullJobId >> 56);
		nJobId = (int)ullJobId ;
	} else {
        LogWarning("CHttpServerApp::ReqDelete(job id is not string, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'job_id',error:'empty value'", nUniqueId);
		return;
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqDelete(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}
	//将json转换为asn格式并发送到dcc
    ReqJobDeleteList reqPacket;
    ReqJobDelete* packet = reqPacket.Append();

    //填充包
    packet->jobId = nJobId;

    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;
    info.type = AsyncReqInfo::DELETE;

    int ret = SendPacketToDCC(reqPacket, 
            Ajs::reqJobDeleteListCid,
            nUniqueId,
            server_info.first,
            server_info.second
            );

    if ( ret != 0 ){
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
        return;
    }

    //发送成功，将客户端信息加入map
    m_mapAsyncReqInfo[nUniqueId] = info;

    LogDebug("CHttpServerApp::ReqDelete(update ok! job id: %u, job server id: %hu flow id: %u)",nJobId, js_id, nUniqueId);
}

//2.0.0版本新增批量接口
void CHttpServerApp::ReqCreateBatch(Json::Value &req, unsigned int nUniqueId)
{
    if (req["json_job_array"].isNull()){
        LogWarning("CHttpServerApp::ReqCreateBatch(json_job_array is null, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job_array', error:'empty value", nUniqueId);
        return;
    }

    Json::Value &json_job = req["json_job_array"];

    if (!json_job.isArray()){
        LogWarning("CHttpServerApp::ReqCreateBatch(json_job is not array, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job', error:'invalid value", nUniqueId);
        return;
    }

    //限制批量创建任务的数量
    if (json_job.size() > (unsigned int)g_nMaxCreateBatchCount){
        LogWarning("CHttpServerApp::ReqCreateBatch(create too many jobs (> %d), flow id: %u)", g_nMaxCreateBatchCount, nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'create too many jobs'", nUniqueId);
    	return;
    }

    int nClientModule = -1;
    string strPassWd;
    ReqJobCreateList reqPacket;
    for(Json::Value::ArrayIndex i = 0; i != json_job.size(); ++i){
        ReqJobCreate *packet = reqPacket.Append();
        Json::Value &request = json_job[i];

        packet->flag = 0;

        if (!request.isObject()){
            LogWarning("CHttpServerApp::ReqCreateBatch(json_job_array is not a object, flow id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'is not object'", nUniqueId);
            return;
        }

        //检查client_module
        if(request.isMember("client_module") && request["client_module"].isInt()){
            packet->clientModule = request["client_module"].asInt();
            //防止批量创建任务时给出不同的client_module
            if ( i == 0 )
            	nClientModule = (int)packet->clientModule;
            else if (nClientModule != (int)packet->clientModule){
                LogWarning("CHttpServerApp::ReqCreateBatchBatch(client_module is not consistent, flow id: %u, client id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_module',error:'client_module is not consistent', index of array: " + IntToString((unsigned int)i), nUniqueId);
                return;
            }
        }
        else{
            LogWarning("CHttpServerApp::ReqCreateBatchBatch(client_module is invalid, flow id: %u, index of array: %u)", nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_module',error:'invalid value', index of array: " + IntToString((unsigned int)i), nUniqueId);
            return;
        }

        string passwd;
        if(request.isMember("client_passwd") && request["client_passwd"].isString()){
            passwd = request["client_passwd"].asString();
            if ( 0 == i )
            	strPassWd = passwd;
            else if (strPassWd != passwd) {
                LogWarning("CHttpServerApp::ReqCreateBatchBatch(passwd is not consistent, flow id: %u, client id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_module',error:'passwd is not consistent', index of array: " + IntToString((unsigned int)i), nUniqueId);
                return;
            }
        } else {
            LogWarning("CHttpServerApp::ReqCreateBatch(client_passwd is invalid, flow id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'client_passwd',error:'invalid value', index of array: " + IntToString((unsigned int)i), nUniqueId);
            return;
        }


        if ( 0 == i && !ClientModuleAuth((int)(packet->clientModule), passwd)){
            LogWarning("CHttpServerApp::ReqCreateBatch(client_passwd is not correct, flow id: %u, client id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(ACCESS_DENIED,ACCESS_DENIED_REASON +  "param:'client_passwd',error:'incorrect password', index of array: " + IntToString((unsigned int)i), nUniqueId);
            return;
        }


        //检查run_mode
        if (request["run_mode"].isString()){ 
            string run_mode = Trim(request["run_mode"].asString());
            int ret = CheckRunModule(run_mode);
            if (ret != -1)
                packet->flag = (int)(packet->flag) | ret;
            else {
                LogWarning("CHttpServerApp::ReqCreateBatch(run_mode is invalid, flow id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'run_mode',error:'invalid value', index of array: " + IntToString((unsigned int)i), nUniqueId);
                return;
            }
        } else if (!request["run_mode"].isNull()) {
            LogWarning("CHttpServerApp::ReqCreateBatch(run_mode is not a string, flow id: %u, index of array: %u)",  nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'run_mode',error:'invalid value', index of array: " + IntToString((unsigned int)i), nUniqueId);
            return;
        }

        //检查delete_mode
        if (request["delete_mode"].isString()){ 
            string delete_mode = Trim(request["delete_mode"].asString());
            int ret = CheckDeleteModule(delete_mode);
            if (ret != -1)
                packet->flag = (int)(packet->flag) | ret;
            else {
                LogWarning("CHttpServerApp::ReqCreateBatch(delete_mode is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'delete_mode',error:'invalid value'", nUniqueId);
                return;
            }

        } else if (!request["delete_mode"].isNull()) {
            LogWarning("CHttpServerApp::ReqCreateBatch(delete_mode is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON +  "param:'delete_mode',error:'invalid value'", nUniqueId);
            return;
        }

        //检查now_run
        if (request["now_run"].isString()){ 
            string now_run = Trim(request["now_run"].asString());
            int ret = CheckNowRun(now_run);
            if (ret != -1)
                packet->flag = (int)(packet->flag) | ret;
            else {
                LogWarning("CHttpServerApp::ReqCreateBatch(now_run is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'now_run',error:'invalid value'", nUniqueId);
                return;
            }

        } else if (!request["now_run"].isNull()){
            LogWarning("CHttpServerApp::ReqCreateBatch(now_run is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'now_run',error:'invalid value'", nUniqueId);
            return;
        } else {
            packet->flag = (int)(packet->flag) | AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL; 
        }


        if (request["signal"].isInt()){
            packet->userSignal = request["signal"].asInt();
        } else
            packet->userSignal = g_nDefaultSignal;	

        if (request["author"].isString())
            packet->author = request["author"].asString().c_str();
        else 
            packet->author = "";

        if (request["query_key"].isString())
            packet->queryKey = request["query_key"].asString().c_str();
        else 
            packet->queryKey = "";

        if (request["job_type"].isString())
            packet->jobType = request["job_type"].asString().c_str();
        else
            packet->jobType = "";

        if (request["job_desc"].isString())
            packet->jobDesc = request["job_desc"].asString().c_str();
        else
            packet->jobDesc = "";

        if (request["data"].isString())
            packet->userData = request["data"].asString().c_str();
        else 
            packet->userData = "";

        //检查step_list
        if (request["step_list"].isNull()){ 
            LogWarning("CHttpServerApp::ReqCreateBatch(step_list is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list',error:'empty value'", nUniqueId);
            return;
        } else if ( request["step_list"].isArray() ) {
            Json::Value &step_list = request["step_list"];

            if ( step_list.size() == 0){
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list',error:'empty value'", nUniqueId);
                return ;
            } else {
                for (Json::Value::ArrayIndex i = 0; i != step_list.size(); ++i){
                    Json::Value &step = step_list[i];
                    JobStepSingle* sigle = packet->stepList.Append();

                    //检查type
                    if (!step.isObject()) {
                        LogWarning("CHttpServerApp::ReqCreateBatch(step %u is not object, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list',error:'invalid value'", nUniqueId);
                        return;
                    } else if ( step["type"].isNull()) {
                        LogWarning("CHttpServerApp::ReqCreateBatch('type' of step %u is null, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list type',error:'empty value'", nUniqueId);
                        return ;
                    } else if (step["type"].isInt()) {
                        int type = step["type"].asInt();
                        if (0 == type) {
                            //执行命令
                            unsigned int nCmdIp = 0;
                            if (step["ip"].isString()){
                                string strTemp = step["ip"].asString();
                                if (IpStringToInt(nCmdIp, strTemp) != 0) {
                                    nCmdIp = StringToInt(strTemp);
                                    string strTemp2 = IntToString(nCmdIp);
                                    if (strTemp != strTemp2)
                                        nCmdIp = 0;
                                }
                                if (nCmdIp == 0) {
                                    LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list ip',error:'invalid value'", nUniqueId);
                                    return;
                                }

                            }else {
                                LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list ip',error:'value is not string'", nUniqueId);
                                return ;
                            }

                            sigle->choiceId = JobStepSingle::jobStepShellCmdCid; 
                            sigle->jobStepShellCmd = new JobStepShellCmd;
                            JobStepShellCmd *cmd = sigle->jobStepShellCmd;
                            cmd->flag = 0;

                            cmd->agentId = nCmdIp;

                            if(step["time_out"].isInt())
                                cmd->timeout = step["time_out"].asInt();
                            else
                                cmd->timeout = g_nDefaultTimeout;

                            if(step["user"].isString())
                                cmd->execUser = step["user"].asString().c_str();
                            else 
                                cmd->execUser = g_strDefaultUser.c_str();

                            if(step["not_log"].isInt()){
                                int not_log = step["not_log"].asInt();
                                int ret = CheckNotLog(not_log);
                                if ( ret != -1 )
                                    cmd->flag = (int)(cmd->flag) | ret;
                                else {
                                    LogWarning("CHttpServerApp::ReqCreateBatch('not_log' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list not_log',error:'value is not int'", nUniqueId);
                                    return ;
                                }
                            } 

                            if(step["not_getout"].isInt()){
                                int not_getout = step["not_getout"].asInt();
                                int ret = CheckNotGetOut(not_getout);
                                if ( ret != -1 )
                                    cmd->flag = (int)(cmd->flag) | ret;
                                else {
                                    LogWarning("CHttpServerApp::ReqCreateBatch('not_getout' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list not_getout',error:'value is not int'", nUniqueId);
                                    return ;
                                }
                            } 

                            if(step["not_geterr"].isInt()){
                                int not_geterr = step["not_geterr"].asInt();
                                int ret = CheckNotGetOut(not_geterr);
                                if ( ret != -1 )
                                    cmd->flag = (int)(cmd->flag) | ret;
                                else {
                                    LogWarning("CHttpServerApp::ReqCreateBatch('not_geterr' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list not_geterr',error:'value is not int'", nUniqueId);
                                    return ;
                                }
                            } 

                            if(step["buf_size"].isInt()){
                                int nBufSize = step["buf_size"].asInt();
                                if (g_nMaxStdOut < nBufSize ){
                                    LogWarning("CHttpServerApp::ReqCreateBatch('buf_size' of step %u is greater than 6M, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list buf_size',error:'value is too large(>6M)'", nUniqueId);
                                    return;
                                }
                                cmd->bufSize = nBufSize;
                            }
                            else
                                cmd->bufSize = g_nDefaultBufSize;

                            if(step["cmd"].isString())
                                cmd->cmd = step["cmd"].asString().c_str();
                            else 
                                cmd->cmd = "";

                            if(step["err_deal"].isInt())
                                cmd->errDeal = step["err_deal"].asInt();
                            else
                                cmd->errDeal = g_nDefaultErrDeal;

                            if(step["info"].isString())
                                cmd->info = step["info"].asString().c_str();
                            else
                                cmd->info = "";

                            if(step["desc"].isString())
                                cmd->desc = step["desc"].asString().c_str();
                            else
                                cmd->desc = "";


                        } else if (1 == type) {
                            //传文件

                            sigle->choiceId = JobStepSingle::jobStepFileSvrCid; 
                            sigle->jobStepFileSvr = new JobStepFileSvr;
                            JobStepFileSvr *file = sigle->jobStepFileSvr;
                            file->flag = 0;

                            if(step["src_ip"].isString()){
                                string src_ip = Trim(step["src_ip"].asString());
                                unsigned int ip;
                                int nCmdIp = -1;
                                if ( IpStringToInt(ip ,src_ip) != 0 ){
                                    nCmdIp = StringToInt(src_ip);
                                    string strTemp2 = IntToString(nCmdIp);
                                    if (src_ip != strTemp2)
                                        nCmdIp = 0;
                                    else ip = (unsigned)nCmdIp;
                                }

                                if (nCmdIp == 0){
                                    LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list src_ip',error:'invalid ip'", nUniqueId);
                                    return ;
                                } else 
                                    file->srcAgentId = ip;

                            } else {
                                LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list src_ip',error:'value is not string'", nUniqueId);
                                return ;
                            }

                            if(step["src_path"].isString()){
                                file->srcFile = step["src_path"].asString().c_str();
                            } else {
                                LogWarning("CHttpServerApp::ReqCreateBatch('src_path' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list src_path',error:'empty value'", nUniqueId);
                                return ;
                            }

                            if(step["dest_ip"].isString()){
                                string dest_ip = Trim(step["dest_ip"].asString());
                                unsigned int ip;
                                int nCmdIp = -1;
                                if ( IpStringToInt(ip ,dest_ip) != 0 ){
                                    nCmdIp = StringToInt(dest_ip);
                                    string strTemp2 = IntToString(nCmdIp);
                                    if (dest_ip != strTemp2)
                                        nCmdIp = 0;
                                    else ip = (unsigned)nCmdIp;
                                }

                                if (nCmdIp == 0){
                                    LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list dest_ip',error:'invalid ip'", nUniqueId);
                                    return ;
                                }
                                else file->dstAgentId = ip;
                            } else {
                                LogWarning("CHttpServerApp::ReqCreateBatch('ip' of step %u is not string, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list dest_ip',error:'value is not string'", nUniqueId);
                                return ;
                            }

                            if(step["dest_path"].isString() ){
                                file->dstPath = step["dest_path"].asString().c_str();
                            } else {
                                LogWarning("CHttpServerApp::ReqCreateBatch('dest_path' of step %u is invalid, flow id: %u, type: %d)", (unsigned int)i, nUniqueId, type);
                                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list dest_path',error:'empty value'", nUniqueId);
                                return ;
                            }


                            if(step["desc"].isString())
                                file->desc = step["desc"].asString().c_str();
                            else 
                                file->desc = "";

                            if(step["info"].isString())
                                file->info = step["info"].asString().c_str();
                            else
                                file->info = "";

                            if(step["err_deal"].isInt())
                                file->errDeal = step["err_deal"].asInt();
                            else
                                file->errDeal = g_nDefaultErrDeal;

                            if(step["time_out"].isInt())
                                file->timeout = step["time_out"].asInt();
                            else
                                file->timeout = g_nDefaultTimeout;
                            
                            if(step["out_max_con_retry"].isInt()){
								int ret = CheckFileOutMaxConRetry(step["out_max_con_retry"].asInt());
								if ( ret != -1 )
									file->flag = (int)(file->flag) | ret;
							}
                            
                        } else {
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list type',error:'invalid value'", nUniqueId);
                            return ;
                        }

                    } else {
                        LogWarning("CHttpServerApp::ReqCreateBatch('type' of step %u is not int, flow id: %u)", (unsigned int)i, nUniqueId);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list type',error:'invalid value'", nUniqueId);
                        return ;
                    }
                }
            }
        } else {
            LogWarning("CHttpServerApp::ReqCreateBatch(step_list is not a array, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list',error:'value is not array'", nUniqueId);
            return ;
        }
    }

    //发送到dcc
    pair<unsigned int, unsigned short> server_info;
    if ( GetJobServerByClientModule(nClientModule, server_info) ){
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::CREATE2_BATCH;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobCreateListCid, nUniqueId, server_info.first, server_info.second);
    	if (ret != 0){
    		LogWarning("CHttpServerApp::ReqCreateBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
    		return;
    	}

    	//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
    } else {
        LogWarning("CHttpServerApp::ReqCreateBatch(get job server by client module failed! flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
    }
    LogDebug("CHttpServerApp::ReqCreateBatch(create batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqCreateMany(Json::Value &req, unsigned int nUniqueId)
{
	ReqJobCreate2 packet;

	packet.flag = 0;

	try{

		if (req["json_job"].isNull() ){
            LogWarning("CHttpServerApp::ReqCreateMany(Json:: json_job is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'empty value'", nUniqueId);
			return;
		}

		Json::Value &request = req["json_job"];

		if (!request.isObject()){
            LogWarning("CHttpServerApp::ReqCreateMany(Json:: json_job is not a object, flow id: %u)",  nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'json_job',error:'invalid value'", nUniqueId);
			return;
		}

		//检查client_module
		if(request.isMember("client_module") && request["client_module"].isInt()){
			packet.clientModule = request["client_module"].asInt();
		}
		else{
            LogWarning("CHttpServerApp::ReqCreateMany(Json:: client_module is invalid, flow id: %u)",  nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'client_module',error:'invalid value'", nUniqueId);
			return;
		}

		string passwd;
		if(request.isMember("client_passwd") && request["client_passwd"].isString()){
			passwd = request["client_passwd"].asString();
		} else {
            LogWarning("CHttpServerApp::ReqCreateMany(Json:: client_passwd is invalid, flow id: %u)",  nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'client_passwd',error:'invalid value'", nUniqueId);
			return;
		}


		if (!ClientModuleAuth((int)(packet.clientModule), passwd)){
            LogWarning("CHttpServerApp::ReqCreateMany(client_passwd is not correct, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(ACCESS_DENIED,ACCESS_DENIED_REASON +  "param:'client_passwd',error:'incorrect password'", nUniqueId);
			return;
		}


		//检查run_mode
		if (request["run_mode"].isString()){
			string run_mode = Trim(request["run_mode"].asString());
			int ret = CheckRunModule(run_mode);
			if (ret != -1)
				packet.flag = (int)(packet.flag) | ret;
			else {
                LogWarning("CHttpServerApp::ReqCreateMany(run_mode is invalid, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'run_mode',error:'invalid value'", nUniqueId);
				return;
			}
		} else if (!request["run_mode"].isNull()) {
            LogWarning("CHttpServerApp::ReqCreateMany(run_mode is not a string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'run_mode',error:'invalid value'", nUniqueId);
			return;
		}

			//检查delete_mode
			if (request["delete_mode"].isString()){
				string delete_mode = Trim(request["delete_mode"].asString());
				int ret = CheckDeleteModule(delete_mode);
				if (ret != -1)
					packet.flag = (int)(packet.flag) | ret;
				else {
                    LogWarning("CHttpServerApp::ReqCreateMany(delete_mode is invalid, flow id: %u)", nUniqueId);
                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'delete_mode',error:'invalid value'", nUniqueId);
					return;
				}

			} else if (!request["delete_mode"].isNull()) {
                LogWarning("CHttpServerApp::ReqCreateMany(delete_mode is not a string, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON +  "param:'delete_mode',error:'invalid value'", nUniqueId);
				return;
			}

			//检查now_run
			if (request["now_run"].isString()){
				string now_run = Trim(request["now_run"].asString());
				int ret = CheckNowRun(now_run);
				if (ret != -1)
					packet.flag = (int)(packet.flag) | ret;
				else {
                    LogWarning("CHttpServerApp::ReqCreateMany(now_run is invalid, flow id: %u)", nUniqueId);
                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST, BAD_JSON_REQUEST_REASON + "param:'now_run',error:'invalid value'", nUniqueId);
					return;
				}

			} else if (!request["now_run"].isNull()){
                LogWarning("CHttpServerApp::ReqCreateMany(now_run is not a string, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'now_run',error:'invalid value'", nUniqueId);
				return;
			} else {
				packet.flag = (int)(packet.flag) | AJS_FLAG_NOW_RUN_IMMEDIATE_NOT_MANUAL;
			}

		if (request["signal"].isInt()){
			packet.userSignal = request["signal"].asInt();
		} else
			packet.userSignal = g_nDefaultSignal;

		if (request["author"].isString())
			packet.author = request["author"].asString().c_str();
		else
			packet.author = "";

		if (request["query_key"].isString())
			packet.queryKey = request["query_key"].asString().c_str();
		else
			packet.queryKey = "";

		if (request["job_type"].isString())
			packet.jobType = request["job_type"].asString().c_str();
		else
			packet.jobType = "";

		if (request["job_desc"].isString())
			packet.jobDesc = request["job_desc"].asString().c_str();
		else
			packet.jobDesc = "";

		if (request["data"].isString())
			packet.userData = request["data"].asString().c_str();
		else
			packet.userData = "";

		//检查step
		if (request["step"].isNull()){
            LogWarning("CHttpServerApp::ReqCreateMany(step_list is null, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step_list',error:'empty value'", nUniqueId);
			return;
        } else if ( request["step"].isObject() ) {
            Json::Value &step= request["step"];
            packet.step = new JobStepShellCmd;
            JobStepShellCmd *cmd = packet.step;
            //检查type
            if (!step.isObject()){
                LogWarning("CHttpServerApp::ReqCreateMany(step is not object, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step',error:'invalid value'", nUniqueId);
                return;
            } else if ( step["type"].isNull()) {
                LogWarning("CHttpServerApp::ReqCreateMany('type' of step is null, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step type',error:'empty value'", nUniqueId);
                return ;
            } else if (step["type"].isInt()) {
                int type = step["type"].asInt();
                if ( 0 == type ) {
                    //执行命令
                    if (step["ip_list"].isString()){
                        vector<string> vecIps;
                        string strTemp = step["ip_list"].asString();
                        SplitDataToVector(vecIps, strTemp, ",");

                        if (vecIps.size() == 0){
                        	LogWarning("CHttpServerApp::ReqCreateMany(ip_list is empty, flow id: %u)",
                                    nUniqueId);
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step ip_list',error:'empty ip list'", nUniqueId);
                        	return;
                        }

                        for (size_t k = 0; k != vecIps.size(); ++k){
                            string& strTempIp = vecIps[k];
                            unsigned int nCmdIp = 0;
                            if (IpStringToInt(nCmdIp, strTempIp) != 0) {
                                nCmdIp = StringToInt(strTempIp);
                                string strTemp2 = IntToString(nCmdIp);
                                if (strTempIp != strTemp2)
                                    nCmdIp = 0;
                            }
                            if (nCmdIp == 0) {
                                LogWarning("CHttpServerApp::ReqCreateMany(invalid ip: %s, flow id: %u)",
                                        strTempIp.c_str(), nUniqueId);
                                continue;
                            }

                            AsnInt* localIp = packet.agentIds.Append();
                            *localIp = nCmdIp;
                        }
                        
                    }else {
                        LogWarning("CHttpServerApp::ReqCreateMany('ip' of step is not string, flow id: %u, type: %d)", nUniqueId, type);
                        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step ip',error:'value is not string'", nUniqueId);
                        return ;
                    }

                    cmd->flag = 0;

                    cmd->agentId = 0;

                    if(step["time_out"].isInt())
                        cmd->timeout = step["time_out"].asInt();
                    else
                        cmd->timeout = g_nDefaultTimeout;

                    if(step["user"].isString())
                        cmd->execUser = step["user"].asString().c_str();
                    else
                        cmd->execUser = g_strDefaultUser.c_str();

                    if(step["not_log"].isInt()){
                        int not_log = step["not_log"].asInt();
                        int ret = CheckNotLog(not_log);
                        if ( ret != -1 )
                            cmd->flag = (int)(cmd->flag) | ret;
                        else {
                            LogWarning("CHttpServerApp::ReqCreateMany('not_log' of step is invalid, flow id: %u, type: %d)", nUniqueId, type);
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step not_log',error:'value is not int'", nUniqueId);
                            return ;
                        }
                    }

                    if(step["not_getout"].isInt()){
                        int not_getout = step["not_getout"].asInt();
                        int ret = CheckNotGetOut(not_getout);
                        if ( ret != -1 )
                            cmd->flag = (int)(cmd->flag) | ret;
                        else {
                            LogWarning("CHttpServerApp::ReqCreateMany('not_getout' of step is invalid, flow id: %u, type: %d)", nUniqueId, type);
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step not_getout',error:'value is not int'", nUniqueId);
                            return ;
                        }
                    }

                    if(step["not_geterr"].isInt()){
                        int not_geterr = step["not_geterr"].asInt();
                        int ret = CheckNotGetOut(not_geterr);
                        if ( ret != -1 )
                            cmd->flag = (int)(cmd->flag) | ret;
                        else {
                            LogWarning("CHttpServerApp::ReqCreateMany('not_geterr' of step is invalid, flow id: %u, type: %d)", nUniqueId, type);
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step not_geterr',error:'value is not int'", nUniqueId);
                            return ;
                        }
                    }

                    if(step["buf_size"].isInt()){
                        int nBufSize = step["buf_size"].asInt();
                        if (g_nMaxStdOut < nBufSize ){
                            LogWarning("CHttpServerApp::ReqCreateMany('buf_size' of step is greater than 6M, flow id: %u, type: %d)", nUniqueId, type);
                            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step buf_size',error:'value is too large(>6M)'", nUniqueId);
                            return;
                        }
                        cmd->bufSize = nBufSize;
                    }
                    else
                        cmd->bufSize = g_nDefaultBufSize;

                    if(step["cmd"].isString())
                        cmd->cmd = step["cmd"].asString().c_str();
                    else
                        cmd->cmd = "";

                    if(step["err_deal"].isInt())
                        cmd->errDeal = step["err_deal"].asInt();
                    else
                        cmd->errDeal = g_nDefaultErrDeal;

                    if(step["info"].isString())
                        cmd->info = step["info"].asString().c_str();
                    else
                        cmd->info = "";

                    if(step["desc"].isString())
                        cmd->desc = step["desc"].asString().c_str();
                    else
                        cmd->desc = "";


                } else {
                    SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step type',error:'invalid value'", nUniqueId);
                    return ;
                }

            } else {
                LogWarning("CHttpServerApp::ReqCreateMany('type' of step is not int, flow id: %u)", nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step type',error:'invalid value'", nUniqueId);
                return ;
            }
        } else {
            LogWarning("CHttpServerApp::ReqCreateMany(step_list is not a array, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'step',error:'value is not array'", nUniqueId);
			return ;
		}
	}catch(exception &e){
        LogWarning("CHttpServerApp::ReqCreateMany(catch a exception, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  e.what(), nUniqueId);
		return;
	}

	//发送到dcc

	pair<unsigned int, unsigned short> server_info;
	if ( GetJobServerByClientModule((int)packet.clientModule, server_info) ) {

        unsigned int uniqueId = 0;
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
		info.create_time = g_nNowTime;
		uniqueId = info.ccd_flow;
		info.type = AsyncReqInfo::CREATE_MANY;

        m_mapAsyncReqInfo[nUniqueId] = info;

		int ret = SendPacketToDCC(packet, Ajs::reqJobCreate2Cid, uniqueId, server_info.first, server_info.second);
		if (ret != 0){
			LogWarning("CHttpServerApp::ReqCreateMany(SendPacketToDCC failed! flow id: %u, unique id: %u)",
                    nUniqueId, uniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
            m_mapAsyncReqInfo.erase(nUniqueId);
			return;
		}
	} else {
        LogWarning("CHttpServerApp::ReqCreateMany(get job server by client module failed!, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	}
    LogDebug("CHttpServerApp::ReqCreateMany(create ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqQueryTime(Json::Value &req, unsigned int nUniqueId)
{
	ReqJobQueryFieldList reqPacket;
	ReqJobQueryField *packet = reqPacket.Append();

	unsigned short js_id = 0;
	unsigned int nJobId = 0;

	if (req["job_id"].isString()) {
		string strJobId = req["job_id"].asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)) {
			LogWarning(
					"CHttpServerApp::ReqQueryTime(job id failed, flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
                            + "param:'job_id',error:'invalid value'", nUniqueId);
			return;
		}

		js_id = (unsigned short) (ullJobId >> 56);
		nJobId = (int) ullJobId;
	} else {
		LogWarning(
				"CHttpServerApp::ReqQueryTime(job id is not string, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON + "param:'job_id',error:'empty value'",
                nUniqueId);
		return;
	}

	int nStep = 0;
	if (req["step"].isInt()){
		nStep = req["step"].asInt();
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(js_id, server_info)){
        LogWarning("CHttpServerApp::ReqQueryTime(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

	packet->jobId = nJobId;
	packet->step = nStep;
	AsnInt *field = packet->field.Append();
	*field = JOB_FIELD_STEP_TIME2;

	AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
	info.create_time = g_nNowTime;
	info.type = AsyncReqInfo::QUERY_TIME;

	int ret = SendPacketToDCC(reqPacket,
			Ajs::reqJobQueryFieldListCid,
            nUniqueId,
			server_info.first,
			server_info.second);

	if (ret != 0){
		LogDebug("CHttpServerApp::ReqQueryTime(SendPacketToDCC failed! job id: %u, job server id: %hu, flow id: %u)",
                nJobId, js_id, nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqQueryTime(query time ok. job id: %u, job server id: %hu, flow id: %u)",
            nJobId, js_id, nUniqueId);
}

void CHttpServerApp::ReqQueryAJSAgent(Json::Value &request, unsigned int nUniqueId)
{
	ReqJobQueryAJSAgentList reqPacket;

	if (request["ip_list"].isArray()) {
		if (request["ip_list"].size() > (unsigned int)g_nMaxQueryAgentOnlineCount) {
			LogWarning(
					"CHttpServerApp::ReqQueryAJSAgent(ip list is too many, flow id: %u, max ip: %d)",
                    nUniqueId, g_nMaxQueryAgentOnlineCount);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
                            + "param:'ip_list',error:'too many ips'", nUniqueId);
			return;
		}
		for (unsigned i = 0; i != request["ip_list"].size(); ++i) {
			Json::Value &ele = request["ip_list"][i];
			if (ele.isString()) {
				AsnInt *agent = reqPacket.Append();
				*agent = IpStringToInt(ele.asString());
			} else {
				LogWarning(
						"CHttpServerApp::ReqQueryAJSAgent(ip is not string, flow id: %u)",
                        nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON + "param:'ip',error:'invalid value'",
                        nUniqueId);
				return;
			}
		}
	} else {
		LogWarning(
				"CHttpServerApp::ReqQueryAJSAgent(ip list is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON + "param:'ip_list',error:'invalid value'",
                nUniqueId);
		return;
	}

	int nClientModule = 0;
	if (request["client_module"].isInt()){
		nClientModule = request["client_module"].asInt();
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerByClientModule(nClientModule, server_info)){
        LogWarning("CHttpServerApp::ReqQueryAJSAgent(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

	AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
	info.create_time = g_nNowTime;
	info.type = AsyncReqInfo::QUERY_AJS_AGENT;

	int ret = SendPacketToDCC(reqPacket,
			Ajs::reqJobQueryAJSAgentListCid,
            nUniqueId,
			server_info.first,
			server_info.second);

	if (ret != 0){
		LogDebug("CHttpServerApp::ReqQueryAJSAgent(SendPacketToDCC failed! flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqQueryAJSAgent(query time ok. flow id: %u)",
            nUniqueId);
}

void CHttpServerApp::ReqQueryTSCAgent(Json::Value &request, unsigned int nUniqueId)
{
	ReqJobQueryTSCAgentList reqPacket;

	if (request["ip_list"].isArray()) {
		if (request["ip_list"].size() > (unsigned int)g_nMaxQueryAgentOnlineCount) {
			LogWarning(
					"CHttpServerApp::ReqQueryTSCAgent(ip list is too many, flow id: %u, max ip: %d)",
                    nUniqueId, g_nMaxQueryAgentOnlineCount);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
                            + "param:'ip_list',error:'too many ips'", nUniqueId);
			return;
		}
		for (unsigned i = 0; i != request["ip_list"].size(); ++i) {
			Json::Value &ele = request["ip_list"][i];
			if (ele.isString()) {
				AsnInt *agent = reqPacket.Append();
				*agent = IpStringToInt(ele.asString());
			} else {
				LogWarning(
						"CHttpServerApp::ReqQueryTSCAgent(ip is not string, flow id: %u)",
                        nUniqueId);
                SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON + "param:'ip',error:'invalid value'",
                        nUniqueId);
				return;
			}
		}
	} else {
		LogWarning(
				"CHttpServerApp::ReqQueryTSCAgent(ip list is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON + "param:'ip_list',error:'invalid value'",
                nUniqueId);
		return;
	}

	int nClientModule = 0;
	if (request["client_module"].isInt()){
		nClientModule = request["client_module"].asInt();
	}

	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nClientModule, server_info)){
        LogWarning("CHttpServerApp::ReqQueryTSCAgent(get server info failed, flow id: %u)", nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

	AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
	info.create_time = g_nNowTime;
	info.type = AsyncReqInfo::QUERY_TSC_AGENT;

	int ret = SendPacketToDCC(reqPacket,
			Ajs::reqJobQueryTSCAgentListCid,
            nUniqueId,
			server_info.first,
			server_info.second);

	if (ret != 0){
		LogDebug("CHttpServerApp::ReqQueryTSCAgent(SendPacketToDCC failed! flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(INTERNET_SERVER_ERROR, INTERNET_SERVER_ERROR_REASON, nUniqueId);
		return;
	}

    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqQueryTSCAgent(query time ok. flow id: %u)",
            nUniqueId);
}

void CHttpServerApp::ReqSetActionBatch(Json::Value &req, unsigned int nUniqueId)
{
	if (req["op_json"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqSetActionBatch(op_json is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'op_json', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &op_json = req["op_json"];

	//非数组
	if (!op_json.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqSetActionBatch(op_json is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'op_json', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (op_json.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqSetActionBatch(op_json is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'op_json', error:'empty value", nUniqueId);
		return;
	}

	ReqJobSetActionList reqPacket;

	unsigned short nJsId = -1;
	for (Json::Value::ArrayIndex i = 0; i != op_json.size(); ++i) {

		ReqJobSetAction *packet = reqPacket.Append();
		unsigned short js_id = 0;
		unsigned int nJobId = 0;
		int nAction = -1;
		int nNewStep = 0;

		Json::Value &request = op_json[i];

		if (!request.isObject()) {
			LogWarning(
					"CHttpServerApp::ReqSetActionBatch(op_json is not object, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'op_json',error:'is not object', index of array: "
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["job_id"].isString()) {
			string strJobId = request["job_id"].asString();
			unsigned long long ullJobId = StringToObj<unsigned long long>(
					strJobId);
			string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

			if (Trim(strJobId) != Trim(tmpStrJobId)) {
				LogWarning(
						"CHttpServerApp::ReqSetActionBatch(trans job_id failed, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job_id',error:'invalid value', index of array: "
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}

			js_id = (unsigned short) (ullJobId >> 56);
			nJobId = (int) ullJobId;

			//防止批量设置的任务发向不同的job server
			if ( 0 == i )
				nJsId = js_id;
			else if ( js_id != nJsId ) {
				LogWarning(
						"CHttpServerApp::ReqSetActionBatch(job server id is not consistent, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job_id',error:'job server id is not consistent', index of array: "
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}
		} else {
			LogWarning(
					"CHttpServerApp::ReqSetActionBatch(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array: "
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["op"].isString()) {
			string op = Trim(request["op"].asString());
			int ret = CheckOp(op);
			if (ret != -1) {
				nAction = ret;
			} else {
				LogWarning(
						"CHttpServerApp::ReqSetActionBatch(op is invalid, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'op',error:'invalid value', index of array: "
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}
		} else {
			LogWarning(
					"CHttpServerApp::ReqSetActionBatch(op is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'op',error:'invalid value', index of array: "
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["new_step"].isInt()) {
			nNewStep = request["new_step"].asInt();
		} else
			nNewStep = 0;

		packet->jobId = nJobId;
		nAction |= (nNewStep << 16);
		packet->action = nAction;

	}

	//发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nJsId, server_info)) {
		LogWarning(
				"CHttpServerApp::ReqSetActionBatch(get job server by js id failed! job server id: %u, flow id: %u)",
                nJsId, nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	} else {
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::SETACTION_BATCH;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobSetActionListCid, nUniqueId,
				server_info.first, server_info.second);
		if (ret != 0) {
			LogWarning(
					"CHttpServerApp::ReqSetActionBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
			return;
		}

		//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
	}
    LogDebug("CHttpServerApp::ReqSetActionBatch(set action batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqUpdateBatch(Json::Value &req, unsigned int nUniqueId)
{
	if (req["update_json"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqUpdateBatch(update_json is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'update_json', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &update_json = req["update_json"];

	//非数组
	if (!update_json.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqUpdateBatch(update_json is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'update_json', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (update_json.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqUpdateBatch(update_json is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'update_json', error:'empty value", nUniqueId);
		return;
	}

	ReqJobUpdateList reqPacket;

	unsigned short nJsId = -1;
	for (Json::Value::ArrayIndex i = 0; i != update_json.size(); ++i) {

		ReqJobUpdate *packet = reqPacket.Append();
		unsigned short js_id = 0;
		unsigned int nJobId;
		int nField = -1;
		int nStep = 0;
		string strData ;

		Json::Value &request = update_json[i];

		if (!request.isObject()){
			LogWarning(
					"CHttpServerApp::ReqUpdateBatch(update_json is not object, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'update_json',error:'is not object', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["job_id"].isString()) {
			string strJobId = request["job_id"].asString();
			unsigned long long ullJobId = StringToObj<unsigned long long>(
					strJobId);
			string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

			if (Trim(strJobId) != Trim(tmpStrJobId)) {
				LogWarning(
						"CHttpServerApp::ReqUpdateBatch(trans job_id failed, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job_id',error:'invalid value', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}

			js_id = (unsigned short) (ullJobId >> 56);
			nJobId = (int) ullJobId;
			if ( 0 == i )
				nJsId = js_id;
			else if ( nJsId != js_id){
				LogWarning(
						"CHttpServerApp::ReqUpdateBatch(js id is not consistent, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job id',error:'js id is not consistent', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}
		} else {
			LogWarning(
					"CHttpServerApp::ReqUpdateBatch(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'empty value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["field"].isString()) {
			string field = Trim(request["field"].asString());

			int ret = CheckField(field);

			if (ret != -1) {
				nField = ret;
			} else {
				LogWarning(
						"CHttpServerApp::ReqUpdateBatch(field is invalid, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'field',error:'invalid value', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}

		} else {
			LogWarning(
					"CHttpServerApp::ReqUpdateBatch(Json: field is null, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'field',error:'empty value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}


		if (request["step"].isInt()) {
			nStep = request["step"].asInt();

		} else {
			nStep = g_nDefaultStep;
		}

		//检查data字段

		if (request["data"].isString()) {
			strData = request["data"].asString();
		} else
			strData = g_strDefaultData;

		packet->jobId = nJobId;
		packet->field = nField;
		packet->step = nStep;
		packet->data = strData.c_str();
	}

	//发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nJsId, server_info)) {
		LogWarning(
				"CHttpServerApp::ReqUpdateBatch(get job server by js id failed! job server id: %u, flow id: %u)",
                nJsId, nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	} else {
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::UPDATE_BATHC;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobUpdateListCid, nUniqueId,
				server_info.first, server_info.second);
		if (ret != 0) {
			LogWarning(
					"CHttpServerApp::ReqUpdateBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
			return;
		}

		//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
	}
    LogDebug("CHttpServerApp::ReqUpdateBatch(update batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqQueryBatch(Json::Value &req, unsigned int nUniqueId)
{
	if (req["query2_json"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqQueryBatch(query2_json is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query2_json', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &query2_json = req["query2_json"];

	//非数组
	if (!query2_json.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqQueryBatch(query2_json is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query2_json', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (query2_json.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqQueryBatch(query2_json is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query2_json', error:'empty value", nUniqueId);
		return;
	}

	ReqJobQueryAllList reqPacket;

	unsigned short nJsId = -1;
	for (Json::Value::ArrayIndex i = 0; i != query2_json.size(); ++i) {

		ReqJobQueryAll *packet = reqPacket.Append();
		unsigned short js_id = 0;
		unsigned int nJobId;
		int nStep = 0;
		string strData ;

		Json::Value &request = query2_json[i];

		if (!request.isObject()){
			LogWarning(
					"CHttpServerApp::ReqQueryBatch(request is not object, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'query2_json',error:'is not object', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["job_id"].isString()) {
			string strJobId = request["job_id"].asString();
			unsigned long long ullJobId = StringToObj<unsigned long long>(
					strJobId);
			string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

			if (Trim(strJobId) != Trim(tmpStrJobId)) {
				LogWarning(
						"CHttpServerApp::ReqQueryBatch(trans job_id failed, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job_id',error:'invalid value', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}

			js_id = (unsigned short) (ullJobId >> 56);
			nJobId = (int) ullJobId;
			if ( 0 == i )
				nJsId = js_id;
			else if ( nJsId != js_id){
				LogWarning(
						"CHttpServerApp::ReqQueryBatch(js id is not consistent, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job id',error:'js id is not consistent', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}
		} else {
			LogWarning(
					"CHttpServerApp::ReqQueryBatch(job_id is not string, flow id: %u, client_id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'empty value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["query_step"].isInt()) {
			nStep = request["query_step"].asInt();
		} else {
			nStep = g_nDefaultStep;
		}

		packet->jobId = nJobId;
		packet->step = nStep;
	}

	//发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nJsId, server_info)) {
		LogWarning(
				"CHttpServerApp::ReqQueryBatch(get job server by js id failed! job server id: %u, flow id: %u)",
                nJsId, nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	} else {
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::QUERY2_BATCH;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobQueryAllListCid, nUniqueId,
				server_info.first, server_info.second);
		if (ret != 0) {
			LogWarning(
					"CHttpServerApp::ReqQueryBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
			return;
		}

		//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
	}
    LogDebug("CHttpServerApp::ReqQueryBatch(query batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqQueryFieldBatch(Json::Value &req, unsigned int nUniqueId)
{
	if (req["query_field_json"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqQueryFieldBatch(query_field_json is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query_field_json', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &query_field_json = req["query_field_json"];

	//非数组
	if (!query_field_json.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqQueryFieldBatch(query_field_json is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query_field_json', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (query_field_json.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqQueryFieldBatch(query_field_json is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'query_field_json', error:'empty value", nUniqueId);
		return;
	}

	ReqJobQueryFieldList reqPacket;

	unsigned short nJsId = -1;
	for (Json::Value::ArrayIndex i = 0; i != query_field_json.size(); ++i) {

		ReqJobQueryField *packet = reqPacket.Append();
		unsigned short js_id = 0;
		unsigned int nJobId;
		int nStep = 0;
		string strData ;

		Json::Value &request = query_field_json[i];

		//元素不是json对象,返回错误
		if (!request.isObject()){
			LogWarning(
					"CHttpServerApp::ReqQueryFieldBatch(request is not object, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		if (request["job_id"].isString()) {
			string strJobId = request["job_id"].asString();
			unsigned long long ullJobId = StringToObj<unsigned long long>(
					strJobId);
			string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

			if (Trim(strJobId) != Trim(tmpStrJobId)) {
				LogWarning(
						"CHttpServerApp::ReqQueryFieldBatch(trans job_id failed, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job_id',error:'invalid value', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}

			js_id = (unsigned short) (ullJobId >> 56);
			nJobId = (int) ullJobId;
			if ( 0 == i )
				nJsId = js_id;
			else if ( nJsId != js_id){
				LogWarning(
						"CHttpServerApp::ReqQueryFieldBatch(js id is not consistent, flow id: %u, index of array: %u)",
                        nUniqueId, (unsigned int)i);
                SendErrHttpRspByUniqueId(
						BAD_JSON_REQUEST,
						BAD_JSON_REQUEST_REASON
								+ "param:'job id',error:'js id is not consistent', index of array:"
								+ IntToString((unsigned int)i),
                                nUniqueId);
				return;
			}
		} else {
			LogWarning(
					"CHttpServerApp::ReqQueryFieldBatch(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}

		set<int> field_set;
		//检查field字段
		if (request["field"].isString()){

			field_set.clear();

			string field = Trim(request["field"].asString());
			//bool check_step = false;

			if (Trim(field) == "*"){
				for (int i = JOB_FIELD_STATUS; i <= JOB_FIELD_STEP_FILE_TRANS_TOTAL_SIZE; ++i) {
					field_set.insert(i);
				}
				//check_step = true;
			} else {
				vector<string> fields;
				SplitDataToVector(fields, field, "|");
				//检查每个field是否合法
				for (size_t i = 0; i != fields.size(); ++i) {
					string f = Trim(fields[i]);

					int ret = CheckField(f);
					if (ret != -1) {
						field_set.insert(ret);
					}
					/*
					if (f.find("step") != string::npos ) {
						check_step = true;
					}*/
				}
			}

			//检查step字段
			//if ( check_step ){
			if (request["step"].isInt())
				nStep = request["step"].asInt();
			else
				nStep = g_nDefaultStep;
		} else {
            LogWarning("CHttpServerApp::ReqQueryFieldBatch(field is not string, flow id: %u)", nUniqueId);
            SendErrHttpRspByUniqueId(BAD_JSON_REQUEST,BAD_JSON_REQUEST_REASON +  "param:'field',error:'empty value'", nUniqueId);
			return;
		}

		packet->jobId = nJobId;
		packet->step = nStep;
		for (set<int>::iterator it = field_set.begin();
				it != field_set.end();
				++it) {
			AsnInt *elmt = packet->field.Append();
			*elmt = *it;
		}
	}

	//发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nJsId, server_info)) {
		LogWarning(
				"CHttpServerApp::ReqQueryFieldBatch(get job server by js id failed! job server id: %u, flow id: %u)",
                nJsId, nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	} else {

        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::QUERY_FIELD_BATCH;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobQueryFieldListCid, nUniqueId,
				server_info.first, server_info.second);
		if (ret != 0) {
			LogWarning(
					"CHttpServerApp::ReqQueryFieldBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
			return;
		}

		//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
	}
    LogDebug("CHttpServerApp::ReqQueryFieldBatch(query feild batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqDeleteBatch(Json::Value &req, unsigned int nUniqueId)
{
	if (req["job_id_list"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqDeleteBatch(job_id_list is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &job_id_list = req["job_id_list"];

	//非数组
	if (!job_id_list.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqDeleteBatch(job_id_list is not array, flow id: %u, client id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (job_id_list.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqDeleteBatch(job_id_list is empty, flow id: %u, client id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	ReqJobDeleteList reqPacket;

	unsigned short nJsId = -1;
	for (Json::Value::ArrayIndex i = 0; i != job_id_list.size(); ++i) {

		ReqJobDelete *packet = reqPacket.Append();
		unsigned short js_id = 0;
		unsigned int nJobId;

		Json::Value &request = job_id_list[i];
		if (!request.isString()){
			//如果job id不为string，返回错误
			LogWarning(
					"CHttpServerApp::ReqDeleteBatch(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}


		string strJobId = request.asString();
		unsigned long long ullJobId = StringToObj<unsigned long long>(strJobId);
		string tmpStrJobId = ObjToString<unsigned long long>(ullJobId);

		if (Trim(strJobId) != Trim(tmpStrJobId)) {
			LogWarning(
					"CHttpServerApp::ReqDeleteBatch(trans job_id failed, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
                            + IntToString((unsigned int) i), nUniqueId);
			return;
		}

		js_id = (unsigned short) (ullJobId >> 56);
		nJobId = (int) ullJobId;
		if (0 == i)
			nJsId = js_id;
		else if (nJsId != js_id) {
			LogWarning(
					"CHttpServerApp::ReqDeleteBatch(js id is not consistent, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job id',error:'js id is not consistent', index of array:"
                            + IntToString((unsigned int) i), nUniqueId);
			return;
		}

		packet->jobId = nJobId;
	}

	//发送到dcc
	pair<unsigned int, unsigned short> server_info;
	if (!GetJobServerInfoById(nJsId, server_info)) {
		LogWarning(
				"CHttpServerApp::ReqDeleteBatch(get job server by js id failed! job server id: %u, flow id: %u)",
                nJsId, nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, "Service Unavailable", nUniqueId);
		return;
	} else {
        AsyncReqInfo info;
        info.ccd_flow = m_nFlow;
        info.append = m_nNowAppend;
        info.uniq_id = m_nUniqueId;
        info.create_time = g_nNowTime;
        info.type = AsyncReqInfo::DELETE_BATHC;

        int ret = SendPacketToDCC(reqPacket, Ajs::reqJobDeleteListCid, nUniqueId,
				server_info.first, server_info.second);
		if (ret != 0) {
			LogWarning(
					"CHttpServerApp::ReqDeleteBatch(SendPacketToDCC() failed! flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
			return;
		}

		//发送成功
        m_mapAsyncReqInfo[nUniqueId] = info;
	}
    LogDebug("CHttpServerApp::ReqDeleteBatch(delete batch ok! flow id: %u)", nUniqueId);
}

void CHttpServerApp::ReqSequenceCreate(Json::Value &req, unsigned int nUniqueId)
{
	if (req["job_id_list"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceCreate(job_id_list is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &job_id_list = req["job_id_list"];

	//非数组
	if (!job_id_list.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceCreate(job_id_list is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (job_id_list.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqSequenceCreate(job_id_list is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	ReqSerialJobCreateList reqPacket;
	ReqSerialJobCreate *packet = reqPacket.Append();

    if (req["time_interval"].isInt())
        packet->timeInterval =  req["time_interval"].asInt();
    else packet->timeInterval = g_nSerialTimeInterval; 

	for (Json::Value::ArrayIndex i = 0; i != job_id_list.size(); ++i) {

		Json::Value &request = job_id_list[i];
		if (!request.isString()){
			//如果job id不为string，返回错误
			LogWarning(
					"CHttpServerApp::ReqSequenceCreate(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}


		string strJobId = request.asString();
		AsnOcts* asnStrJobId = packet->jobIdList.Append();
		*asnStrJobId = strJobId.c_str();
	}

	//发送到dcc
    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;

    int ret = SendPacketToDCC(reqPacket, Ajs::reqSerialJobCreateListCid, nUniqueId,
			g_nSerialServerIp, g_nSerialServerPort);
	if (ret != 0) {
		LogWarning(
				"CHttpServerApp::ReqSequenceCreate(SendPacketToDCC() failed! flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
		return;
	}

	//发送成功
    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqSequenceCreate(sequence create ok! flow id: %u)",
            nUniqueId);
}

void CHttpServerApp::ReqSequenceErase(Json::Value &req, unsigned int nUniqueId)
{
	if (req["job_id_list"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceErase(job_id_list is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &job_id_list = req["job_id_list"];

	//非数组
	if (!job_id_list.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceErase(job_id_list is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (job_id_list.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqSequenceErase(job_id_list is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'job_id_list', error:'empty value", nUniqueId);
		return;
	}

	int sequence_id = 0;
	if (!req["sequence_id"].isString()){
		LogWarning(
				"CHttpServerApp::ReqSequenceErase(sequence_id is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'sequence_id', error:'invalid value", nUniqueId);
		return;
	} else {
		string s_id = req["sequence_id"].asString();
		sequence_id = StringToInt(s_id);
		string tmp = IntToString(sequence_id);
		if (tmp != s_id){
			LogWarning(
					"CHttpServerApp::ReqSequenceErase(sequence_id is invalid, flow id: %u)",
                    nUniqueId);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
                            + "param:'sequence_id', error:'invalid value", nUniqueId);
			return;
		}
	}

	ReqSerialJobEraseList reqPacket;
	ReqSerialJobErase *packet = reqPacket.Append();

	packet->type = 0;
	packet->taskId = sequence_id;
	for (Json::Value::ArrayIndex i = 0; i != job_id_list.size(); ++i) {

		Json::Value &request = job_id_list[i];
		if (!request.isString()){
			//如果job id不为string，返回错误
			LogWarning(
					"CHttpServerApp::ReqSequenceErase(job_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'job_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}


		string strJobId = request.asString();
		AsnOcts* asnStrJobId = packet->jobIdList.Append();
		*asnStrJobId = strJobId.c_str();
	}

	//发送到dcc
    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;

    int ret = SendPacketToDCC(reqPacket, Ajs::reqSerialJobEraseListCid, nUniqueId,
			g_nSerialServerIp, g_nSerialServerPort);
	if (ret != 0) {
		LogWarning(
				"CHttpServerApp::ReqSequenceErase(SendPacketToDCC() failed! flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
		return;
	}

	//发送成功
    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqSequenceErase(sequence create ok! flow id: %u)",
            nUniqueId);
}

void CHttpServerApp::ReqSequenceDelete(Json::Value &req, unsigned int nUniqueId)
{
	if (req["sequence_id_list"].isNull()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceDelete(job_id_list is null, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'sequence_id_list', error:'empty value", nUniqueId);
		return;
	}

	Json::Value &sequence_id_list = req["sequence_id_list"];

	//非数组
	if (!sequence_id_list.isArray()) {
		LogWarning(
				"CHttpServerApp::ReqSequenceDelete(sequence_id_list is not array, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'sequence_id_list', error:'invalid value", nUniqueId);
		return;
	}

	//空数组
	if (sequence_id_list.size() == 0){
		LogWarning(
				"CHttpServerApp::ReqSequenceDelete(sequence_id_list is empty, flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(
				BAD_JSON_REQUEST,
				BAD_JSON_REQUEST_REASON
                        + "param:'sequence_id_list', error:'empty value", nUniqueId);
		return;
	}

	ReqSerialJobDeleteList reqPacket;

	for (Json::Value::ArrayIndex i = 0; i != sequence_id_list.size(); ++i) {

		Json::Value &request = sequence_id_list[i];

		if (!request.isString()){
			//如果job id不为string，返回错误
			LogWarning(
					"CHttpServerApp::ReqSequenceDelete(sequence_id is not string, flow id: %u, index of array: %u)",
                    nUniqueId, (unsigned int)i);
            SendErrHttpRspByUniqueId(
					BAD_JSON_REQUEST,
					BAD_JSON_REQUEST_REASON
							+ "param:'sequence_id',error:'invalid value', index of array:"
							+ IntToString((unsigned int)i),
                            nUniqueId);
			return;
		}


		ReqSerialJobDelete *packet = reqPacket.Append();
		packet->taskId = StringToInt(request.asString());
	}

	//发送到dcc
    AsyncReqInfo info;
    info.ccd_flow = m_nFlow;
    info.append = m_nNowAppend;
    info.uniq_id = m_nUniqueId;
    info.create_time = g_nNowTime;

    int ret = SendPacketToDCC(reqPacket, Ajs::reqSerialJobDeleteListCid, nUniqueId,
			g_nSerialServerIp, g_nSerialServerPort);
	if (ret != 0) {
		LogWarning(
				"CHttpServerApp::ReqSequenceDelete(SendPacketToDCC() failed! flow id: %u)",
                nUniqueId);
        SendErrHttpRspByUniqueId(GATWAY_TIMEOUT, GATWAY_TIMEOUT_REASON, nUniqueId);
		return;
	}

	//发送成功
    m_mapAsyncReqInfo[nUniqueId] = info;
	LogDebug("CHttpServerApp::ReqSequenceDelete(sequence delete ok! flow id: %u)",
            nUniqueId);
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
