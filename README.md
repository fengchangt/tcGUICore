# tcGUICore

Curve Robotics 的通用 GUI 与倍福 ADS 核心库。多套上位机共用这一份源码，产品工程通过 CMake 引入，不要在各产品里再复制 ImGui、AdsLib 或 NDI。

当前版本号写在仓库根目录的 `project` 文件里（`MAJOR` / `MINOR` / `PATCH`），CMake 工程名是 `tcGUICore`。

它服务两类软件：

- **触摸屏 HMI**：读、写一台或多台倍福 PLC 的变量。
- **研发工具**：NDI 磁导航采集位姿，在界面上显示，再按协议经 ADS 写入 PLC。

产品侧通常只做三件事：加载与 PLC 共用的 JSON、可选地在每帧绑定自己的控件、调用 `run()`。窗口、主题、ADS 连接和符号读写由本库完成。

## 环境要求

- CMake 3.16 或更高
- C++17、C11
- Windows：Visual Studio（MSVC），渲染使用 DirectX 11
- Linux：OpenGL 3.3 + GLFW/X11。配置阶段会提示安装：

  `build-essential` `cmake` `libgl1-mesa-dev` `libx11-dev` `libxrandr-dev` `libxinerama-dev` `libxcursor-dev` `libxi-dev`

界面中文依赖系统字体。Windows 优先使用微软雅黑；Linux 会依次查找 Noto Sans CJK、文泉驿等。找不到中文字体时仍能启动，汉字可能显示为方框。

第三方源码已放在 `thirdparty/`，默认不需要联网下载：

| 目录 | 用途 |
| --- | --- |
| `imgui-1.92.8` | Dear ImGui，以及 `imgui_stdlib` |
| `implot` / `implot3d` | 二维、三维绘图 |
| `glfw-3.5.1` | 窗口与输入 |
| `ADS-113.0.36-1` | 独立 AdsLib（TCP/AMS） |
| `NDI` | NDI Combined API（磁导航） |

GLFW 的查找顺序是：`thirdparty/glfw` 或 `thirdparty/glfw-*` 源码、预编译库、系统上的 `glfw3`、最后才是 FetchContent 下载 3.4。

## 目录

```
tcGUICore/
  CMakeLists.txt          顶层工程、选项、安装导出
  cmake/                  tcguicore_add_app() 与 find_package 模板
  project                 版本号
  src/
    tcGUICore.h           产品只需包含的头文件
    Application.cpp       loadConfig / addPlc / onFrame / run
    AdsManager/           多 PLC 连接、符号读写、UDP 扫描
    Viewer/               无边框窗口、暗色主题、UiBind
    Common/               JSON、日志、中英文、可执行文件路径
  example/
    PLCHmiDemo/           只加载 JSON，使用库自带界面
    NDIEMSensor/          自绘 NDI + PLC 连接界面
    RobotConsole/         SurgeonConsole（树莓派扶手触摸屏，Win/Linux 同名）
  scripts/
    build_surgeon_console_pi.sh   树莓派本机编译
  thirdparty/             ImGui、ImPlot、GLFW、AdsLib、NDI
```

顶层单独编译时，可执行文件和静态库按平台分开，避免 Debug / Release 的 MSVC 运行库混用：

- `bin/<系统>-<架构>/<配置>/`，例如 `bin/win-x64/release/`
- `lib/<系统>-<架构>/<配置>/`

作为其它工程的子目录引入时，不会改对方的编译标准或输出路径。

## CMake 目标与选项

链接名：

| 目标 | 内容 |
| --- | --- |
| `tcGUICore::tcGUICore` | 本库（窗口 + ADS 会话）。Windows 上还会链接 `d3d11`、`dxgi` |
| `tcGUICore::gui` | ImGui + ImPlot + ImPlot3D + GLFW/OpenGL |
| `tcGUICore::ads` | 独立 AdsLib，别名指向 `AdsLib` |
| `tcGUICore::ndi` | NDI Combined API。HMI 不要把它链进核心库 |

常用选项（可在产品工程里先 `set(... CACHE BOOL "" FORCE)` 再 `add_subdirectory`）：

