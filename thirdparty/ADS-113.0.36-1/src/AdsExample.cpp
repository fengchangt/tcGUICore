// AdsTest.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
// 这是CurveMed 的Ads C++ 通讯例程 参考：beckhoff Ads sum-cmd测试例程

#include <iostream>
#include <vector>
#include "AdsClient.h"
#include "AdsExample.h"

void adsexample(ADSClient* pAdsClient)
{
    std::cout <<" ---Info: CurveAdsApp is satrted!!"<< std::endl;
    pAdsClient->connect();
    pAdsClient->getVariableHandle();
    pAdsClient->writeData();
    pAdsClient->readData();
    std::cout << " CurveAdsApp is finished!!" << std::endl;
    system("pause");
}

void adsinit(ADSClient* pAdsClient)
{
    pAdsClient->connect();
    pAdsClient->getVariableHandle();
    if (pAdsClient->m_connected && pAdsClient->symbolReadAlign && pAdsClient->symbolWriteAlign) {
        std::cout << "---Info: CurveAds Init finished" << std::endl;
    }
    else {
        std::cout << "---Info: CurveAds Init Wrong" << std::endl;
    }
    
}










