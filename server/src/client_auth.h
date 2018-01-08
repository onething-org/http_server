#ifndef __CLIENT_AUTH_H__
#define __CLIENT_AUTH_H__

#include <string>
#include <mysql++/mysql++.h>
#include "util.h"

using namespace std;
using namespace mtnc_lib;
using namespace mysqlpp;

typedef struct __CLIENT_INFO {
	bool enable;
	string passwd;
} UserInfo;

class CHttpServerApp;

class CClientAuth: public CThread {
public:
	CClientAuth(CHttpServerApp *ptr);
	virtual ~CClientAuth();
public:
	void SetDbInfo(const string &strDbName = "ajs", const string& strHost = "127.0.0.1", const string &strUser = "root", const string &strPassword = "", unsigned short nPort = 3306);
	void SetTimeInterval(int interval);

private:
	bool Connect();
	void DisConnect();
	void Store(StoreQueryResult &result, Query &query);
	Query GetQuery();
	
	virtual void Run();

private:
	CHttpServerApp* m_httpServerApp;
	map<int, UserInfo> m_AuthInfos;
	mysqlpp::Connection m_mysqlConn;
	string m_strHost;
	string m_strDb;
	string m_strUser;
	string m_strPassword;
	unsigned short m_nPort;
	int m_nTimeInterval;
	time_t m_nTimeLastUpdate;
	bool m_bContinue;
};

#endif
