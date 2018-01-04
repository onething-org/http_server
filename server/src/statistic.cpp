#include "statistic.h"

static int fTypeInfoCmp(const void *element1, const void * element2)
{
    TypeKey* pTypeKey1 = &(((TypeInfo*)element1)->m_TypeKey);
    TypeKey* pTypeKey2 = &(((TypeInfo*)element2)->m_TypeKey);

        int iCmpRet = strcmp(pTypeKey1->m_szName,pTypeKey2->m_szName);
    if(iCmpRet != 0)
    {
        return iCmpRet;
    }

    return pTypeKey1->m_iResult - pTypeKey2->m_iResult;
}

CStatistic* CStatistic::m_pInstance = NULL;
CStatistic* CStatistic::Instance()
{
    if(m_pInstance == NULL)
    {
        m_pInstance = new CStatistic();
        m_pInstance->Inittialize("../log/Statistic");
    }
    return m_pInstance;
}

int CStatistic::Inittialize(const char *pszLogBaseFile,int iMaxSize/*=20000000*/,int iMaxNum/*=10*/,
                        int iTimeOutUs1/*=100000*/,
                        int iTimeOutUs2/*=500000*/,
                        int iTimeOutUs3/*=1000000*/)
{
    strcpy(m_szLogBase, pszLogBaseFile);
    m_iLogMaxSize = iMaxSize;
    m_iLogMaxNum = iMaxNum;

    m_iTimeOutUs[0] = iTimeOutUs1;
    m_iTimeOutUs[1] = iTimeOutUs2;
    m_iTimeOutUs[2] = iTimeOutUs3;
    
    ClearStat();
    return 0;
}

CStatistic::CStatistic(bool bUseMutex/*=false*/)
{
    m_iTypeNum = 0;
    memset(m_astTypeInfo,0,sizeof(m_astTypeInfo));
    
    m_bUseMutex = bUseMutex;
    pthread_mutex_init(&m_stMutex, NULL);

    m_iMaxTypeNameLen = 0;
}

CStatistic::~CStatistic()
{
}

int CStatistic::AddStat(char* szTypeName, int iResultID, 
            struct timeval *pstBegin, struct timeval *pstEnd,
            char* szRecordAtMax,int iSumVal,int iStatCount)
{
    if(m_bUseMutex) pthread_mutex_lock(&m_stMutex);
    int iRet = _AddStat(szTypeName,iResultID, pstBegin,pstEnd,szRecordAtMax,iSumVal,iStatCount);
    if(m_bUseMutex) pthread_mutex_unlock(&m_stMutex);
    return iRet;
}
int CStatistic::GetStat(char* szTypeName,int iResultID,TypeInfo &stTypeInfo)
{
    if(m_bUseMutex) pthread_mutex_lock(&m_stMutex);
    int iRet = _GetStat(szTypeName,iResultID,stTypeInfo);
    if(m_bUseMutex) pthread_mutex_unlock(&m_stMutex);
    return iRet;
}

