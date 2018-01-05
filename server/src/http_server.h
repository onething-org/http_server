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

static unsigned int   g_nCreateSyncReTryCount = 0;

using namespace std;
using namespace tfc::net;

class CHttpReqPkt;

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
	virtual void ReceiveDataDCC2MCD(AjsPacket &packet, unsigned int nIp, unsigned short nPort);

    virtual void OnExpire(unsigned int nUniqueId);
	virtual void OnTimer(time_t cur);
	virtual void OnSignalUser1();
	virtual void OnSignalUser2();

	virtual void ChildAction();

	virtual void SendErrHttpRsp(int code,
			   	const string& reason,
			   	unsigned int &nFlow
				);

    virtual void SendErrHttpRspByUniqueId(int code,
                const string& reason,
                unsigned int &nUniqueId
                );

private:
	template<typename Type> 
	int SendPacketToDCC(Type& obj, enum Ajs::ChoiceIdEnum nType, unsigned int nAppend, unsigned int nIp, unsigned short nPort);
	int SendPacketToCCD(CHttpRspPkt &packet, unsigned int nFlow);
	void SendHttpRsp(const Json::Value &response, unsigned int client_id);
    void SendHttpRspByUniqueId(const Json::Value &response, unsigned int nUniqueId);

    void ReqRiskyPort(Json::Value &request, unsigned int nUniqueId);
    void ReqWhiteList(Json::Value &request, unsigned int nUniqueId);
    void ReqNormalPort(Json::Value &request, unsigned int nUniqueId);

    void ReqCreate(Json::Value &request, unsigned int nUniqueId);
    void ReqSetAction(Json::Value &request, unsigned int nUniqueId);
    void ReqUpdate(Json::Value &request, unsigned int nUniqueId);
    void ReqQuery(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryField(Json::Value &request, unsigned int nUniqueId);
    void ReqQuerySelect(Json::Value &request, unsigned int nUniqueId);
    void ReqDelete(Json::Value &request, unsigned int nUniqueId);
	//void ReqQuerySys(int type, unsigned int nIp, unsigned short nPort);

    //2.0.0版本新增批量接口
    void ReqCreateBatch(Json::Value &request, unsigned int nUniqueId);
    void ReqSetActionBatch(Json::Value &request, unsigned int nUniqueId);
    void ReqUpdateBatch(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryBatch(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryFieldBatch(Json::Value &request, unsigned int nUniqueId);
    void ReqDeleteBatch(Json::Value &request, unsigned int nUniqueId);
	//串行化接口
    void ReqSequenceCreate(Json::Value &request, unsigned int nUniqueId);
    void ReqSequenceErase(Json::Value &request, unsigned int nUniqueId);
    void ReqSequenceDelete(Json::Value &request, unsigned int nUniqueId);

    void ReqCreateMany(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryTime(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryAJSAgent(Json::Value &request, unsigned int nUniqueId);
    void ReqQueryTSCAgent(Json::Value &request, unsigned int nUniqueId);


	void RspCreate(const RspJobCreateList* packet,
		   	unsigned int nIp,
		   	unsigned short nPort,
            unsigned int nUniqueId
			);
	void RspCreate2(const RspJobCreateList* packet,
		   	unsigned int nIp,
		   	unsigned short nPort,
            unsigned int nUniqueId
			);
	void RspCreateSync2(const void* packet,
		   	unsigned int nIp,
		   	unsigned short nPort,
            unsigned int nUniqueId,
			enum Ajs::ChoiceIdEnum nType
			);
	void RspSetAction(const RspJobSetActionList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspUpdate(const RspJobUpdateList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQuery(const RspJobQueryAllList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQuery2(const RspJobQueryAllList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQueryField(const RspJobQueryFieldList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQuerySelect(RspJobQuerySelect* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspDelete(const RspJobDeleteList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	/*
	void RspQuerySys(RspJobQuerySys* packet,
		   	unsigned int id,
			unsigned int nIp
			);
			*/

    //2.0.0版本新增批量接口
	void RspCreateBatch( RspJobCreateList* packet,
		   	unsigned int nIp,
		   	unsigned short nPort,
            unsigned int nUniqueId
			);
	void RspSetActionBatch( RspJobSetActionList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspUpdateBatch( RspJobUpdateList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQueryBatch( RspJobQueryAllList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspQueryFieldBatch( RspJobQueryFieldList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	void RspDeleteBatch( RspJobDeleteList* packet,
            unsigned int nUniqueId,
			unsigned int ip
			);
	//串行化接口
	void RspSequenceCreate( RspSerialJobCreateList* packet,
                unsigned int nUniqueId,
				unsigned int ip
				);
	void RspSequenceErase( RspSerialJobEraseList* packet,
                    unsigned int nUniqueId,
					unsigned int ip
					);
	void RspSequenceDelete( RspSerialJobDeleteList* packet,
                    unsigned int nUniqueId,
					unsigned int ip
					);

	void RspQueryTime(const RspJobQueryFieldList* packet,
            unsigned int nUniqueId,
			unsigned int ip);

	void RspQueryAJSAgent( RspJobQueryAJSAgent* packet,
            unsigned int nUniqueId,
			unsigned int ip);

	void RspQueryTSCAgent( RspJobQueryTSCAgent* packet,
            unsigned int nUniqueId,
			unsigned int ip);

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
};

