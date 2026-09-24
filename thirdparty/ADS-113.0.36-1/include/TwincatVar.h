#ifndef TWINCATVAR_H
#define TWINCATVAR_H
#include <string>

#pragma pack(1)
typedef struct sMotorobeject// 和beckhoff的变量对应
{
    double lPosition;// deg
    double lVelocity;//  deg/s
} twincatMotorInfo;


/*-----                从Twincat读数据                 --------*/
enum class E_StateMachine : unsigned short {
    Paused = 0,// 停止/挂起状态
    Init,// 初始化状态
    Run,// 运行状态
    Busy,// 忙碌状态
    Error,// 错误状态
    Debug,// 调试状态
    Reset,// 复位
    EmergencyStop,// 急停模式
    FreeDom,// 自由模式
    Idle,// 空闲状态
    ReInit,// 重初始化
    PlatformPositioning,
    MSWorkStandBy,
    MSWorking,
    EndoControl,
    PowerOff// 关机
};
const std::string strStateMachine[] = {
     "Paused" ,// 停止/挂起状态
     "Init",// 初始化状态
     "Run",// 运行状态
     "Busy",// 忙碌状态
     "Error",// 错误状态
     "Debug",// 调试状态
     "Reset",// 复位
     "EmergencyStop",// 急停模式
     "FreeDom",// 自由模式
     "Idle",// 空闲状态
     "ReInit",// 重初始化
     "PlatformPositioning",
     "MSWorkStandBy",
     "MSWorking",
     "EndoControl",
     "PowerOff"// 关机
};




enum class E_InstrumentState : unsigned short {
    Paused = 0,// 停止/挂起状态
    Init,// 初始化状态
    Idle,// 空闲状态
    Run,// 运行状态
    Busy,// 忙碌状态
    Error,// 错误状态
    Debug,// 调试状态
    Reset,
    EmergencyStop,// 急停模式
    FreeDom,// 自由模式
    Homing,// 回零
    ToolDockong,// 快换安装
    RailTurning,// 进给调整
    RailLocked,// 进给锁定
    ExitTool,// 退器械
    MSWorkStandBy,// 主从待命
    MSWorking// 主从
};
const std::string strInstrumentState[] = {
     "Paused",// 停止/挂起状态
     "Init",// 初始化状态
     "Idle",
     "Run",// 运行状态
     "Busy",// 忙碌状态
     "Error",// 错误状态
     "Debug",// 调试状态
     "Reset",// 复位
     "EmergencyStop",// 急停模式
     "FreeDom",// 自由模式
     "Homing",
     "ToolDockong",
     "RailTurning",
     "RailLocked",
     "ExitTool",
     "MSWorkStandBy",
      "MSWorking"// 主从
};

enum class E_EndoState : unsigned short {
    Paused = 0,// 停止/挂起状态
    Init,// 初始化状态
    Idle,// 空闲状态
    Run,// 运行状态
    Busy,// 忙碌状态
    Error,// 错误状态
    Debug,// 调试状态
    Reset,
    EmergencyStop,// 急停模式
    FreeDom,// 自由模式
    Homing,// 回零
    ToolDockong,// 快换安装
    RailTurning,// 调整进给
    RailLocked,// 进给锁定
    ExitTool,// 退器械
    MSWorkStandBy,// 主从待命
    MSWorking// 主从
};
const std::string strEndoState[] = {
     "Paused",// 停止/挂起状态
     "Init",// 初始化状态
     "Idle",
     "Run",// 运行状态
     "Busy",// 忙碌状态
     "Error",// 错误状态
     "Debug",// 调试状态
     "Reset",// 复位
     "EmergencyStop",// 急停模式
     "FreeDom",// 自由模式
     "Homing",
     "ToolDockong",
     "RailTurning",
     "RailLocked",
     "ExitTool",
     "MSWorkStandBy",
      "MSWorking"// 主从
};

enum class E_InstType : unsigned short {
    None    = 0,
    Volkmann = 1,// 刮匙
    Clamp = 2,// 夹钳
    ElectKnife = 3// 电刀
};

enum class E_Module : unsigned short {
    ROBOT = 0,
    MASTER = 1,
    MSMANAGER = 2,
    ENDO = 3,
    INSTRUMENT_LEFT = 4,
    INSTRUMENT1_RIGHT = 5,
    VISION = 6,
    MOBILEBASECART = 7,
    AXISMOTIONMANAGER = 8
};
enum class E_ErrorLevel : unsigned short {
    ERROR_NONE = 0,
    ERROR_INFO  = 1,
    ERROR_WARNING  = 2,
    ERROR_CRITICAL  = 3,
    ERROR_FATAL  = 4
};