int CStatistic::WriteToFile()
{
    if(m_iTypeNum == 0)
        return 0;

    if(m_bUseMutex) 
        pthread_mutex_lock(&m_stMutex);
    
    unsigned long long ullTmpAveUSec;   
    char szTmpStr[128];
    char szTmpStr2[128];
    char szTmpStr3[128];
    GetDateString(szTmpStr);

    int iStatSpan = time(0)-m_iLastClearTime;
    _WriteLog("\n===============Statistic in %ds, %s=====================\n",iStatSpan,szTmpStr);

    sprintf(szTmpStr,">%.3fms",m_iTimeOutUs[0]/(float)1000);
    sprintf(szTmpStr2,">%.3fms",m_iTimeOutUs[1]/(float)1000);
    sprintf(szTmpStr3,">%.3fms",m_iTimeOutUs[2]/(float)1000);

    char szFormatHead[1024];
    sprintf(szFormatHead,"%%-%ds|%%8s|%%8s|%%8s|%%9s|%%9s|%%9s|%%-18s|%%11s|%%11s|%%11s|\n",
                    m_iMaxTypeNameLen);

    _WriteLog(szFormatHead,
                "","RESULT","TOTAL","SUMVAL","AVG(ms)","MAX(ms)","MIN(ms)","RECATMAX",
                szTmpStr,szTmpStr2,szTmpStr3);

    char szFormat[1024];
    sprintf(szFormat,"%%-%ds|%%8d|%%8u|%%8u|%%9.3f|%%9.3f|%%9.3f|%%-18s|%%11u|%%11u|%%11u|\n",
                    m_iMaxTypeNameLen);
    
    TypeInfo stAllTypeInfo;
    memset(&stAllTypeInfo,0,sizeof(stAllTypeInfo));
    for(int i=0; i<m_iTypeNum; i++)
    {
        ullTmpAveUSec = 0;
        if(m_astTypeInfo[i].m_unAllCount > 0)
        {
            ullTmpAveUSec = 1000000*m_astTypeInfo[i].m_stTime.tv_sec;
            ullTmpAveUSec += m_astTypeInfo[i].m_stTime.tv_usec;
            ullTmpAveUSec /= (unsigned long long)m_astTypeInfo[i].m_unAllCount;     
        }else
        {
            continue;
        }
            
        _WriteLog(szFormat,
                m_astTypeInfo[i].m_TypeKey.m_szName,
                m_astTypeInfo[i].m_TypeKey.m_iResult,               
                m_astTypeInfo[i].m_unAllCount,
                m_astTypeInfo[i].m_unSumVal,
                ((unsigned int)ullTmpAveUSec)/(float)1000,
                m_astTypeInfo[i].m_unMaxTime/(float)1000,
                m_astTypeInfo[i].m_unMinTime/(float)1000,
                m_astTypeInfo[i].m_szRecordAtMax,
                m_astTypeInfo[i].m_unTimeOut[0],
                m_astTypeInfo[i].m_unTimeOut[1],
                m_astTypeInfo[i].m_unTimeOut[2]);

        stAllTypeInfo.m_unAllCount += m_astTypeInfo[i].m_unAllCount;
        stAllTypeInfo.m_unSumVal += m_astTypeInfo[i].m_unSumVal;

        if(stAllTypeInfo.m_unMaxTime < m_astTypeInfo[i].m_unMaxTime)
            stAllTypeInfo.m_unMaxTime = m_astTypeInfo[i].m_unMaxTime;

        if((stAllTypeInfo.m_unMinTime==0)||(stAllTypeInfo.m_unMinTime > m_astTypeInfo[i].m_unMinTime))
            stAllTypeInfo.m_unMinTime = m_astTypeInfo[i].m_unMinTime;
        
        stAllTypeInfo.m_unTimeOut[0] += m_astTypeInfo[i].m_unTimeOut[0];
        stAllTypeInfo.m_unTimeOut[1] += m_astTypeInfo[i].m_unTimeOut[1];
        stAllTypeInfo.m_unTimeOut[2] += m_astTypeInfo[i].m_unTimeOut[2];
    }

    char szFormatTail[1024];
    sprintf(szFormatTail,"%%-%ds|%%8d|%%8u|%%8u|%%9.3f|%%9.3f|%%9.3f|                  |%%11u|%%11u|%%11u|\n",
                    m_iMaxTypeNameLen);
    
    float fTmpZero = 0.00;
    _WriteLog("----------------------------------------------------------------------------------------\n");    
    _WriteLog(szFormatTail,
            "ALL",
            0,          
            stAllTypeInfo.m_unAllCount,
            stAllTypeInfo.m_unSumVal,
            fTmpZero,
            stAllTypeInfo.m_unMaxTime/(float)1000,
            stAllTypeInfo.m_unMinTime/(float)1000,
            stAllTypeInfo.m_unTimeOut[0],
            stAllTypeInfo.m_unTimeOut[1],
            stAllTypeInfo.m_unTimeOut[2]);

    _ShiftFiles();
    
    if(m_bUseMutex) 
        pthread_mutex_unlock(&m_stMutex);
    
    return 0;
}

