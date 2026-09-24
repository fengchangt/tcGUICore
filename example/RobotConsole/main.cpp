#include "ConsoleUi.h"

#include "tcGUICore.h"

int main()
{
    tcGUICore::Application app("RobotConsole");
    app.setWindowSize(1600, 900);

    tcGUICore::PlcConfig master;
    master.id = "master";
    master.ip = "172.13.158.13";
    master.amsNetId = tcGUICore::amsNetIdFromIp(master.ip);
    master.adsPort = 851;
    master.displayName.zh = "主机器人";
    master.displayName.en = "Master";
    app.addPlc(std::move(master));

    tcGUICore::PlcConfig slave;
    slave.id = "slave";
    slave.ip = "172.13.158.17";
    slave.amsNetId = tcGUICore::amsNetIdFromIp(slave.ip);
    slave.adsPort = 851;
    slave.displayName.zh = "从机器人";
    slave.displayName.en = "Slave";
    app.addPlc(std::move(slave));

    app.onFrame([] { drawRobotConsole(); });
    return app.run();
}
