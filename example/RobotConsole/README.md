# Surgeon Console（医生控制台扶手触摸屏）

同一工程跨平台：Windows / 树莓派 5（Linux aarch64）/ Ubuntu。  
产物名统一为 **`SurgeonConsole`**（无空格）；窗口标题仍为 “Surgeon Console”。  
渲染：Windows = DX11；Linux = GLFW + OpenGL。不链接 NDI。

## 产物路径

| 平台 | 路径 |
| --- | --- |
| Windows | `bin/win-x64/release/SurgeonConsole.exe` |
| 树莓派 5 | `bin/lin-arm64/release/SurgeonConsole` |

旁路文件：`SurgeonConsole.json`、`plc_symbols.json`（改 PLC 地址只改 `symbol`）。

## Windows

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target RobotConsole
```

## 树莓派 / Linux

```bash
chmod +x scripts/build_surgeon_console_pi.sh
./scripts/build_surgeon_console_pi.sh
```

或手动：

```bash
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release -DTCGUICORE_ENABLE_NDI=OFF
cmake --build build-pi --target RobotConsole -j$(nproc)
cd bin/lin-arm64/release && ./SurgeonConsole
```

依赖：`build-essential cmake pkg-config libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev fonts-noto-cjk`

触摸全屏：`TCGUICORE_MAXIMIZE=1 ./SurgeonConsole`
