#include "mcd_frame.h"
#include "tfc_net_epoll_flow.h"

const unsigned char FLG_CTRL_RELOAD = 0x01;
const unsigned char FLG_CTRL_QUIT = 0x02;

#define JOB_SERVER_ASN_BUF_SIZE (10 * 1024 * 1024)

static CCheckFlag g_checkFlag;
int g_nLogStrLen = 1024;

void PrintStr(const string &prompt, const string &content, unsigned int lenth, unsigned int flow)
{
	string format = prompt;
	format += "%-.";
	format += IntToString(lenth);
	format += "s, flow id is: %u";
	LogLowest(format.c_str(), content.c_str(), flow);
}

void disp_dcc(void *priv);
void disp_ccd(void *priv);

static void sigusr1_handle(int sig_val) {
    g_checkFlag.SetFlag(FLG_CTRL_RELOAD);
    signal(SIGUSR1, sigusr1_handle);
}

static void sigusr2_handle(int sig_val) {
    g_checkFlag.SetFlag(FLG_CTRL_QUIT);
    signal(SIGUSR2, sigusr2_handle);
}

void CTimeout::on_expire() {
    m_pMCDFrame->OnExpire(this->_msg_seq);
}

int CMCDFrame::Init(const std::string &conf_file) {
    try {
		m_page.Init(conf_file);


        m_pMQCCD2MCD = _mqs["mq_ccd_2_mcd"];
        m_pMQMCD2CCD = _mqs["mq_mcd_2_ccd"];
        m_pMQDCC2MCD = _mqs["mq_dcc_2_mcd"];
        m_pMQMCD2DCC = _mqs["mq_mcd_2_dcc"];

        if(m_pMQCCD2MCD == NULL || m_pMQMCD2DCC == NULL || m_pMQDCC2MCD == NULL || m_pMQMCD2CCD == NULL)
            return -1;

        signal(SIGUSR1, sigusr1_handle);
        signal(SIGUSR2, sigusr2_handle);

        m_lastCheckTime = time(NULL);
    }
    catch (exception &ex) {
        return -1;
    }

    return 0;
}

void CMCDFrame::run(const std::string &conf_file) {
    if (Init(conf_file))
        return;

    m_nPacketBufSize = JOB_SERVER_ASN_BUF_SIZE;
    m_pPacketBuf = (char*)malloc(m_nPacketBufSize);
    if (m_pPacketBuf == NULL) {
        LogError("CMCDFrame::run(m_pPacketBuf == NULL)");
        exit(EXIT_FAILURE);
    }

    if (add_mq_2_epoll(m_pMQCCD2MCD, disp_ccd, this)) {
        LogError("CMCDFrame::run(add mq_ccd_2_mcd to epoll error)");
        exit(EXIT_FAILURE);
    }

    if (add_mq_2_epoll(m_pMQDCC2MCD, disp_dcc, this)) {
        LogError("CMCDFrame::run(add mq_dcc_2_mcd to epoll error)");
        exit(EXIT_FAILURE);
    }

    // 子类的动作
    ChildAction();

    while  (!stop) {
        if (CheckFlags() > 0) {
            break;
        }

        CheckTimeOut();

        run_epoll_4_mq();

        TimeoutHandler();
    }

    free(m_pPacketBuf);
    m_pPacketBuf = NULL;
    m_nPacketBufSize = 0;
}

