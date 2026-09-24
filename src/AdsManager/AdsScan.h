#pragma once

#include <string>
#include <vector>

namespace tcGUICore {

// UDP 48899 发现结果。下拉框只显示 ip，amsNetId 留给连接使用。
struct AdsEndpoint {
    std::string ip;
    std::string amsNetId;
};

// 做一次 AMS UDP 广播发现（端口 48899），非实时扫描。
std::vector<AdsEndpoint> scanAdsIpsOnce(int timeoutMs = 700);

} // namespace tcGUICore