| 选项 | 默认 | 含义 |
| --- | --- | --- |
| `TCGUICORE_ENABLE_GUI` | ON | 编译 ImGui / ImPlot / ImPlot3D |
| `TCGUICORE_ENABLE_ADS` | ON | 编译独立 AdsLib |
| `TCGUICORE_ENABLE_NDI` | ON | 编译 NDI。缺少 `CombinedApi.h` 时只告警 |
| `TCGUICORE_LINK_NDI` | OFF | 是否把 NDI 链进 `tcGUICore` 本体。产品应自行链接 `tcGUICore::ndi` |
| `TCGUICORE_ENABLE_IMGUI_DEMO` | OFF | 是否编入 imgui / implot 官方 Demo 源文件 |
| `TCGUICORE_ENABLE_TWINCAT_CLIENT` | OFF | 仅 Windows：旧版 `AdsClient`（`AdsPortOpen` + `windows.h`），不跨平台 |
| `TCGUICORE_USE_FETCHCONTENT_GLFW` | ON | 本地没有 GLFW 源码时允许下载 |
| `TCGUICORE_BUILD_EXAMPLES` | 顶层编译时为 ON | 编译 `example/` |
| `TCGUICORE_INSTALL` | OFF | 生成 install / `find_package`。日常开发保持关闭，用 `add_subdirectory` |

`example/` 下每个带 `CMakeLists.txt` 的子目录都会被加入。新增样例时新建文件夹即可，不要分叉核心库。

`tcguicore_add_app(名字 [NDI] SOURCES ... HEADERS ...)` 会链接 `tcGUICore::tcGUICore`。传入 `NDI` 时再链接 `tcGUICore::ndi`，并定义 `TCGUICORE_APP_HAS_NDI`。若该目录里存在 `plc_symbols.json`、`plc_symbols` 或 `plc_config.json`，构建后会复制到可执行文件旁边，文件名统一为 `plc_symbols.json`。

## 在本仓库里编译

在仓库根目录执行。下面以 Visual Studio 2022、64 位 Release 为例：

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

单配置生成器（Ninja）需要自己指定构建类型，输出目录会带上 `debug` 或 `release`：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

成功后可执行文件在：

- `bin/win-x64/release/PLCHmiDemo.exe`
- `bin/win-x64/release/NDIEMSensor.exe`
- `bin/win-x64/release/SurgeonConsole.exe`（医生控制台，跨平台统一名）

Linux 示例：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 树莓派 5（SurgeonConsole）

同一工程、同一产物名。请在 **树莓派本机**（aarch64）编译：

```bash
chmod +x scripts/build_surgeon_console_pi.sh
./scripts/build_surgeon_console_pi.sh
```

产物：`bin/lin-arm64/release/SurgeonConsole`。详见 `example/RobotConsole/README.md`。

两个样例的 CMake 都要求已经生成 `NDI_CombinedApi`。默认 `TCGUICORE_ENABLE_NDI=ON`，且 `thirdparty/NDI` 存在时会生成该目标。若关掉 NDI，样例会被跳过。

只编库、不编样例：

```bat
cmake -S . -B build -DTCGUICORE_BUILD_EXAMPLES=OFF
```

## 产品工程如何接入

把本仓库作为子目录。HMI 只链核心库：

```cmake
add_subdirectory(path/to/tcGUICore)
add_executable(MyHmi main.cpp)
target_link_libraries(MyHmi PRIVATE tcGUICore::tcGUICore)
```

也可以用辅助函数，效果相同：

```cmake
add_subdirectory(path/to/tcGUICore)
include(tcGUICoreAddApp)   # 顶层已加入 CMAKE_MODULE_PATH 时会自动可见

tcguicore_add_app(MyHmi SOURCES main.cpp)
```

磁导航类产品再链 NDI，不要打开 `TCGUICORE_LINK_NDI` 去污染 HMI：

```cmake
tcguicore_add_app(NdiTool NDI SOURCES main.cpp NdiPanel.cpp)
```

安装导出（`TCGUICORE_INSTALL`）仅在 GLFW 不是 `IMPORTED` 预编译库时可用。多产品维护时继续用 `add_subdirectory`。

## 程序接口

产品只包含 `tcGUICore.h`。入口是 `tcGUICore::Application`。

使用库自带界面（连接栏、左侧分组、PLC 符号表）：

```cpp
#include "tcGUICore.h"

int main()
{
    tcGUICore::Application app;
    if (!app.loadConfig()) {   // 默认找 plc_symbols.json
        return 1;
    }
    return app.run();
}
```

