#include "NdiPanel.h"

#include "tcGUICore.h"

int main()
{
    tcGUICore::Application app("NDIEMSensor");
    app.setWindowSize(1600, 900);

    tcGUICore::PlcConfig plc;
    plc.id = "plc1";
    plc.ip = "172.13.158.17";
    plc.amsNetId = tcGUICore::amsNetIdFromIp(plc.ip);
    plc.adsPort = 851;
    app.addPlc(std::move(plc));
    app.onFrame([] { drawNdiEmSensorUi(); });
    return app.run();
}
