#include "http_server.h"
#include <utility>

CClientAuth::CClientAuth(CHttpServerApp *ptr)
	:m_httpServerApp(ptr),m_mysqlConn(false),m_nPort(0),m_nTimeInterval(0),m_nTimeLastUpdate(0),m_bContinue(true)
{}

CClientAuth::~CClientAuth()
{
	m_bContinue = false;
	m_httpServerApp = NULL;
}

void CClientAuth::SetDbInfo(const string &strDbName , const string &strHost, const string &strUser, const string &strPassword, unsigned short nPort)
{
	m_strHost = strHost;
	m_strDb = strDbName;
	m_strUser = strUser;
	m_strPassword = strPassword;
	m_nPort = nPort;
}

void CClientAuth::SetTimeInterval(int interval) {
	m_nTimeInterval = interval;
}

bool CClientAuth::Connect()
{
	LogInfo("CClientAuth::Connect(db:%s, host:%s, user:%s, passwd:%s, port:%d)", m_strDb.c_str(), m_strHost.c_str(), m_strUser.c_str(), m_strPassword.c_str(), m_nPort);

    m_mysqlConn.disconnect();

    m_mysqlConn.set_option(new SetCharsetNameOption("utf8"));
    m_mysqlConn.set_option(new MultiStatementsOption(true));
    m_mysqlConn.set_option(new MultiResultsOption(true));
    bool bOk = m_mysqlConn.connect(m_strDb.c_str(), m_strHost.c_str(), m_strUser.c_str(), m_strPassword.c_str(), m_nPort);

	if (bOk) {
		LogInfo("CClientAuth::Connect(connect to the db is ok)");
		return true;
	} else {
		int err = m_mysqlConn.errnum();
		LogError("CClientAuth::Connect(connect to the db is failed, errnum: %d)", err);
		return false;
	}
}

void CClientAuth::DisConnect()
{
	m_mysqlConn.disconnect();
}

void CClientAuth::Store(StoreQueryResult &result, Query &query)
{
	result = query.store();
}

Query CClientAuth::GetQuery()
{
	return m_mysqlConn.query();
}

void CClientAuth::Run()
{
	LogInfo("CClientAuth::Run()");
	m_mysqlConn.thread_start();
	time_t nTimeNow = time(NULL);
	m_nTimeLastUpdate = nTimeNow;
	bool flag = true;
	while(m_bContinue)
	{
		nTimeNow = time(NULL);
		if (flag || nTimeNow - m_nTimeLastUpdate >= m_nTimeInterval)
		{
			if(!Connect())
			{
				sleep(30);
				continue;
			}

			Query query = GetQuery();
			query << "select client_module, password, disable from tb_user";
			StoreQueryResult result;
			Store(result, query);

			m_AuthInfos.clear();
			for(size_t i = 0; i != result.size(); ++i)
			{
				int client_module = (int)result[i]["client_module"];
				string password = (const char*)result[i]["password"];
				int    disable = (int)result[i]["disable"];
				UserInfo info;
				info.enable = disable > 0 ? false : true;
				info.passwd = password;
				m_AuthInfos.insert(make_pair(client_module, info));
			}

			query << "";

			DisConnect();
			m_httpServerApp->UpdateModuleAuth(m_AuthInfos);

			m_nTimeLastUpdate = nTimeNow;
			flag = false;
		}
		sleep(20);
	}
}