自绘界面时注册 `onFrame`。标题栏和帧率仍由库绘制；标题栏以下整页换成回调，库自带的连接栏和符号表不再出现。回调里需要自己完成连接和控件，参见 `example/NDIEMSensor`。

```cpp
#include "tcGUICore.h"

int main()
{
    tcGUICore::Application app("MyTool");
    app.setWindowSize(1600, 900);
    app.setLanguage(tcGUICore::Language::Zh);

    tcGUICore::PlcConfig plc;
    plc.id = "plc1";
    plc.ip = "192.168.1.10";
    plc.amsNetId = tcGUICore::amsNetIdFromIp(plc.ip);  // "192.168.1.10.1.1"
    plc.adsPort = 851;
    app.addPlc(std::move(plc));

    app.onFrame([] {
        tcGUICore::uiCheckbox("plc1.bEnable");
        tcGUICore::uiValueText("plc1.fPosition");
    });
    return app.run();
}
```

`loadConfig` 会按顺序尝试这些路径，命中即停止：参数路径、可执行文件目录下的该路径、当前工作目录下的该路径，以及 `plc_symbols.json` / `plc_symbols`（当前目录和可执行文件目录）。成功后会套用窗口大小和语言，并登记 JSON 里的 PLC 与变量。

其它调用：

- `ads()` 返回 `AdsHub`，用于查找 PLC、改连接参数、按键读写运行时变量。
- `Application::instance()` 在 `run()` 期间有效，`UiBind` 和样例通过它拿到当前会话。
- 窗口标题缺省时使用可执行文件名（不含扩展名）。

变量绑定在 `Viewer/UiBind.h`，键格式是 `plcId.name`，例如 `plc1.bEnable`：

| 函数 | 适用类型 |
| --- | --- |
| `uiCheckbox` | `bool` |
| `uiDragFloat` | `float`、`double` |
| `uiDragInt` | `int8` / `uint8` / `int16` / `uint16` / `int32` |
| `uiValueText` | 只读显示；整数类型会再画一个拖动框 |

`direction` 为 `input` 的变量只能显示。`output` 且当前会话 AMS NetId 与配置一致时才允许改写。

## JSON 配置

与 PLC 工程约定同一份符号名、类型和方向。方向必须在连接前写好。根对象至少包含 `plcs` 数组。

```json
{
  "window": {
    "width": 1920,
    "height": 1080,
    "language": "zh"
  },
  "plcs": [
    {
      "id": "plc1",
      "displayName": { "zh": "主站", "en": "Master" },
      "ip": "127.0.0.1",
      "adsPort": 851,
      "timeoutMs": 2000,
      "variables": [
        {
          "name": "bEnable",
          "symbol": "MAIN.bEnable",
          "type": "bool",
          "direction": "output",
          "label": { "zh": "使能", "en": "Enable" }
        },
        {
          "name": "fPosition",
          "symbol": "GVL.fPosition",
          "type": "float",
          "direction": "input",
          "label": { "zh": "位置 mm", "en": "Position mm" }
        }
      ]
    }
  ]
}
```

字段说明：

- `window.language`：`zh` 或 `en`。界面按钮和日志走同一套文案。
- `plcs[].id`、`ip` 必填。`adsPort` 默认 851，`timeoutMs` 默认 2000。
- `displayName`、`label` 可以是字符串，或 `{ "zh", "en" }`（也接受 `cn` 作为中文）。
- AMS NetId 不在 JSON 里单独写。加载时固定为 `ip + ".1.1"`（TwinCAT 默认形式）。界面改 IP 并连接时，会把这次会话的 NetId 写回该 PLC。
- `variables[].name` 是界面绑定名；`symbol` 是 TwinCAT 符号路径，例如 `MAIN.bEnable`。两者都不能空。
- `direction` 必填。相对 C++：`input` / `in` / `read` / `fromPlc` 表示从 PLC 读入；`output` / `out` / `write` / `toPlc` 表示向 PLC 写出。

`type` 接受这些名字（大小写按下列字面量）：

| JSON | 字节 |
| --- | --- |
| `bool` / `BOOL` | 1 |
| `int8` / `SINT` | 1 |
| `uint8` / `byte` / `BYTE` / `USINT` | 1 |
| `int16` / `int` / `INT` | 2 |
| `uint16` / `WORD` / `UINT` | 2 |
| `int32` / `DINT` | 4 |
| `uint32` / `DWORD` / `UDINT` | 4 |
| `int64` / `LINT` | 8 |
| `float` / `REAL` | 4 |
| `double` / `LREAL` | 8 |

