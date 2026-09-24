#include <iostream>
#include <stdexcept>
#include "AdsClient.h"


/*-----                读变量                 --------*/
char      twincatAdsDataInput[] = { "GVL_Master.stAdsOutput" };// { "MAIN.sAdsDataOutput" }; // Twicnat结构体

char      twincatAdsDataOutput[] = { "GVL_Master.stAdsInput" }; // Twicnat结构体 



ADSClient::ADSClient() {



}

ADSClient::~ADSClient() {
    disconnect();
    if (nPort != 0) {
        AdsPortClose();
    }
}
bool ADSClient::connect(AmsAddr a_Addr) {
    
    nPort = AdsPortOpen();
    // 初始化ADS端口
    if (nPort == 0) {
        throw std::runtime_error("---Info: Failed to open ADS port");
    }
    
    Addr = a_Addr;
    AdsSyncReadStateReq(pAddr, &nAdsState, &nDeviceState);
    if (nAdsState == ADSSTATE_RUN) {
        m_connected = true;
        std::cout << "---Info: Success to Connecte Romote PLC " << std::endl;
    }
    else {
        m_connected = false;
        std::cout << "---Info: Fail to Connected PLC" << std::endl;
        return false;
    }

    return true;
}

bool ADSClient::connect(void) {
    USHORT   nAdsState, nDeviceState;	//包含PLC的状态信息
    nPort = AdsPortOpen();
    // 初始化ADS端口
    if (nPort == 0) {
        throw std::runtime_error("---Info: Failed to open ADS port");
    }

    //获取本地设备地址
    nErr = AdsGetLocalAddress(pAddr);
    pAddr->port = 851;

    AdsSyncReadStateReq(pAddr, &nAdsState, &nDeviceState);
    if (nAdsState != ADSSTATE_RUN) {
        // 设置AMS地址
        pAddr->netId.b[0] = 172;
        pAddr->netId.b[1] = 13;
        pAddr->netId.b[2] = 158;
        pAddr->netId.b[3] = 17;
        pAddr->netId.b[4] = 1;
        pAddr->netId.b[5] = 1;
        AdsSyncReadStateReq(pAddr, &nAdsState, &nDeviceState);
        if (nAdsState == ADSSTATE_RUN) {
            m_connected = true;
            std::cout << "---Info: Success to Connecte Romote PLC " << std::endl;
        }
        else{
            m_connected = false;
            std::cout << "---Info: Fail to Connected PLC" << std::endl;
            return false;
        }
    }
    else {
        m_connected = true;
        std::cout << "---Info: Success to Connecte Local PLC " << std::endl;
    }
    return true;
}

void ADSClient::disconnect() {
    if (m_connected) {
        m_connected = false;
        AdsPortClose();
        std::cout << "---Info: Disconnected from PLC" << std::endl;
    }
}

bool ADSClient::isConnected() const {
    return m_connected;
}





