#ifndef ADSCLIENT_H
#define ADSCLIENT_H

#include <string>
#include <cstdint>
#include <functional>
#include <windows.h>
#include "AdsDef.h"
#include "AdsLib.h"
#include "TwincatVar.h"


class ADSClient {
public:
    // 回调函数类型定义
    using NotificationCallback = std::function<void(const uint8_t* data, uint32_t size)>;

    ADSClient();
    ~ADSClient();

    // 连接PLC
    bool connect(void);

    bool connect(AmsAddr a_Addr);

    // 断开连接
    void disconnect();
    
    // 检查连接状态
    bool isConnected() const;

    // 读取数据
    bool readData(void);
    
    // 写入数据
    bool writeData(void);

    // 读取指定类型的数据
    template<typename T>
    bool readValue(const std::string& variableName, T& value) {
        return readData(variableName, &value, sizeof(T));
    }

    // 写入指定类型的数据
    template<typename T>
    bool writeValue(const std::string& variableName, const T& value) {
        return writeData(variableName, &value, sizeof(T));
    }

    bool getReadSymbolsHandle(char inputSymbols[], unsigned long input_size);
    bool getWriteSymbolsHandle(char outputSymbols[], unsigned long output_size);

    void getVariableHandle(void);
    void getSymbolsHandle(char inputSymbols[], unsigned long input_size, char outputSymbols[], unsigned long output_size);


    // Twincat变量Ads接口
    TwincatVarReadBuffer sAdsDataInput;// From Twincat Read 
    TwincatVarWriteBuffer sAdsDataOutput;// Write To Twincat

    bool m_connected;
    bool symbolReadAlign = false;
    bool symbolWriteAlign = false;
    double cycleTime;// 一写一读
    double writeStartTime;// 记录写的时间
    USHORT   nAdsState, nDeviceState;	//包含PLC的状态信息
private:
    AmsAddr Addr;
    PAmsAddr pAddr = &Addr;  //AMS的地址
    long nErr, nPort;

    /*-----                读变量                 --------*/
    rDataPar parReadReq[2]; //定义结构体数组，存放对应两个PLC变量的发送请求数据。from beckhoff sumAds Demo
    #pragma pack(4) 
    double  mAdsSumReadBufferRes[300];
    #pragma pack()  // 恢复默认对齐
    AdsSymbolEntry		  InfoExFloat; //存放用AdsSyncReadWriteReq读取到的变量的信息（结构体）
    AdsSymbolEntry* pInfoExFloat = &InfoExFloat;//InfoExFloat地址给pInfoExFloat指针，存放获取到的变量的信息（indexGroup，indexOffset，length等信息）
    PAdsSymbolEntry       pAdsSymbolEntry;

    /*-----                写变量                 --------*/
    unsigned char mAdsSumWriteBufferRes[1000];
    wDataPar parWriteReq;
    AdsSymbolEntry		  InfoExint; // 存放用AdsSyncReadWriteReq读取到的变量的信息（结构体）
    AdsSymbolEntry* pInfoExint = &InfoExint;//指针赋值
    PAdsSymbolEntry       pAdsWriteSymbolEntry;

    long reqSize = 0;
    long reqNum = 1;



};



#endif // ADS_CLIENT_H