void CStatistic::ClearStat()
{
    if(m_bUseMutex) pthread_mutex_lock(&m_stMutex);

    m_iLastClearTime = time(0);
    for (int i=0; i<m_iTypeNum; i++)
    {
        CLEAN_TYPE_INFO_DATA(m_astTypeInfo[i]); 
    }
    
    if(m_bUseMutex) pthread_mutex_unlock(&m_stMutex);
}

int CStatistic::_AddStat(char* szTypeName, int iResultID, 
            struct timeval *pstBegin, struct timeval *pstEnd,
            char* szRecordAtMax,int iSumVal,int iStatCount)
{   
    if(!szTypeName)
        return -1;

    //key
    TypeKey stTypeKey;
    memset(stTypeKey.m_szName,0,sizeof(stTypeKey.m_szName));
    strncpy(stTypeKey.m_szName,szTypeName,sizeof(stTypeKey.m_szName));
    stTypeKey.m_iResult = iResultID;
    
    //保证TypeKey是TypeInfo的第一个元素,以便bsearch查找
    TypeInfo* pDestTypeInfo = (TypeInfo*)bsearch((void*)&stTypeKey,m_astTypeInfo,m_iTypeNum,sizeof(TypeInfo),fTypeInfoCmp);
    if(pDestTypeInfo)
    {
        pDestTypeInfo->m_unAllCount += iStatCount;  
        pDestTypeInfo->m_unSumVal += iSumVal;
        _AddTime(pDestTypeInfo,pstBegin, pstEnd,szRecordAtMax); 
        return 0;       
    }

    //第一次统计iType   
    if(m_iTypeNum >= TYPE_NUM)
    {
        printf("ERROR:No type Alloc!\n[%s:%d]",__FILE__,__LINE__);
        return -1;
    }

    if(m_iMaxTypeNameLen < (int)strlen(szTypeName))
        m_iMaxTypeNameLen = strlen(szTypeName);
    
    memset(&m_astTypeInfo[m_iTypeNum],0,sizeof(TypeInfo));
    memcpy(&(m_astTypeInfo[m_iTypeNum].m_TypeKey),&stTypeKey,sizeof(TypeKey));

    m_astTypeInfo[m_iTypeNum].m_unAllCount = iStatCount;
    m_astTypeInfo[m_iTypeNum].m_unSumVal += iSumVal;
    _AddTime(&(m_astTypeInfo[m_iTypeNum]),pstBegin, pstEnd,szRecordAtMax);
    
    ++m_iTypeNum;

    //按统计名称排序
    qsort((void *)&m_astTypeInfo[0],m_iTypeNum,sizeof(TypeInfo),fTypeInfoCmp);
    return 0;
}

int CStatistic::_GetStat(char* szTypeName,int iResultID,TypeInfo &stTypeInfo)
{
    if(!szTypeName)
        return -1;

    //key
    TypeKey stTypeKey;
    memset(stTypeKey.m_szName,0,sizeof(stTypeKey.m_szName));
    strncpy(stTypeKey.m_szName,szTypeName,sizeof(stTypeKey.m_szName));
    stTypeKey.m_iResult = iResultID;
    
    TypeInfo* pDestTypeInfo = (TypeInfo*)bsearch((void*)&stTypeKey,m_astTypeInfo,m_iTypeNum,sizeof(TypeInfo),fTypeInfoCmp);
    if(pDestTypeInfo)
    {
        memcpy(&stTypeInfo,pDestTypeInfo,sizeof(TypeInfo));
        return 0;       
    }
    return -1;
}