void ADSClient::getVariableHandle(void) {

 // ---------------------Read----------------------//
    // TwincatReadStrcutVar
    std::cout << "---Info: Get TwincatReadStrcutVar symbol info" << std::endl;
    nErr = AdsSyncReadWriteReq(pAddr,
        ADSIGRP_SYM_INFOBYNAMEEX,
        0x0,
        sizeof(InfoExFloat),
        pInfoExFloat,
        sizeof(twincatAdsDataInput),
        twincatAdsDataInput);
   
    pAdsSymbolEntry = (PAdsSymbolEntry)pInfoExFloat; //强制转换为PAdsSymbolEntry类型的数据
    parReadReq[0].indexGroup = pAdsSymbolEntry->iGroup;//给写入请求的结构体赋值，将信息打包
    parReadReq[0].indexOffset = pAdsSymbolEntry->iOffs;
    parReadReq[0].length = pAdsSymbolEntry->size;

    // if TwincatVar change ,need adapting
    reqSize = parReadReq[0].length;  //计算请求数据包的大小parRe（ IG, IO, Len） 
    reqNum = 1;  //请求的数据的个数
    // std::cout << "Initialize the memory of variable mAdsSumBufferRes " << std::endl;
    memset(mAdsSumReadBufferRes, 0, size_t(reqSize));//初始化存储区域
    unsigned long long templength;
    templength = sizeof(sAdsDataInput);
    //templength = sizeof(sAdsDataInput.bPedalState);// 3
    //templength = sizeof(sAdsDataInput.bAdsHeartBeat);// 1
    //templength = sizeof(sAdsDataInput.stSlaveBasicInfo);// 93
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.eSlaveState);// 2
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.strRobotname);// 16
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.strVersion);// 16
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.stSlaveError);// 40
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.stSlaveError.eErrorLevel);// 2
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.stSlaveError.eModule);// 2
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.stSlaveError.iErrorCode);// 4
    //    templength = sizeof(sAdsDataInput.stSlaveBasicInfo.stSlaveError.strDescription);// 31
    //templength = sizeof(sAdsDataInput.eInstrumentState);// 4
    //templength = sizeof(sAdsDataInput.eInstrumentType);// 4
    //templength = sizeof(sAdsDataInput.eEndoState);// 4
    //templength = sizeof(sAdsDataInput.lMotorActualInfo);// 416
    //templength = sizeof(sAdsDataInput.lDataScope);// 96
    // 
    //templength = sizeof(sAdsDataInput);// 460

    if (templength != reqSize) {
        std::cout << "---Info: C++ sAdsDataInput length "<< templength <<" bytes != Twincat stAdsOutput length " << parReadReq[0].length <<" bytes "<< std::endl;
        symbolReadAlign = false;
    }
    else {
        std::cout << "---Info: C++ sAdsDataInput length " << templength << " bytes = Twincat stAdsOutput length " << parReadReq[0].length << " bytes " << std::endl;
        symbolReadAlign = true;
    }

 // ---------------------Write----------------------//
        /* 获取对应变量的信息 */
    nErr = AdsSyncReadWriteReq(pAddr,
        ADSIGRP_SYM_INFOBYNAMEEX,
        0x0,
        sizeof(InfoExint),
        pInfoExint,
        sizeof(twincatAdsDataOutput),
        twincatAdsDataOutput);

    pAdsWriteSymbolEntry = (PAdsSymbolEntry)pInfoExint;

    /*创建请求区域数据 */
    parWriteReq.indexGroup = pAdsWriteSymbolEntry->iGroup;
    parWriteReq.indexOffset = pAdsWriteSymbolEntry->iOffs;
    parWriteReq.length = pAdsWriteSymbolEntry->size;

    templength = sizeof(sAdsDataOutput);// 392
    //templength = sizeof(sAdsDataOutput.bHeatBeat);// 1
    //templength = sizeof(sAdsDataOutput.bInsReady);// 1
    //templength = sizeof(sAdsDataOutput.eLeftMTMState);// 2
    //templength = sizeof(sAdsDataOutput.eRightMTMState);// 2
    //templength = sizeof(sAdsDataOutput.eMasterState);// 2
    //templength = sizeof(sAdsDataOutput.eRightMTMState);// 2
    //templength = sizeof(sAdsDataOutput.lMotorTargetInfo);// 384


    if (templength != parWriteReq.length) {
        std::cout << "---Info: C++ sAdsDataOutput length " << templength << " bytes != Twincat sAdsDataInput length " << parWriteReq.length << " bytes " << std::endl;
        symbolWriteAlign = false;
    }
    else {
        std::cout << "---Info: C++ sAdsDataOutput length " << templength << " bytes = Twincat sAdsDataInput length " << parWriteReq.length << " bytes " << std::endl;
        symbolWriteAlign = true;
    }


}

bool ADSClient::getReadSymbolsHandle(char inputSymbols[], unsigned long input_size)
{
    // ---------------------Read----------------------//
    std::cout << "---Info: Get TwincatReadStrcutVar symbol info" << std::endl;
    nErr = AdsSyncReadWriteReq(pAddr,
        ADSIGRP_SYM_INFOBYNAMEEX,
        0x0,
        sizeof(InfoExFloat),
        pInfoExFloat,
        sizeof(inputSymbols),
        inputSymbols);

    pAdsSymbolEntry = (PAdsSymbolEntry)pInfoExFloat; //强制转换为PAdsSymbolEntry类型的数据
    parReadReq[0].indexGroup = pAdsSymbolEntry->iGroup;//给写入请求的结构体赋值，将信息打包
    parReadReq[0].indexOffset = pAdsSymbolEntry->iOffs;
    parReadReq[0].length = pAdsSymbolEntry->size;

    // if TwincatVar change ,need adapting
    reqSize = parReadReq[0].length;  //计算请求数据包的大小parRe（ IG, IO, Len） 
    reqNum = 1;  //请求的数据的个数
    // std::cout << "Initialize the memory of variable mAdsSumBufferRes " << std::endl;
    memset(mAdsSumReadBufferRes, 0, size_t(reqSize));//初始化存储区域

    if (input_size != reqSize)
    {
        std::cout << "---Info: C++ sAdsDataInput length " << input_size << " bytes != Twincat stAdsOutput length " << parReadReq[0].length << " bytes " << std::endl;
        symbolReadAlign = false;
        return false;
    }
    else
    {
        std::cout << "---Info: C++ sAdsDataInput length " << input_size << " bytes = Twincat stAdsOutput length " << parReadReq[0].length << " bytes " << std::endl;
        symbolReadAlign = true;
        return true;
    }

}