typedef struct twincatErrorInfos {
    E_Module eModule = E_Module::ROBOT;
    E_ErrorLevel eErrorLevel = E_ErrorLevel::ERROR_NONE;
    unsigned int iErrorCode = 0;
    char strDescription[51] = {};
} ST_ErrorInfos;


typedef struct twincatSlaveBasicInfo {
    char     strRobotname[16] = {};
    char     strVersion[16] = {};
    E_StateMachine  eSlaveState = E_StateMachine::Paused;
    ST_ErrorInfos   stSlaveError = {};
} ST_SlaveBasicInfo;



 // 与twincat内存对齐方式一致
typedef struct twincatReadBuffer {
    bool                    bPedalState[3] = {false,false,false};
    bool                    bAdsHeartBeat = false;
    ST_SlaveBasicInfo       stSlaveBasicInfo = {};
    E_InstrumentState       eInstrumentState[2] = { E_InstrumentState::Paused,E_InstrumentState::Paused };
    E_InstType              eInstrumentType[2] = {};
    E_EndoState             eEndoState = E_EndoState::Paused;
    twincatMotorInfo        lMotorActualInfo[26] = {};
    double                  lDataScope[12] = {};
} TwincatVarReadBuffer;
#pragma pack()
//定义结构体用来打包发送请求数据
typedef struct readDataPar
{
    unsigned long		indexGroup;	// index group in ADS server interface
    unsigned long		indexOffset;	// index offset in ADS server interface
    unsigned long		length;		// count of bytes to read
} rDataPar;


/*-----                给Twincat写数据                 --------*/
#pragma pack(1)
//定义结构体用来打包发送请求数据
enum class E_MasterState : unsigned short {
    Paused = 0,// 停止/挂起状态
    Init,// 初始化状态
    Idle,// 空闲状态
    Run,// 运行状态
    Busy,// 忙碌状态
    Error,// 错误状态
    Debug,// 调试状态
    Reset,
    EmergencyStop,// 急停模式
    FreeDom,// 自由模式
    MSWorkStandBy,// 主从待命
    MSWorking,// 主从中
    Disconnected,// ADS掉线
    FreeDrag,//自由拖动
    Impedance// 阻抗拖动
};
const std::string strMasterState[] = {
     "Paused",// 停止/挂起状态
     "Init",// 初始化状态
     "Idle",// 空闲状态
     "Run",// 运行状态
     "Busy",// 忙碌状态
     "Error",// 错误状态
     "Debug",// 调试状态
     "Reset",// 复位
     "EmergencyStop",// 急停模式
     "FreeDom",// 自由模式
     "MSWorkStandBy",
     "MSWorking",
     "Disconnected",
     "FreeDrag",
     "Impedance"
};

enum class E_MTMState : unsigned short {
    Paused = 0,// 停止/挂起状态
    Init,// 初始化状态
    Idle,// 空闲状态
    Run,// 运行状态
    Busy,// 忙碌状态
    Error,// 错误状态
    Debug,// 调试状态
    Reset,
    EmergencyStop,// 急停模式
    FreeDom,// 自由模式
    FreeDrag,
    Impedance,
    MSWorkStandBy,
    MSWorkFreeDrag,// 主从自由拖动
    MSWorking,// 主从中
};
const std::string strMTMState[] = {
    "Paused",// 停止/挂起状态
    "Init",// 初始化状态
    "Idle",// 空闲状态
    "Run",// 运行状态
    "Busy",// 忙碌状态
    "Error",// 错误状态
    "Debug",// 调试状态
    "Reset",
    "EmergencyStop",// 急停模式
    "FreeDom",// 自由模式
    "FreeDrag",
    "Impedance",
    "MSWorkStandBy",
    "MSWorkFreeDrag",// 主从待命
    "MSWorking"// 主从中
};
 // 与twincat内存对齐方式一致
typedef struct twincatWriteBuffer {
    E_MasterState       eMasterState = E_MasterState::Paused;
    E_MTMState          eMTMState[2] = { E_MTMState::Paused,E_MTMState::Paused };
    bool                bHeatBeat = false;
    bool                bInsReady[2] = { false,false };
    twincatMotorInfo    lMotorTargetInfo[26] = {};
    double              lDataScope[12] = {};
} TwincatVarWriteBuffer;
#pragma pack()
typedef struct writeDataPar
{
    unsigned long		indexGroup;	// index group in ADS server interface
    unsigned long		indexOffset;	// index offset in ADS server interface
    unsigned long		length;		// count of bytes to read
    TwincatVarWriteBuffer sAdsDataOutput;
} wDataPar;


//////////////////////////////////////////////////////////////////////////////////////////////////////////////



#endif 