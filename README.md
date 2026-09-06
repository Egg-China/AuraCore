# AuraCore

Aura Launcher 的下一代启动器核心：从 [PrismLauncher](https://github.com/PrismLauncher/PrismLauncher)
蒸馏出的核心域能力（实例管理、版本元数据、下载、认证、启动流程、模组平台 API），
不含 Prism 的 Qt 桌面壳层、品牌资源与自更新器。

> 状态：**阶段 0 —— 核心源导入 + 版权声明**。源码树已按核心域裁剪导入，
> 尚未接通构建（上游 CMake 深度耦合 Application/ui，阶段 1 重写核心构建目标）。

## 这是什么 / 不是什么

| | 说明 |
|---|---|
| 是 | 独立仓库、GPL-3.0 派生作品、只保留核心域的 Prism 源码蒸馏 |
| 不是 | PrismLauncher 的 GitHub fork；不继承其 Release / Issue / fork 网络 |
| 不是 | 可直接编译的库（阶段 1 交付 BuildConfig/ui 解耦与核心构建目标） |

## 版权

见 [NOTICE](NOTICE)。上游基线 `24124e5b4`（develop）。所有导入文件保留原始
版权头；AuraCore 修改逐项记录于提交历史。

## 结构

- `launcher/` —— 上游核心域源码（路径与上游保持一致，便于 cherry-pick 跟进）
- `docs/IMPORT_MANIFEST.md` —— 导入 / 排除清单及理由
- `docs/AURA_MIGRATION.md` —— Aura Launcher 集成路线

## 与 Aura Launcher 的关系

Aura-Launcher 保持 JavaFX + Tauri 2/Vue 双 UI；AuraCore 只替换其后端核心，
通过适配层逐能力切换，任何阶段都可回退到 HMCL 核心。
