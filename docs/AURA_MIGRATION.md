# AuraCore 迁移蓝图（蒸馏版）

AuraCore 不是 PrismLauncher 的 fork，而是其核心域源码的独立蒸馏仓库：只保留
实例管理、版本元数据、下载、认证、启动流程、模组平台 API 等核心能力源码，
剥离 Qt 壳层、品牌、自更新器，并以 GPL-3.0 + 完整版权声明独立演进。

## 许可与版权边界

- 全仓 GPL-3.0（见 LICENSE / COPYING.md），派生作品只能延续同许可证。
- 上游版权（Prism Launcher Contributors / MultiMC Contributors / peterix）
  以 NOTICE 显式声明，文件原始版权头一律保留，绝不移除或覆盖。
- Apache-2.0 边界（Aura Plugin System）源码禁止入库；互通只走
  进程 / 协议（ABI 稳定 C 接口或 IPC）/ 数据文件边界。
- 对上游代码的实质修改在提交信息与 docs/ 中逐项声明（GPL §5a）。

## 分支模型（独立仓库，无 fork 镜像）

| 分支 | 用途 |
|------|------|
| `main` | AuraCore 主线（默认分支），一切 Aura 提交落在这里 |
| `core/*` | 功能分支，合并回 `main` |

上游同步：本地保留 `upstream` 远程指向 PrismLauncher（仅作为 cherry-pick
素材源，不构成 GitHub fork）；核心域修复按 `launcher/` 相同路径 cherry-pick。

## 与 Aura Launcher 的关系

- Aura-Launcher 保持 JavaFX（HMCL 经典 UI）+ Tauri 2/Vue（Modern UI）双 UI；
  AuraCore 只替换后端核心，不动 UI 方向。
- 迁移期 Aura-Launcher 内 HMCL 核心与 AuraCore 通过 `CoreBackend` 适配层共存，
  按能力逐项切换，每步可构建、可启动、可回退。
- UI 切换协议（`selectedUiFrontend`：`javafx` / `dev.aura.modern-ui`）与插件
  注册体系保持稳定，AuraCore 接管核心后继续兼容。

## 阶段计划

### 阶段 0 —— 蒸馏导入（已完成）
- [x] 独立仓库（非 fork）建立：剥离上游 Release/Issue/fork 网络
- [x] 核心域 14 目录 + 根域文件导入（launcher/ 路径与上游一致）
- [x] 排除壳层：ui / icons / resources / screenshots / translations /
      news / updater / macsandbox / Application / 上游 CMakeLists
- [x] LICENSE + COPYING.md + NOTICE + 导入清单
- [ ] （下一步进入阶段 1）

### 阶段 1 —— 可构建核心（当前，已完成）
- [x] 重写核心构建目标：stub BuildConfig / 脱钩 Application 与 ui 引用
- [x] 厘清 vendored 依赖（libnbt++、murmur2、qdcss、javacheck、NewLaunch）并按许可证逐项导入
- [x] CI 基线：核心目标在 Windows（MinGW / Qt 6.8.3）与 Linux（Qt 6.8.3）编译通过；macOS 延后至阶段 2 拉平
- [ ] 梳理 Aura-Launcher 与 HMCL 核心调用面，定义 `CoreBackend` ABI（随阶段 2 开工细化）

#### 阶段 1 实现要点（2026-09-06）

- `CoreApplication`：QObject 无头服务容器替代 QApplication 壳层；`ApplicationFwd.h` 保持 `APPLICATION` 宏兼容，
  全库 59 处 include 改名后核心域继续直连 settings/network/metacache/instances/accounts/javalist/icons。
- 启动链路收口：`launch(MinecraftInstance*, LaunchMode, MinecraftTarget::Ptr, MinecraftAccountPtr, QString)`
  与 `kill(BaseInstance*)` 由 CoreApplication 提供；LaunchController 生命周期由 `m_controllers` 托管，
  finished 后自动移除。
- OAuth 兼容：保留 `oauthReplyRecieved(QVariantMap)` 信号供宿主进程转发 MSA 回调。
- 无头化（qWarning + 合理默认）：CustomMessageBox / BlockedMods / OptionalMod / UntrustedMods /
  ProgressDialog / MSALogin / ProfileSelect / ProfileSetup / ChooseOfflineName / NetworkJobFailedDialog；
  可选 mod 全选、blocked mod 跳过未匹配继续、不自动进 demo、复用旧离线名、NetJob 自动重试、
  授权失败即 abort。
- BuildConfig：git commit/tag/refspec 与时间戳在 `add_library` 前求值注入 `AURACORE_*` 宏。
- vendored：libnbt++（NBT_BUILD_TESTS 默认关闭于 CI）、murmur2、qdcss、javacheck/NewLaunch（`--release 8`），
  tomlplusplus v3.4.0 走 FetchContent，zlib/libarchive 为外部静态依赖。
### 阶段 2 —— 只读能力（当前）
- [x] 实例发现与元数据读取（CoreBackend ABI v0：list_instances / get_instance，JSON 输出）
- [x] 版本清单 / 模组清单查询（离线缓存 list_component_lists；在线刷新随写路径接入）
- [x] Java 运行时探测（本地候选扫描并过滤不存在路径；版本/架构校验待 JavaChecker 集成）

#### CoreBackend ABI v0（2026-09-06）

- `corebackend/include/auracore/backend.h`：纯 C ABI（`extern "C"` + 显式导出），宿主必须先持有 QCoreApplication 且同线程调用。
- 查询全部以紧凑 JSON 返回，调用方用 `auracore_free` 释放；错误经状态码 + `auracore_last_error` 暴露。
- `auracore_backend.dll/.so` 链接整个 auracore_core（全量符号经 DLL 链接补齐：archive / helpers / icons / meta JsonFormat /
  TaskStepWrapper / QuitAfterGameStop / JavaMetadata / PixmapCache::s_instance）。
- `auracore-probe` CLI 冒烟工具：instances / java / component-lists 三查询，CI 已纳入运行。
- 严格离线：meta 无缓存时 `cached:false`，绝不触发网络（loadTask(Offline) 无文件会回源 + NetJob 自动重试死循环，
  ABI 侧用文件存在性守卫 + 30s 事件循环安全阀双保险）。
### 阶段 3 —— 写路径
- [ ] 实例创建 / 编辑 / 删除 / 导入导出
- [ ] 下载与镜像源策略
- [ ] 账户体系（微软登录、离线档案）

### 阶段 4 —— 启动流程与收尾
- [ ] 启动参数组装与进程管理
- [ ] 日志 / 崩溃收集回传双 UI
- [ ] HMCL 核心退役与数据迁移

## 原则

1. 每步迁移保持 Aura-Launcher 可构建、可启动、可回退。
2. 能力切换以设置项或构建开关暴露；默认 HMCL 核心直到 AuraCore 对应能力
   达到同等完整度。
3. GPL 边界绝对优先：含糊的代码移动先停下做许可证审查。
