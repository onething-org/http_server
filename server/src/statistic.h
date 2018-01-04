#ifndef __STATISTIC_H__
#define __STATISTIC_H__

#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
 #include <stdarg.h>
 #include <pthread.h>

#define TYPE_NUM            256
#define TYPE_NAME_LEN       64
#define MAX_TIMEOUT_LEVEL   3

typedef struct
{
    unsigned long long tv_sec;
    unsigned long long tv_usec;
}ULLTimeVal;

typedef struct
{
    char m_szName[TYPE_NAME_LEN];
    int m_iResult;
}TypeKey;

typedef struct
{
    //key
    TypeKey m_TypeKey;
    
    unsigned int m_unAllCount;
    unsigned int m_unMaxTime;
    unsigned int m_unMinTime;
    ULLTimeVal m_stTime;    
    unsigned int m_unTimeOut[MAX_TIMEOUT_LEVEL];
    char m_szRecordAtMax[TYPE_NAME_LEN];
    unsigned int m_unSumVal;
}TypeInfo;

#define CLEAN_TYPE_INFO_DATA(stTypeInfo) { \
    (stTypeInfo).m_unAllCount = 0;\
    (stTypeInfo).m_unMaxTime = 0;\
    (stTypeInfo).m_unMinTime = 0;\
    (stTypeInfo).m_stTime.tv_sec = 0;\
    (stTypeInfo).m_stTime.tv_usec = 0;\
    memset(&((stTypeInfo).m_unTimeOut[0]),0,sizeof((stTypeInfo).m_unTimeOut));\
    memset(&((stTypeInfo).m_szRecordAtMax[0]),0,sizeof((stTypeInfo).m_szRecordAtMax));\
    (stTypeInfo).m_unSumVal=0;\
}

class CStatistic
{
public:
    static CStatistic* Instance();
    static CStatistic* m_pInstance;
    
    CStatistic(bool bUseMutex=false);
    ~CStatistic();

    //iTimeOutUs :时间计数标尺,单位us
    int Inittialize(const char *pszLogBaseFile=(char *)"./Statistic",
        int iMaxSize=20000000,
        int iMaxNum=10,
        int iTimeOutUs1=100000,
        int iTimeOutUs2=500000,
        int iTimeOutUs3=1000000);

    //pTypeName: 统计名称
    //szRecordAtMax: 最多延时产生时记录的数据
    //iStatCount统计个数
    //iSumVal 累加的值
    int AddStat(char* szTypeName,
                int iResultID=0, 
                struct timeval *pstBegin=NULL, 
                struct timeval *pstEnd=NULL,
                char* szRecordAtMax=NULL,
                int iSumVal=0,
                int iStatCount=1);

    int AddStat(int iTypeName,
                int iResultID=0, 
                struct timeval *pstBegin=NULL, 
                struct timeval *pstEnd=NULL,
                char* szRecordAtMax=NULL,
                int iSumVal=0,
                int iStatCount=1)
    {
        char szTypeName[TYPE_NAME_LEN];
        sprintf(szTypeName,"%d",iTypeName);
        return AddStat(szTypeName,iResultID,pstBegin,pstEnd,szRecordAtMax,iSumVal,iStatCount);
    }
    
    int GetStat(char* szTypeName,int iResultID,TypeInfo &stTypeInfo);
    const TypeInfo* GetStat(int &iTypeNum)
    {
        iTypeNum = m_iTypeNum;
        return m_astTypeInfo;
    };

    int WriteToFile();  
    void ClearStat();   

private:
    static void GetDateString(char *szTimeStr);
    
    int _AddStat(char* pszTypeName,
                int iResultID, 
                struct timeval *pstBegin, 
                struct timeval *pstEnd,
                char* szRecordAtMax,
                int iSumVal,
                int iStatCount);    
    int _GetStat(char* szTypeName,int iResultID,TypeInfo &stTypeInfo);
    
    int _ShiftFiles();
    void _WriteLog(const char *sFormat, ...);
    void _AddTime(TypeInfo* pTypeInfo,
                struct timeval *pstBegin, 
                struct timeval *pstEnd,
                char* szRecordAtMax);

    //互斥使用
    bool m_bUseMutex;
    pthread_mutex_t m_stMutex;   
    
    int m_iTypeNum;
    TypeInfo m_astTypeInfo[TYPE_NUM];
    
    char m_szLogBase[256];
    int m_iLogMaxSize;
    int m_iLogMaxNum;

    unsigned int m_iTimeOutUs[MAX_TIMEOUT_LEVEL];
    
    int m_iLastClearTime;

    int m_iMaxTypeNameLen;
};
#endif