bool  ADSClient::getWriteSymbolsHandle(char* outputSymbols, unsigned long output_size)
{
    // ---------------------Write----------------------//
       /* 获取对应变量的信息 */
    nErr = AdsSyncReadWriteReq(pAddr,
        ADSIGRP_SYM_INFOBYNAMEEX,
        0x0,
        sizeof(InfoExint),
        pInfoExint,
        sizeof(outputSymbols),
        outputSymbols);

    pAdsWriteSymbolEntry = (PAdsSymbolEntry)pInfoExint;

    /*创建请求区域数据 */
    parWriteReq.indexGroup = pAdsWriteSymbolEntry->iGroup;
    parWriteReq.indexOffset = pAdsWriteSymbolEntry->iOffs;
    parWriteReq.length = pAdsWriteSymbolEntry->size;


    if (output_size != parWriteReq.length) {
        std::cout << "---Info: C++ sAdsDataOutput length " << output_size << " bytes != Twincat sAdsDataInput length " << parWriteReq.length << " bytes " << std::endl;
        symbolWriteAlign = false;
        return false;
    }
    else {
        std::cout << "---Info: C++ sAdsDataOutput length " << output_size << " bytes = Twincat sAdsDataInput length " << parWriteReq.length << " bytes " << std::endl;
        symbolWriteAlign = true;
        return true;
    }
}



void ADSClient::getSymbolsHandle(char inputSymbols[], unsigned long input_size,char outputSymbols[], unsigned long output_size)
{
    getReadSymbolsHandle(inputSymbols, input_size);
    getWriteSymbolsHandle(outputSymbols, output_size);
}





bool ADSClient::readData(void) 
{
    if (!m_connected) {
        std::cerr << "---Error: Not connected to PLC" << std::endl;
        return false;
    }
    // std::cout << "sum cmd read " << std::endl;
    nErr = AdsSyncReadWriteReq(pAddr,
        0xf080,//读取的指令   0xf080 读  0xf081 写  0xf082读写同时
        reqNum,  ///读取的数据个数
        4 * reqNum + reqSize,	//number requested bytes in the sample two variables each 4 bytes.
                                //NOTE : we request additional "error"-flag(long) for each ADS-sub commands
        (void*)(mAdsSumReadBufferRes),  //存放读取回来的数据的数据的地址
        12 * reqNum,			// send 12 bytes { IG（4）, IO（4）, Len（4）  3*4=12} of each ADS-sub command
        &parReadReq);
    /* 0x0 returned !!! */

    BYTE* pObjAdsRes = (BYTE*)mAdsSumReadBufferRes + (reqNum * 4);	// point to ADS-data
    BYTE* pObjAdsErrRes = (BYTE*)mAdsSumReadBufferRes;				// point to ADS-err

  //  memcpy(&sAdsDataOutput.TwincatVarData, pObjAdsRes, reqSize);
  //  std::cout << "Test:var01:" << sAdsDataOutput.curveSigma7->lPositionData[0] << std::endl;

     memcpy(&sAdsDataInput, pObjAdsRes, reqSize);
   //  std::cout << "Test:var01:" << sAdsDataOutput.curveSigma7->lPositionData[0] << std::endl;


    if (nErr == 0) {
        for (long idx = 0; idx < reqNum; idx++) {
            // was communication for ADS-sub command OK ??
            long nAdsErr = *(long*)pObjAdsErrRes;
            if (nAdsErr == 0) {
                // get data out of stream
            }
            pObjAdsErrRes = pObjAdsErrRes + 4; // point to next ADS-err object
            pObjAdsRes = pObjAdsRes + 4;
        }
    }
    return true;
}


bool ADSClient::writeData(void) 
{

    sAdsDataOutput.bHeatBeat = true;

    memcpy(&parWriteReq.sAdsDataOutput, &sAdsDataOutput, parWriteReq.length);
    
    nErr = AdsSyncReadWriteReq(pAddr,
        0xf081,				// ADS list-write command
        reqNum,				// number of ADS-Sub commands
        4 * reqNum,			// we expect an ADS-error-return-code (long) for each ADS-Sub command
        (void*)(mAdsSumWriteBufferRes), // provide space for the response containing the return codes
        12+ parWriteReq.length,			// cbyteLen : send28 bytes (IG1, IO1, Len1, IG2, IO2, Len2, Data1, Data2)   4*6  + 4 =28
        &parWriteReq);			// buffer with data

    if (nErr == 0) {
        PBYTE pObjAdsErrRes = (BYTE*)mAdsSumWriteBufferRes;				// point to ADS-err

        for (long idx = 0; idx < reqNum; idx++) {
            // was communication for ADS-sub command OK ??
            long nAdsErr = *(long*)pObjAdsErrRes;
            if (nAdsErr == 0) {
                // data are written to device

            }
            else {
                // error writing this data to device
                std::cerr << "---Error: Error writing this data to device!!---" << std::endl;

            }

            pObjAdsErrRes = pObjAdsErrRes + 4;					// point to next ADS-err object
        }
    }
    return true;
}