void CStatistic::_AddTime(TypeInfo* pTypeInfo,
        struct timeval *pstBegin, struct timeval *pstEnd,char* szRecordAtMax)
{
    if (!pstBegin || !pstEnd)
    {
        return;
    }

    unsigned int unTimeSpanUs = (pstEnd->tv_sec - pstBegin->tv_sec)*1000000 + 
                                (pstEnd->tv_usec - pstBegin->tv_usec);

    if((unTimeSpanUs >= m_iTimeOutUs[0]) && (unTimeSpanUs < m_iTimeOutUs[1]))
        pTypeInfo->m_unTimeOut[0]++;
    else if((unTimeSpanUs >= m_iTimeOutUs[1]) && (unTimeSpanUs < m_iTimeOutUs[2]))
        pTypeInfo->m_unTimeOut[1]++;
    else if(unTimeSpanUs > m_iTimeOutUs[2])
        pTypeInfo->m_unTimeOut[2]++;
    
    if(unTimeSpanUs>pTypeInfo->m_unMaxTime)
    {
        pTypeInfo->m_unMaxTime = unTimeSpanUs;
        if(szRecordAtMax)
        {
            strncpy(pTypeInfo->m_szRecordAtMax,szRecordAtMax,
                                sizeof(pTypeInfo->m_szRecordAtMax)-1);      
        }
    }
    if((pTypeInfo->m_unMinTime == 0)||
        (unTimeSpanUs<pTypeInfo->m_unMinTime))
    {
        pTypeInfo->m_unMinTime = unTimeSpanUs;
    }

    pTypeInfo->m_stTime.tv_sec += (pstEnd->tv_sec - pstBegin->tv_sec);
    pTypeInfo->m_stTime.tv_usec += (pstEnd->tv_usec - pstBegin->tv_usec);

    if(pTypeInfo->m_stTime.tv_usec > 1000000)
    {
        pTypeInfo->m_stTime.tv_sec++;
        pTypeInfo->m_stTime.tv_usec -= 1000000;
    }

    if(pTypeInfo->m_stTime.tv_usec<0)
    {
        pTypeInfo->m_stTime.tv_usec += 1000000;
        pTypeInfo->m_stTime.tv_sec--;
    }
}

int CStatistic::_ShiftFiles()
{
    struct stat stStat;
    char sLogFileName[300];
    char sNewLogFileName[300];
    int i;

    sprintf(sLogFileName,"%s.log", m_szLogBase);
    if(stat(sLogFileName, &stStat) < 0)
        return -1;

    if (stStat.st_size < m_iLogMaxSize)
        return 0;

    sprintf(sLogFileName,"%s%d.log", m_szLogBase, m_iLogMaxNum-1);
    if (access(sLogFileName, F_OK) == 0)
    {
        if (remove(sLogFileName) < 0 )
            return -1;
    }

    for(i = m_iLogMaxNum-2; i >= 0; i--)
    {
        if (i == 0)
            sprintf(sLogFileName,"%s.log", m_szLogBase);
        else
            sprintf(sLogFileName,"%s%d.log", m_szLogBase, i);
            
        if (access(sLogFileName, F_OK) == 0)
        {
            sprintf(sNewLogFileName,"%s%d.log", m_szLogBase, i+1);
            if (rename(sLogFileName,sNewLogFileName) < 0 )
            {
                return -1;
            }
        }
    }
    return 0;
}

void CStatistic::_WriteLog(const char *sFormat, ...)
{
    va_list ap;
    va_start(ap, sFormat);
    
    FILE  *pstFile;
    char szLogFileName[300];
    sprintf(szLogFileName,"%s.log", m_szLogBase);
    if ((pstFile = fopen(szLogFileName, "a+")) == NULL)
    {
        printf("Fail to open log file %s\n",szLogFileName);
        return;
    }
    vfprintf(pstFile, sFormat, ap);
    fclose(pstFile);
    va_end(ap);
}