int CMCDFrame::DispatchCCD() {
    try {
        unsigned int data_len = 0;
        unsigned int flow = 0;
        int ret = 0;

        for (unsigned int i = 0; i < 1000; i++) {
            data_len = 0;
            ret = m_pMQCCD2MCD->try_dequeue(m_pPacketBuf, m_nPacketBufSize, data_len, flow);
            if ((ret == 0) && (data_len > 0)) {
				LogDebug("CMCDFrame::DispatchCCD(): ret = %d, data_len = %u", ret, data_len);
				TCCDHeader * ccd_head = (TCCDHeader *)m_pPacketBuf;
				LogDebug("CMCDFrame::DispatchCCD(): ccd_head->_type = %d", (int)ccd_head->_type);
				if (ccd_head->_type != ccd_rsp_data)
				{
					LogDebug("CMCDFrame::DispatchCCD() continue: ccd_head->_type = %d", (int)ccd_head->_type);
					continue;
				}
                size_t head_len = sizeof(TCCDHeader);
				LogDebug("CMCDFrame::DispatchCCD(), head_len = %d", (int)head_len);
                ret = (int)(data_len - head_len);
                string test(m_pPacketBuf + head_len, ret);
                unsigned int ip = ((TCCDHeader *)m_pPacketBuf)->_ip;
				LogDebug("CMCDFrame::DispatchCCD(), %d", ip);
                LogDebug("CMCDFrame::DispatchCCD( receive ccd data len: %d, flow id: %u, client ip: %s)", data_len, flow, IpIntToString(ip).c_str());
                PrintStr("CMCDFrame::DispatchCCD( data content:\n", test, g_nLogStrLen, flow);

                CHttpReqPkt packet(m_pPacketBuf + head_len, ret);

				int ret = packet.Init();
                if (ret == 0) {
                    ReceiveDataCCD2MCD(packet, flow, ((TCCDHeader *)m_pPacketBuf)->_ip, ((TCCDHeader *)m_pPacketBuf)->_port);
				} else {
					LogError("CMCDFrame::DispatchCCD(CHttpReqPkt Init error, return ret: %d)", ret);
					SendErrHttpRsp(500, "HTTP_INIT_ERROR", flow);
				}
            }
            else if (ret != 0 && (int)data_len > m_nPacketBufSize) {
                // 缓冲区大小不够
                free(m_pPacketBuf);
                m_nPacketBufSize = data_len;
                m_pPacketBuf = (char*) malloc(m_nPacketBufSize);
                if (m_pPacketBuf == NULL){
					LogError("CMCDFrame::DispatchCCD(m_pPacketBuf == NULL)");
                    exit(EXIT_FAILURE);
				}
            }
            else
                break;
        }
    }
    catch (exception &ex) {
        return -1;
    }

    return 0;
}

int CMCDFrame::DispatchDCC() {
    try {
        unsigned int data_len = 0;
        unsigned int flow = 0;
        int ret = 0;

        for (unsigned int i = 0; i < 1000; i++) {
            ret = m_pMQDCC2MCD->try_dequeue(m_pPacketBuf, m_nPacketBufSize, data_len, flow);
            if((ret == 0) && (data_len > 0)) {
				LogDebug("CMCDFrame::DispatchDCC(data_len is: %d)", data_len);
				TDCCHeader * dcc_head = (TDCCHeader *)m_pPacketBuf;
				unsigned int ip = dcc_head->_ip;
				unsigned short port = dcc_head->_port;
				if (dcc_head->_type != dcc_rsp_data){
					LogInfo("CMCDFrame::DispatchDCC(dcc_head->_type != dcc_rsp_data)");
					continue;
				}
                size_t head_len = sizeof(TDCCHeader);
				size_t body_len = (size_t) (data_len - head_len);
                if (body_len > 0) {
                    AjsPacket packet;
					unsigned int retu;
                    if (ParseFromBuffer(packet, retu, m_pPacketBuf + head_len, body_len) == 0){
                        // ReceiveDataDCC2MCD(packet, ip, port);
					}
                }else{
					LogInfo("CMCDFrame::DispatchDCC(body_len <= 0)");
				}
            }
            else if (ret != 0 && (int)data_len > m_nPacketBufSize) {
				LogDebug("CMCDFrame::DispatchDCC(), else if");
                // 缓冲区大小不够
                free(m_pPacketBuf);
                m_nPacketBufSize = data_len;
                m_pPacketBuf = (char*) malloc(m_nPacketBufSize);
                if (m_pPacketBuf == NULL){
					LogError("CMCDFrame::DispatchDCC(m_pPacketBuf == NULL)");
                    exit(EXIT_FAILURE);
				}
            }
            else
                break;
        }
    }
    catch( exception &ex) {
        return -1;
    }

    return 0;
}

