#include "tcGUICore.h"

int main()
{
    tcGUICore::Application app;
    if (!app.loadConfig()) {
        return 1;
    }
    return app.run();
}
