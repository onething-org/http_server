#ifndef _MCD_FRAME_H_
#define _MCD_FRAME_H_

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string>
#include <iostream>

#include "tfc_stat.h"
#include "tfc_cache_proc.h"
#include "tfc_base_timer.h"
#include "tfc_base_config_file.h"
#include "tfc_net_ccd_define.h"
#include "tfc_net_dcc_define.h"

#include "asn-incl.h"
#include "ajs_pkt.h"
#include "util.h"
#include "ethnet.h"
#include "http_pkt.h"
#include "asn_parse.h"
#include "json_helper.h"

using namespace tfc;
using namespace tfc::base;
using namespace std;
using namespace mtnc_lib;

void PrintStr(const string& prompt, const string &content, unsigned int lenth, unsigned int flow);

class CMCDFrame;

class CCheckFlag {
public:
    CCheckFlag(): m_nFlag((unsigned char)0){}
    ~CCheckFlag(){}
    void SetFlag(unsigned char flag){ m_nFlag |= flag;}
    bool IsFlagSet( unsigned char flag ) { return (( m_nFlag & flag ) == flag); }
    void ClearFlag(){ m_nFlag = (unsigned char)0;}

private:
    unsigned char m_nFlag;
};

class CTimeout : public tfc::base::CSimpleTimerInfo {
public:
    CTimeout(CMCDFrame* pFrame) : m_pMCDFrame(pFrame){}

private:
    virtual void on_expire();

private:
    CMCDFrame* m_pMCDFrame;
};

class CMCDFrame : public tfc::cache::CacheProc {
    friend class CTimeout;
public:
    CMCDFrame() {
        m_pPacketBuf = NULL;
        m_nPacketBufSize = 0;

        m_pMQCCD2MCD = NULL;
        m_pMQMCD2CCD = NULL;
        m_pMQDCC2MCD = NULL;
        m_pMQMCD2DCC = NULL;
        m_lastCheckTime = time(NULL);
        m_bQuit = false;
        m_nTimer = 0;
    }

    virtual ~CMCDFrame(){}
    virtual void run(const std::string& conf_file);

    int DispatchCCD();
    int DispatchDCC();

protected:
    // 下面4个函数，子类可以选择性的继承
    virtual void ReceiveDataCCD2MCD(CHttpReqPkt &packet, unsigned int nFlow, unsigned int nIp, unsigned short nPort){}
    virtual void ReceiveDataDCC2MCD(AjsPacket &packet, unsigned int nIp, unsigned short nPort){}
    virtual void OnExpire(unsigned int id){}
    virtual void OnTimer(time_t cur){}
    virtual void OnSignalUser1(){}
    virtual void OnSignalUser2(){}
    virtual void SendErrHttpRsp(int code,
    		   	const string& reason,
    		   	unsigned int &nFlow
    			){}

	//孩子类动作
	virtual void ChildAction() {}

    virtual void TimeoutHandler() {}

    // 下面的8个函数，子类可以调用，用以完成任务
	/*
	template<typename Type> int SendDataMCD2CCD(Type& elem, unsigned int nFlow) {
		unsigned int nSize = 0;
		char* pTemp = SerializeToBufferPrivate(nSize, elem);
		if (pTemp != NULL)
			return m_pMQMCD2CCD->enqueue(pTemp, nSize, nFlow); 
		else    
			return -1;
	}
	*/

    template<typename Type> int SendDataMCD2DCC(Type& elem, unsigned int nIp, unsigned short nPort)
	{
		size_t head_len = sizeof(TDCCHeader);

		unsigned int nSize = 0;
		char* pTemp = SerializeToBufferPrivate(nSize, elem, head_len);

		if (pTemp != NULL) {
			TDCCHeader stDccHeader;
			stDccHeader._ip = nIp;
			stDccHeader._port = nPort;
			stDccHeader._type = dcc_req_send;

			pTemp -= head_len;
			memcpy(pTemp, &stDccHeader, head_len);
			
			unsigned int nFlow = (nIp >> 16) + (nPort << 16);

			return m_pMQMCD2DCC->enqueue(pTemp, nSize + head_len, nFlow);
		} else return -1;
	}

    int SendDataMCD2CCD(const char* pData, unsigned int nSize, unsigned int nFlow);
    int SendDataMCD2DCC(const char* pData, unsigned int nSize, unsigned int nIp, unsigned short nPort);

    template<typename Type> char* PacketObjectToBuf(unsigned int& nSize, Type& elem) {
        return SerializeToBufferPrivate(nSize, elem);
    }

    void AddToTimeoutQueue(unsigned int id, unsigned int gap = 10);
    void DeleteFromTimeoutQueue(unsigned int id);
    void SetTimer(int nTimer = 0);

    void AdjustAsnBufToDefault();

private:
    int Init(const std::string& conf_file);
    int CheckFlags();
    void CheckTimeOut();
    template<typename Type> char* SerializeToBufferPrivate(unsigned int& nSize, Type& elem, int nReservdSize = 0) {
        char* pBuffer = NULL;
        if (SerializeToBuffer(pBuffer, nSize, m_pPacketBuf + nReservdSize, m_nPacketBufSize - nReservdSize, elem) == 0)
            return pBuffer;

        if ((int)nSize > (m_nPacketBufSize - nReservdSize)) {
            m_nPacketBufSize = nSize + nReservdSize;
            free(m_pPacketBuf);
            m_pPacketBuf = (char*) malloc(m_nPacketBufSize);
            if (m_pPacketBuf == NULL){
				cout << "SerializeToBufferPrivate(m_pPacketBuf == NULL)" << endl;
                exit(EXIT_FAILURE);
			}

            if (SerializeToBuffer(pBuffer, nSize, m_pPacketBuf + nReservdSize, m_nPacketBufSize - nReservdSize, elem) == 0)
                return pBuffer;
        }

        return NULL;
    }

private:
    tfc::net::CFifoSyncMQ* m_pMQCCD2MCD;
    tfc::net::CFifoSyncMQ* m_pMQMCD2CCD;

    tfc::net::CFifoSyncMQ* m_pMQDCC2MCD;
    tfc::net::CFifoSyncMQ* m_pMQMCD2DCC;

    //TFCObject m_objTFC;
	CFileConfig m_page;
    time_t m_lastCheckTime;
    bool m_bQuit;

    tfc::base::CSimpleTimerQueue m_timeOutQueue;
    int m_nTimer;
    time_t m_lastTimer;

    char* m_pPacketBuf;
    int m_nPacketBufSize;
};

#endif