int CMCDFrame::SendDataMCD2CCD(const char *pData, unsigned int nSize, unsigned int nFlow) {
	size_t head_len = sizeof(TCCDHeader);
	if (m_nPacketBufSize < (int)(head_len + nSize)) {
		m_nPacketBufSize = nSize + head_len;
		free(m_pPacketBuf);
		m_pPacketBuf = (char *)malloc(m_nPacketBufSize);
	}

	TCCDHeader ccdHeader;
	ccdHeader._type = ccd_req_data;

	memcpy(m_pPacketBuf, &ccdHeader, head_len);
	memcpy(m_pPacketBuf + head_len, pData, nSize);

    return m_pMQMCD2CCD->enqueue(m_pPacketBuf, nSize + head_len, nFlow);
}

int CMCDFrame::SendDataMCD2DCC(const char* pData, unsigned int nSize, unsigned int nIp, unsigned short nPort) {
    size_t head_len = sizeof(TDCCHeader);
    if(nSize + head_len > (unsigned int)m_nPacketBufSize){
		m_nPacketBufSize = nSize + head_len;
		free(m_pPacketBuf);
		m_pPacketBuf = (char *)malloc(m_nPacketBufSize);
	}

    TDCCHeader stDccHeader;
    stDccHeader._ip = nIp;
    stDccHeader._port = nPort;
    stDccHeader._type = dcc_req_send;
    memcpy(m_pPacketBuf, &stDccHeader, head_len);

    memcpy(m_pPacketBuf + head_len, pData, nSize);

    unsigned int nFlow = (nIp >> 16) + (nPort << 16);

    return m_pMQMCD2DCC->enqueue(m_pPacketBuf, nSize + head_len, nFlow);
}

void CMCDFrame::AddToTimeoutQueue(unsigned int id, unsigned int gap/* = 10*/) {
    // 注意：在超时处理函数中调用该函数时，如果再添加新的超时，并且新的id跟当前正在超时的id一样，则会出问题，根本原因是对map的操作
    DeleteFromTimeoutQueue(id);

    CTimeout *pInfo = new CTimeout(this);
    m_timeOutQueue.set(id, pInfo, gap);
}

void CMCDFrame::DeleteFromTimeoutQueue(unsigned int id) {
    tfc::base::CSimpleTimerInfo* pInfo = NULL;
    if (m_timeOutQueue.get(id, &pInfo) == 0)
        delete pInfo;
}

void CMCDFrame::SetTimer(int nTimer/* = 0*/) {
    m_nTimer = nTimer;
    m_lastTimer = time(NULL);
}

void CMCDFrame::AdjustAsnBufToDefault() {
    if (m_nPacketBufSize == JOB_SERVER_ASN_BUF_SIZE)
        return;

    free(m_pPacketBuf);
    m_nPacketBufSize = JOB_SERVER_ASN_BUF_SIZE;
    m_pPacketBuf = (char*)malloc(m_nPacketBufSize);
    if (m_pPacketBuf == NULL) {
		LogError("CMCDFrame::AdjustAsnBufToDefault(m_pPacketBuf == NULL)");
        exit(EXIT_FAILURE);
	}
}

void CMCDFrame::CheckTimeOut() 
{
	time_t cur = time(NULL);
    if (m_lastTimer > cur)
        m_lastTimer = cur;

    if (m_nTimer > 0 && cur - m_lastTimer >= m_nTimer) {
        m_lastTimer = cur;
        OnTimer(cur);
    }

    m_timeOutQueue.check_expire(cur);
}

int CMCDFrame::CheckFlags() {
    if (g_checkFlag.IsFlagSet(FLG_CTRL_RELOAD)) {
        OnSignalUser1();
        g_checkFlag.ClearFlag();
    }

    if (g_checkFlag.IsFlagSet(FLG_CTRL_QUIT)) {
        OnSignalUser2();
        g_checkFlag.ClearFlag();
    }

    return 0;
}

void disp_dcc(void *priv)
{
	CMCDFrame *app = (CMCDFrame *)priv;
	app->DispatchDCC();
}

void disp_ccd(void *priv)
{
	CMCDFrame *app = (CMCDFrame *)priv;
	app->DispatchCCD();
}
