# Vokie AI Passport 固件

[English](README.md) | 简体中文

FoloToy AI Passport 是一个开放式可穿戴 AI 硬件。本仓库保留上游开发基线，并把 Vokie AI Passport 语音输入固件作为当前应用；仓库同时保存开发应用所需的**硬件事实、稳定接口、资源边界、参考实现和验收方法**。

这个仓库的组织方式是：

- `main` 包含 Vokie BLE 语音输入应用，并保留对 FoloToy 上游基线和历史的归属说明；
- `components/bsp` 隔离板级差异，为应用提供稳定 API；
- 历史 `demo/*` 源码和上游分支仅作为参考，不参与当前固件编译；
- AI 开发约定见 [`AGENTS.zh_CN.md`](../AGENTS.zh_CN.md) 与 [`docs/development/ai-guide.zh_CN.md`](development/ai-guide.zh_CN.md)；完整硬件上下文和故障知识见 [`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md`](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)；
- 构建结果与真机结果分开记录，禁止把"编译通过"描述成"硬件验证通过"。

## 硬件能力契约

下表描述当前固件实际使用的硬件和应用能力，而不是芯片数据手册中所有可能的能力。

| 能力 | 已确认实现 | 应用接口 | 必须遵守的边界 |
| --- | --- | --- | --- |
| 显示 | ST7789P3，240 × 320，竖屏 RGB565，SPI2 40 MHz；状态界面与 LEDC 背光控制 | `bsp_display_*`、`bsp_lvgl_*`、`ui_status_*` | ESP32-C3 无 PSRAM；当前为小型单 DMA 缓冲；BSP 未暴露 LCD MISO、触摸或 TE 接口 |
| 输入 | `UP` / `DOWN` / `OK` 三键，共用 GPIO0 ADC 电阻分压；映射为 PTT、发送、删除、清空与取消 | `bsp_button_init()`、`bsp_button_read_mv()` | 回调运行在 button 组件任务中，不能阻塞；不能再创建第二个 ADC1 unit |
| 音频 | ES8311 以 16 kHz、16-bit、单声道采集麦克风；每 20 ms 独立编码为 IMA ADPCM | `bsp_audio_*`、`vokie_ble_start()` | PCM 读取是阻塞调用并运行在工作任务；录音要求兼容 BLE 主机完成订阅且 ATT MTU 不小于 185 |
| Bluetooth LE | 以 `Vokie Passport` 名义进行可连接 NimBLE 广播；提供 Control、Audio 与 Device info 特征 | `vokie_ble_*` | 仅允许一个连接；协议 V1 不提供配对、对端身份认证或应用层加密 |
| 背光 | 活动时 65%，处理时 38%，3 秒后降至 18%，20 秒后关闭 | `ui_status_touch()`、`ui_status_set_state()` | 熄屏暂停 LVGL 工作，音频闲置时允许自动轻睡眠；LCD 控制器仍供电，BLE 保持启用 |
| 保护存储 | 3 MB factory app，以及固定的 `cardid` 和永久 Recovery 区域 | `partitions.csv`、bootloader hook | 不得覆盖已写入的设备身份或 Recovery 载荷；使用文档规定的安装路径 |
| 日志与烧录 | ESP32-C3 原生 USB Serial/JTAG | ESP-IDF console | GPIO18/19 保留给 USB；UART0 默认 TX GPIO21 与背光冲突 |

所有引脚、地址、面板参数和按键电压窗口只在 [`components/bsp/include/bsp_pins.h`](../components/bsp/include/bsp_pins.h) 定义。应用代码不得复制这些常量。完整引脚表、面板初始化、ADC 阈值、I2C 地址规则、音频时钟和内存说明见 [AI 硬件开发指南](hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)。

当前应用使用 ESP-IDF 定时器、FreeRTOS 任务、NVS 初始化、NimBLE 和 LVGL。Wi-Fi 与历史菜单 Demo 不参与当前固件编译。当前产品与固件基线使用 8 MB Flash，包含 3 MB factory-app 分区，并固定保留设备身份与永久 Recovery 区域，使二创固件仍可通过小程序安装。主机契约见 [BLE 协议](ai-passport-ble-protocol.zh_CN.md)。

### 不属于当前能力契约的事项

公开固件能力以表中接口为限，不能仅凭 ESP32-C3 芯片能力推断其他板级接口。新增硬件接口必须提供明确的 BSP 定义和实机验收标准。

## 安全扩展固件

给 AI 助手的有效需求应同时说明主机行为和设备验收标准：

