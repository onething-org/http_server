#include "tfc_cache_proc.h"
#include "tfc_net_ccd_define.h"
#include "tfc_net_dcc_define.h"
#include "tfc_base_config_file.h"
#include "tfc_base_str.h"

#include "mcd_frame.h"

#include "statistic.h"
#include "client_auth.h"

#include <iostream>
#include <cstring>
#include <map>
#include <set>
#include <utility>

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

static unsigned int g_nCreateSyncReTryCount = 0;

using namespace std;
using namespace tfc::net;

class CHttpReqPkt;

typedef struct __CONFIRM_INFO {
    int id;
    int stat;
} ConfirmInfo;

typedef struct __RESULT_INFO {
    int id;
    string ip;
    int port;
    string type;
    string host;
} ResultInfo;

typedef struct __PORT_TYPE
{
    unsigned int port;
    string type;
    string hostname;
} PortType;

typedef struct __IP_PORT_TYPE
{
    string ip;
    unsigned int port;
    string type;
    string hostname;

    bool operator < (const __IP_PORT_TYPE &other) const
    {
        if (ip < other.ip)
            return true;
        else if (port < other.port)
            return true;
        else if (type < other.type)
            return true;
        else
            return false;
    }
} IpPortType;

typedef struct __SYNC_REQ_INFO {
	enum {                                   //同步任务状态
		CREATE_SYNC_WAIT_REQ_CREATE = 0,
		CREATE_SYNC_WAIT_RSP_CREATE,
		CREATE_SYNC_WAIT_REQ_QUERY_FIELD,
		CREATE_SYNC_WAIT_RSP_QUERY_FIELD,
		CREATE_SYNC_WAIT_FINISH
	};

	enum {
		CREATE_SYNC = 0,
		CREATE_SYNC2
	};

    unsigned int              uniq_id;
	unsigned int              ccd_flow;      //ccd flow
	int                       type;          //请求类型
	unsigned int              append;        //客户端附加字段
	int       				  retry_count;   //同步任务查询状态的重试次数
	int                       status;        //同步任务状态
	time_t                    last_send_time;//上一次发送查询包的时间
	unsigned int              ip;            //job server ip
	unsigned short            port;          //job server port
	int                       job_id;        //job id

	__SYNC_REQ_INFO()
		:uniq_id(0), ccd_flow(0), type(-1)
		,append(0), retry_count(g_nCreateSyncReTryCount)
		,status(0) ,last_send_time(0)
		,ip(0), port(0), job_id(0)
	{}
} SyncReqInfo;

typedef struct __ASYNC_REQ_INFO {
	enum {
		CREATE = 0,
		CREATE2,
		CREATE2_BATCH,
		SETACTION,
		SETACTION_BATCH,
		UPDATE,
		UPDATE_BATHC,
		QUERY,
		QUERY2,
		QUERY2_BATCH,
		QUERY_FIELD,
		QUERY_FIELD_BATCH,
		DELETE,
		DELETE_BATHC,
		QUERY_SELECT,
		CREATE_MANY,
		QUERY_TIME,
		QUERY_AJS_AGENT,
		QUERY_TSC_AGENT
	};
    unsigned int              uniq_id;
	unsigned int              ccd_flow;    //ccd flow
	int                       type;        //请求类型
	unsigned int              append;      //客户端附加字段
	time_t                    create_time; //创建时间

	__ASYNC_REQ_INFO()
		:uniq_id(0), ccd_flow(0), type(-1), append(0), create_time(0){}

} AsyncReqInfo;

class CScheduler{
public:
	virtual ~CScheduler(){}
	virtual bool Init(const set<unsigned short> &job_server) = 0;
	virtual bool GetAJobServer(unsigned short &job_server_id) = 0;
	virtual bool GetAllJobServer(vector<unsigned short> &serverVct ) = 0;
	virtual bool UpdateLoad(unsigned short job_server_id, int load) = 0;
};

class CRotationScheduler: public CScheduler{
public:
	CRotationScheduler(){}
	virtual ~CRotationScheduler(){}

	virtual bool Init(const set<unsigned short> &job_server);
	virtual bool GetAJobServer(unsigned short &job_server_id);
	virtual bool GetAllJobServer(vector<unsigned short> &serverVct );
	virtual bool UpdateLoad(unsigned short job_server_id, int load){return true;};
private:
	set<unsigned short>                      m_jobServer_set;
	set<unsigned short>::iterator            m_current_it;
};

class CHttpServerApp : public CMCDFrame {
public:
	CHttpServerApp();
	virtual ~CHttpServerApp();

	virtual void ReceiveDataCCD2MCD(CHttpReqPkt &packet, unsigned int nFlow, unsigned int nIp, unsigned short nPort);

    virtual void OnExpire(unsigned int nUniqueId);
	virtual void OnTimer(time_t cur);
	virtual void OnSignalUser1();
	virtual void OnSignalUser2();

