# Thunderbolt PC SDK 3.5.10

本目录为 `demo/pc` 默认使用的 Windows Thunderbolt SDK（与发行包 **3.5.10** 虚表匹配）。

## 目录布局

```
third_party/thunderbolt/
├── include/                         # 头文件
│   ├── IThunderEngine.h
│   └── ThunderEngineDefine.h
├── lib/
│   ├── thunderboltyy/release/
│   │   ├── x64/thunderbolt.lib      # 默认链接（XPROBE_THUNDER_VARIANT=thunderboltyy）
│   │   └── x86/thunderbolt.lib
│   └── thunderbolt/release/
│       ├── x64/thunderbolt.lib
│       └── x86/thunderbolt.lib
└── release/                         # 运行时（构建后拷到 exe 旁）
    ├── x64/                         # thunderbolt.dll、VideoSdk_x64.dll、依赖 DLL、*.vnmodel
    └── x86/
```

| 用途 | 路径 |
|---|---|
| 头文件 | `include/` |
| 导入库（链接符号） | `lib/<variant>/release/<arch>/thunderbolt.lib` |
| 运行时 DLL | `release/<arch>/` |

`<variant>`：`thunderboltyy`（CMake 默认）或 `thunderbolt`  
`<arch>`：与工程一致，推荐 **x64**（`cmake -A x64`）

## 与 demo 的衔接

由 [`../../CMakeLists.txt`](../../CMakeLists.txt) 自动探测：

- `XPROBE_THUNDER_INCLUDE` → `include/`
- `XPROBE_THUNDER_LIB` → 对应 arch 的 `thunderbolt.lib`
- `XPROBE_THUNDER_BIN` → `release/<arch>/`

也可手动覆盖上述 CACHE 变量。

## 重要约束

- **头文件与 DLL 必须同为 3.5.10。** 勿用仓库较新分支（如 3.13）的 `IThunderEngine.h`
  去链接本目录 DLL：`IThunderEngine` 虚表错位会在 `setLogFilePath` /
  `getVideoDeviceMgr` / 枚举摄像头等调用上直接崩溃。
- 消费方链接 import lib 时，`IThunderEngine.h` 中 `THUNDER_ENGINE` 为 `dllimport`
  （仅编译 DLL 自身时定义 `THUNDER_ENGINE_BUILD`）。