```text
为 Vokie AI Passport 固件增加一个功能，但不要改变 BLE V1 线协议。
保留受保护的 cardid 和 Recovery 分区，把硬件事实放在 components/bsp，
把产品行为放在 main。从 `main` 创建 `feature/*` 分支，运行
./tools/validate.sh --static 和 --firmware，并把构建结果与尚未执行的
BLE、音频、按键真机验收分别报告。
```

开始前先看 [`reference/`](reference/README.zh_CN.md) 中可复用的经验与已归档应用。当前 `main` 已经包含 Vokie 语音输入应用；不要恢复历史 demo，也不要仅凭 ESP32-C3 数据手册推断板卡未公开的接口。

需求越具体，AI 助手越容易一次实现正确。建议说明：

- 用户流程：每个页面显示什么，三个按键的短按、双击、长按分别做什么；
- 状态与数据：是否计时、断电保存、联网、录音或与电脑通信；
- 体验目标：字体、颜色、动画、声音、响应时间和异常状态；
- 限制条件：是否允许替换主菜单、增加依赖、使用 Flash 或改变默认交互；
- 验收标准：哪些行为必须自动测试，哪些必须在真实硬件观察。

若需求没有给出所有细节，AI 助手可以在不改变产品方向的范围内采用保守默认值，但应在交付中列出这些假设。涉及新接线、电源安全、硬件版本或不可恢复数据格式的决定必须先确认。

## 上游历史示例

FoloToy 原始仓库保留了一些历史 `demo/*` 分支，可作为设计参考。它们不是本独立 Vokie 仓库的分支，也不属于当前固件能力契约。只有在明确配置上游 remote 后，才应查看这些示例：

```bash
git remote add folotoy https://github.com/FoloToy/ai-passport.git
git fetch --no-tags folotoy 'refs/heads/demo/*:refs/remotes/folotoy/demo/*'
git branch -r --list 'folotoy/demo/*'
git diff main...folotoy/demo/tetris-game -- main components tests
git show folotoy/demo/tetris-game:main/demo_tetris.c
```

示例可能以不兼容的方式修改同一菜单、配置或驱动。它们只是历史参考，不会自动兼容当前 Vokie 应用或 BSP 能力契约。

新功能应从本仓库当前公开的 `main` 创建短生命周期的 `feature/*` 或 `fix/*` 分支，不要直接在 `main` 上开发。

```bash
git switch main
git switch -c feature/my-passport-app
```

## 项目结构

```text
components/bsp/include/  BSP 公开 API 与 bsp_pins.h 硬件事实
components/bsp/src/      显示、按键、音频、电池、共享 I2C 实现
main/                    Vokie BLE 外设、音频传输与状态界面
vokie-plugin/            可导入、可修改的 Vokie 桌面 Plugin
tests/                   可脱离硬件运行的轻量逻辑测试源
tools/                   本地与 CI 共用的验证及固件校验脚本
docs/                    项目说明、变更记录、工程/协作规范与设计参考
.github/                 GitHub 社区文档、PR 模板、Issue Form 与 CI 工作流
sdkconfig.defaults       ESP32-C3、USB console、Flash、LVGL 默认配置
partitions.csv           应用与设备身份/Recovery 保护分区布局
dependencies.lock        可复现的 ESP-IDF Managed Component 解析结果
AGENTS.md                AI agent 必读入口（与 AGENTS.zh_CN.md 配对）
CLAUDE.md                Claude Code 指向 AGENTS.md 的入口（含中文配对）
LICENSE                  仓库许可证
```

## 文档索引

本仓库文档按功能域组织。`authoritative` 指对开发与协作有约束力的文档；`参考` 指提供背景或索引的文档。

- [`docs/ai-passport-ble-protocol.zh_CN.md`](ai-passport-ble-protocol.zh_CN.md) — 公开的 BLE V1 服务、消息、音频分片、生命周期与安全边界。
- [`docs/device-diagnostics.zh_CN.md`](device-diagnostics.zh_CN.md) — 设备数值事件持久化、有界存储、USB 导出与解码。
- [`docs/THIRD_PARTY_NOTICES.zh_CN.md`](THIRD_PARTY_NOTICES.zh_CN.md) — 依赖版本、许可证与再分发说明。
- [`docs/development/`](development/README.zh_CN.md) — 工程规则与可复用工作流：`ai-guide.md`、`engineering/`、`ci/`、`release/` 区。其 README 列明它们。
- [`docs/contribution/`](contribution/README.zh_CN.md) — 协作、文档与提交/PR 约定。
- [`docs/hardware-design/`](hardware-design/README.zh_CN.md) — 板卡事实、约束、验收矩阵与排障。
- [`docs/reference/`](reference/README.zh_CN.md) — 参考资料：按贡献者（`reference/<username>/`）组织可复用开发经验与已归档应用于册。
- [`docs/brand/`](brand/README.zh_CN.md) — 公开品牌与产品语言（`brand-and-product.zh_CN.md`）与官方产品视觉参考。
- [`docs/`](README.zh_CN.md) 顶层 — [`CHANGELOG.zh_CN.md`](CHANGELOG.zh_CN.md)、[`brand-and-product.zh_CN.md`](brand/brand-and-product.zh_CN.md)、[`fork-guide.zh_CN.md`](fork-guide.zh_CN.md)。

GitHub 社区治理文档：[CONTRIBUTING.zh_CN.md](../.github/CONTRIBUTING.zh_CN.md)、[CODE_OF_CONDUCT.zh_CN.md](../.github/CODE_OF_CONDUCT.zh_CN.md)、[SECURITY.zh_CN.md](../.github/SECURITY.zh_CN.md)、[SUPPORT.zh_CN.md](../.github/SUPPORT.zh_CN.md)。

> 注：本 README 只描述产品与仓库，不含给 AI 的执行说明；AI 开始开发前请先读根目录 `AGENTS.zh_CN.md`，再按任务路由读取相关文档。