	virtual void ChildAction();

    virtual void TimeoutHandler();

	virtual void SendErrHttpRsp(int code, const string &reason, unsigned int &nFlow);
    virtual void SendErrHttpRspByUniqueId(int code, const string &reason, unsigned int &nUniqueId);

private:
	template<typename Type> 
	int SendPacketToDCC(Type& obj, enum Ajs::ChoiceIdEnum nType, unsigned int nAppend, unsigned int nIp, unsigned short nPort);
	int SendPacketToCCD(CHttpRspPkt &packet, unsigned int nFlow);
	void SendHttpRsp(const Json::Value &response, unsigned int client_id);
    void SendHttpRspByUniqueId(const Json::Value &response, unsigned int nUniqueId);

    void ReqRiskyPort(Json::Value &request, unsigned int nUniqueId);
    void ReqWhiteList(Json::Value &request, unsigned int nUniqueId);
    void ReqNormalPort(Json::Value &request, unsigned int nUniqueId);

    void SendQueryAll(unsigned int nFlow);

	void LoadCfg();
	void HandleJsonRequest(Json::Value &request, unsigned int nFlow);
	void ReqJobServerLoad(){}
	//void ReqJobServerAllJobId();
	void DealSyncTasks(time_t cur);
	void DealAsyncTasks(time_t cur);
	void AdjustAsnBuf(time_t cur);
    void AdjustUniqueBuf(time_t cur);
	void LogStatisticInfo(time_t cur);
	void PrintJobServerInfos();
	void FillQueryResult(Json::Value& root, const RspJobQueryAll* pSeq, unsigned int nIp);
	bool GetJobServerInfoById(unsigned short job_server_id, pair<unsigned int, unsigned short> &info);
	bool GetJobServerByClientModule(int client_module, pair<unsigned int, unsigned short> &server_info);
	bool GetAJobServerInScheduler(pair<unsigned int, unsigned short> & host);
	bool GetAllJobServerInScheduler(vector<pair<unsigned int, unsigned short> > & hosts);
	bool AccessControl(unsigned int nIp, unsigned short nPort)
	{
		return m_validClientIp_set.find(nIp) != m_validClientIp_set.end();
	}
	bool ClientModuleAuth(int client_module, const string &passwd);

public:
	void UpdateModuleAuth(const map<int, UserInfo> &infos);
	unsigned int GetEmptyUniqueID(unsigned int nClientId, unsigned int nSessionId);

    void AnalyzeRisk();

    void die_on_error(int x, char const *context);
    void die_on_amqp_error(amqp_rpc_reply_t x, char const *context);
    void SendDataToRMQ();
    void SendDataToRMQ(amqp_connection_state_t conn, string &key, string &data);

private:
	CScheduler                                                *m_pScheduler;                 //调度器
	CStatistic* m_pStatistic;

	set<unsigned int>                                         m_validClientIp_set;           //所有合法的客户端ip

	map<unsigned short, pair<unsigned int, unsigned short> >  m_allJobServer_map;            //所有的jobserver及对应的id
	set<unsigned short>                                       m_schedulerJobServer_set;      //所有参加调度的jobserver
	map<int, unsigned short>                                  m_clientModuleSpecializedJobServer;//client_mole专有job_server
	map<unsigned int , unsigned short>                        m_jobServerIp2Id_map;          //jobserver ip to id;
	
	map<unsigned int, SyncReqInfo>                            m_mapSyncReqInfo;              //同步客户端请求信息
	map<unsigned int, AsyncReqInfo>                           m_mapAsyncReqInfo;             //异步客户端请求信息
	map<unsigned int, unsigned long long>                     m_mapUniqueId;                 //唯一id

	int m_logStatisticLastTime;
	int m_logStatisticTimeInterval;
	int m_adjustBufferLastTime;
	int m_adjustBufferTimeInterval;

    int m_adjustUniqueLastTime;
    int m_adjustUniqueTimeInterval;

	int m_nCcdRequests;
	int m_nDccReceives;

	int m_nInvalidCcdRequest;
	int m_nInvalidDccReceives;

	int m_nTimeOutedDccReceives;

	bool m_bPrintJobServerInfo;

	CClientAuth*  m_clientAuth;

	map<int, UserInfo> 									  m_clientAuth_map;          //客户端代码密码验证
	CLock m_lock;

	unsigned int m_nNowAppend;
    unsigned int m_nFlow;
    unsigned int m_nUniqueId;

    int m_analyzeRiskLastTime;
    int m_analyzeRiskTimeInterval;

    map<string, PortType> m_riskPortTypeInfo;
    set<IpPortType> m_riskIpPortType;
    set<string> m_riskyPorts_set;
    set<string> m_riskyServices_set;
    set<unsigned int> m_whiteList_set;
    multimap<string, string> m_resultWhiteList_mmap;
    set<string> m_riskyIpPortType_set;
    map<string, set<string> > m_IpPort_Host_map;
};