完整样例见 `example/PLCHmiDemo/plc_symbols.json`。

## ADS 连接在做什么

每台 PLC 对应一个 `PlcClient`，由 `AdsHub` 管理。

Windows 上连接顺序是：

1. 若能加载 `C:\TwinCAT\AdsApi\TcAdsDll\x64\TcAdsDll.dll` 或当前目录的 `TcAdsDll.dll`，先走本机 TwinCAT 路由（`AdsPortOpenEx`）。
2. 路由返回“目标端口未打开”或“没有这条路由”时，再用短超时探测该 IP 的 TCP **48898**。
3. 探测成功后，用独立 AdsLib 按 IP + AMS NetId + 端口建立 `AdsDevice`。

Linux / macOS 没有 TwinCAT 路由这一步，直接使用 AdsLib。AdsLib 走 TCP/AMS，不要求本机安装 TwinCAT。

已连接后，后台大约每 50ms 按 JSON 方向读写一次符号，大约每 800ms 刷新 ADS 状态（RUN 或状态号）。符号句柄按路径缓存。JSON 里的 NetId 与本次会话不一致的变量会被跳过，并在日志里警告一次。

工具栏里的地址下拉会做一次 UDP **48899** 广播（AMS ServerInfo），用于发现网上的 PLC。下拉框显示 IP；应答里的 AMS NetId 留给连接使用。这不是周期扫描。

库自带界面在 **SymbolsScope** 里上传 PLC 符号表（ADS 索引组），表格可筛选、展开基本类型数组，并按组号和偏移读写。上传信息失败时，Windows 上会再尝试读取 `C:\TwinCAT\3.1\Boot\CurrentConfig.xml` 指向的工程 TMC。这张表面对的是 PLC 上的全部符号，和 JSON 里登记给 `uiCheckbox` 的变量是两条路径。

日志留在内存环形缓冲里（最多约 400 条），界面和 `logInfo` / `logWarn` / `logError` 使用同一处。

## 样例怎么用

### PLCHmiDemo

只调用 `loadConfig()` 和 `run()`，打开库自带的暗色窗口：

1. 按上一节编译出 `PLCHmiDemo.exe`。确认旁边有 `plc_symbols.json`（构建时会从 `example/PLCHmiDemo/` 复制）。
2. 运行后再标题栏下选择 Net IP 和 ADS 端口。下拉打开时会扫描一次局域网。
3. 点“连接”。绿灯表示会话已建立。左侧展开 **SymbolsScope** 查看并读写该 PLC 的符号。
4. “中文 / EN”切换界面语言。

JSON 里的 `bEnable`、`fPosition`、`nState` 会进入 `AdsHub` 并在后台轮询。这个样例没有 `onFrame`，所以不会画出 `uiCheckbox` 那一套控件。

### NDIEMSensor

磁导航研发界面，窗口 1600×900，启动时注册一台 `plc1`（默认 IP `172.13.158.17`，端口 851）：

1. 编译并运行 `NDIEMSensor.exe`。NDI 设备用 USB 串口连接。
2. 第一行选择 COM 和波特率（默认 921600），点“连接”。工作线程里会初始化端口并 `startTracking`。
3. 表格显示最多四个通道的四元数 `q0 qx qy qz`、位置 X/Y/Z（毫米）和误差。无效或缺失的通道显示 `--`。
4. 第二行填写或扫描 PLC 的 IP 和端口后连接。

位姿目前只在界面显示。等 PLC 侧数据协议对齐后，再经 ADS 写出。串口命令只在 NDI 工作线程中调用。

## 运行时注意

- 电脑需要能路由到 PLC 所在网段。TCP 48898 不通时，日志会带上本机网卡地址。
- PLC 拒绝连接时，确认 TwinCAT 路由在运行，并且防火墙放行 48898。
- 默认窗口最小约 960×640。Windows 下标题栏为深色，窗口四角为圆角；最大化时取消圆角。
- 帧循环以约 120 FPS 为上限。符号表正在从 PLC 上传时，会暂停 JSON 变量的后台轮询，避免两条路径同时占用同一条 ADS 连接。