void CStatistic::GetDateString(char *szTimeStr)
{
    timeval tval;
    gettimeofday(&tval,NULL);
    struct tm curr;
    curr = *localtime(&tval.tv_sec);

    if (curr.tm_year > 50)
    {
        sprintf(szTimeStr, "%04d-%02d-%02d %02d:%02d:%02d.%03d", 
            curr.tm_year+1900, curr.tm_mon+1, curr.tm_mday,
            curr.tm_hour, curr.tm_min, curr.tm_sec,(int)tval.tv_usec);
    }
    else
    {
        sprintf(szTimeStr, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            curr.tm_year+2000, curr.tm_mon+1, curr.tm_mday,
            curr.tm_hour, curr.tm_min, curr.tm_sec,(int)tval.tv_usec);
    }
}

/*
int main()
{
    timeval tBegin,tEnd;
    gettimeofday(&tBegin,NULL);
    usleep(10000);
    gettimeofday(&tEnd,NULL);

    CStatistic* m_pStatistic = new CStatistic(true);
    m_pStatistic->Inittialize("./statistic");
        
    m_pStatistic->AddStat("SetUindex",0, &tBegin,&tEnd, "1");
    m_pStatistic->AddStat("SetUindex",-20005, &tBegin,&tEnd, "1");
    m_pStatistic->AddStat("SetUindex",-1001, &tBegin,&tEnd, "1");
    m_pStatistic->AddStat("SetUindex",-1001, &tBegin,&tEnd, "1");
    m_pStatistic->AddStat("SetUindex0000",-1001, &tBegin,&tEnd, "1");
    m_pStatistic->AddStat("SetUindex000",-1001, &tBegin,&tEnd, "1");

    gettimeofday(&tBegin,NULL);
    usleep(20000);
    gettimeofday(&tEnd,NULL);
    
    m_pStatistic->AddStat("SetUindex",0, &tBegin,&tEnd, "111.222.333.444:56");

    gettimeofday(&tBegin,NULL);
    usleep(300000);
    gettimeofday(&tEnd,NULL);
    
    m_pStatistic->AddStat("SetUindex",0, &tBegin,&tEnd, "3");

    m_pStatistic->WriteToFile();
    m_pStatistic->ClearStat();
    
    sleep(3);
    gettimeofday(&tBegin,NULL);
    usleep(600000);
    gettimeofday(&tEnd,NULL);
    
    m_pStatistic->AddStat("SetUindex",-1, &tBegin,&tEnd, "111.222.333.444:56");

    gettimeofday(&tBegin,NULL);
    usleep(1000000);
    gettimeofday(&tEnd,NULL);
    
    m_pStatistic->AddStat("SetFindex",-1, &tBegin,&tEnd, "111.222.333.444:56");
    m_pStatistic->AddStat("SetFyndex",-1, &tBegin,&tEnd, "111.222.333.444:56");
    m_pStatistic->AddStat("DelFindex",-1, &tBegin,&tEnd, "7");
    m_pStatistic->AddStat("GetGindex",-1, &tBegin,&tEnd, "8");
    m_pStatistic->AddStat("GetUindex",-1, &tBegin,&tEnd, "9");
    m_pStatistic->AddStat("DelUindex",-1, &tBegin,&tEnd, "10");
    m_pStatistic->AddStat("DelUindex",-1, &tBegin,&tEnd, "10");
    m_pStatistic->AddStat("DelUindex",-1, &tBegin,&tEnd, "10");

    TypeInfo stTypeInfo;
    int iret = m_pStatistic->GetStat("DelUindex",-1,  stTypeInfo);
    if(iret)
    {
        printf("get failed!\n");
    }

    m_pStatistic->AddStat("MSGIN");
    m_pStatistic->AddStat("MSGIN");
    m_pStatistic->AddStat("MSGIN");
    m_pStatistic->WriteToFile();
    m_pStatistic->ClearStat();
    return 0;
}
*/

