# 核心域导入清单

基线：PrismLauncher/PrismLauncher `develop` @ `24124e5b4`

## 导入（launcher/ 下 14 个核心域目录 + 根域文件）

| 目录 | 职责 |
|------|------|
| archive/ | 实例导入导出（zip 读写任务） |
| console/ | Windows 控制台接入（进程输出基础设施） |
| filelink/ | Microsoft 认证本地回调服务 |
| include/ | 对外头文件 |
| java/ | Java 运行时探测与安装 |
| launch/ | LaunchController / LaunchTask 启动流程 |
| logs/ | 日志解析与匿名化（启动输出链路） |
| meta/ | 版本元数据索引与刷新 |
| minecraft/ | Minecraft 实例域（版本、组件、补丁、模组） |
| modplatform/ | Modrinth / CurseForge 平台 API |
| net/ | 下载器与网络任务 |
| settings/ | INI 设置对象 |
| tasks/ | Task 并发框架 |
| tools/ | 外部工具接入（JProfiler / JVisualVM 等性能分析器） |

根级域文件（BaseInstance、InstanceImportTask、FileSystem、GZip、Commandline 等）
除下列排除项外全部保留。

## 排除（启动器壳层，与核心无关）

| 项 | 理由 |
|----|------|
| launcher/ui/ | Qt 桌面界面 —— Aura 用 JavaFX/Tauri 双 UI，不需要 |
| launcher/icons/ resources/ screenshots/ translations/ | 界面资产与本地化 |
| launcher/news/ | 启动器新闻壳层功能 |
| launcher/updater/ | 启动器自更新器（含 .ui 对话框）—— Aura 分发体系自管 |
| launcher/macsandbox/ | macOS App 壳层安全书签（Application 绑定） |
| launcher/Application.* | 桌面应用入口壳层 |
| launcher/FastFileIconProvider.* FileIgnoreProxy.* | Qt 文件对话框模型 |
| launcher/CMakeLists.txt | 深度耦合 ui/Application；已重写为 `auracore_core` 静态库构建目标（阶段 1） |

## 上游跟进策略

保持 `launcher/` 内部相对路径与上游一致；后续从上游 develop cherry-pick 核心域
修复即可按路径干净套用。壳层目录永不回流。

## 阶段 1 构建记录（2026-09-06）

- 新增根 `CMakeLists.txt`：AuraCore 0.1.0 / C++23 / Qt 6.5+（Concurrent、Core、Gui、Network、NetworkAuth、Widgets、Xml），
  tomlplusplus v3.4.0 FetchContent，zlib/libarchive 外部静态依赖。
- `buildconfig/`：静态 BuildConfig 库；git commit/tag/refspec 与构建时间戳由 CMake 在 `add_library` 前注入 `AURACORE_*` 宏。
- `launcher/CMakeLists.txt`：构建 `auracore_core` 静态库，`launcher/ui/` 零引用，`LAUNCHER_APPLICATION` 宏保持网络层守卫可用。
- vendored 目标：javacheck、NewLaunch/NewLaunchLegacy（`--release 8`，产物安装到 `jars/`）、libnbt++、murmur2、qdcss。
- CI：`.github/workflows/build.yml` 覆盖 windows-x64-mingw（Qt 6.8.3 + tools_mingw1310，zlib/libarchive 源码静态构建）
  与 linux-x64（Qt 6.8.3 + apt libarchive/zlib），只构建核心目标与 jar，不发 Release。

## 阶段 2 记录（2026-09-06）

- 新增 `corebackend/`：稳定 C ABI（`auracore/backend.h`）+ `auracore_backend` 共享库 + `auracore-probe` 冒烟工具。
- 补齐 DLL 链接暴露的缺失源：`archive/`、`modplatform/helpers/`、`icons/`、`meta/JsonFormat.*`、
  `launch/TaskStepWrapper.*`、`launch/steps/QuitAfterGameStop.*`、`java/JavaMetadata.*`，
  以及 `PixmapCache::s_instance` 定义（对齐上游 Application.cpp）。
- 修复导入期遗留：murmur2 / qdcss / javacheck 共 8 个文件在 git blob 中为 NUL 污染（本地工作区干净但
  stat 缓存掩盖了差异，新 clone 才暴露），已全部重新提交。
