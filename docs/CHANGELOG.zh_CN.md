<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

- 为每 20ms 一帧的音频明确请求固定 15ms BLE 间隔，避免请求范围继续允许 Windows 选择 30ms。增加分开的 MSYS 池快照，区分 ATT 头部池与音频负载池耗尽。保留重试时限、缓冲数量及 BLE V1；仍需核对中心设备实际采用的间隔并重复真机语音验收。

- 音频改用现有大 MSYS 池并明确预留头部，小池保留给控制与接收，修复零大小最佳适配接口导致“大池空闲却分配失败”。保持 12/24 块总预算及原重试时限，增加所有权/容量回归测试并校验固定池配置。

- 发送失败的录音改为有界重试上报会话错误，不再提交残缺音频；结束事件无法入队时仅请求断开相应连接。上轮主机结果完成前拦截新的 UP 开始。全部诊断 Flash 读写延后至音频空闲，使用 192 条有界队列，新增分配/协议栈失败阶段、MSYS 余量、拦截启动与结束通知证据。保留 80ms 音频重试上限及 BLE V1 格式，长录音与 Windows 使用效果需真机验证。

- 新增 256 KiB 设备本地循环诊断日志，记录 PTT 停止来源、按键/主机/连接事件、音频时序与入队错误。CRC 提交记录跨复位保留并按扇区轮转；采用音频空闲后读写与擦除及纯数值元数据。增加 USB 导出解码、日志/解码器测试和受校验的格式头安装数据，保留 3 MiB 应用、身份及 Recovery 约束。长期时序与功耗影响仍需真机验证。

- 对临时入队失败的 BLE 音频通知最多重试 80 ms，不重发已接受的帧；PTT 开始/结束通知改由音频工作任务发送，等待上限为 200 ms。仅在入队成功后递增音频序号，重连或切换会话时取消旧重试，请求 15–30 ms 连接间隔及更长的链路层数据包，并记录协商参数和准确传输错误。主机明确报错时停止已放弃的麦克风采集。Windows 吞吐和完整语音链路仍需真机验收。

- 停止音频时钟前明确静音并关闭扬声器 DAC，保留麦克风偏置与滤波器状态。音量为零时关闭输出，仅在音频流运行且明确请求播放时重新开启；读回寄存器校验失败则保持流运行并允许重试。此项处理排查刷写后偶发杂音时发现的待机输出隐患，实际声音仍需实机确认。

- 短按改为去抖后的松手即执行，不再等待双击判定；连续短按分别响应。OK 保留 650 ms 长按清空/取消，快按后紧接长按也有效，长按松手不再额外删除。

- 原生 USB Serial/JTAG 总线活跃时持有禁止轻睡眠锁及 80 MHz APB 频率下限锁，每 2 秒轮询，并以 5 秒释放宽限容忍短暂 SOF 间隙。总线长时间静默或拔线后重新允许电池待机。主机挂起及重插行为仍需硬件验收，此前孤立断连的原因尚未确认。

- 待机期间保留 ES8311 偏置及滤波器状态，暂停 I2S/DMA，避免完全重启 codec 导致录音开头反复削波。恢复新的 DMA 后先读走一个不完整的 I2S 槽采样（16 kHz 下为 62.5 微秒），再返回完整 PCM 帧，避免首采样尖峰进入 ADPCM 初始值。暂停/恢复失败时保留必要电源锁并支持重试，不中止固件；保留的模拟静态电流尚未测量。

- 降低熄屏待机活动：闲置时暂停 I2S，录音任务等待事件，熄屏暂停 LVGL tick/刷新/动画，并启用动态调频、自动轻睡眠及 BLE Modem-sleep。ADC 按键改为 20 ms 扫描，相同主机状态不再重置亮屏超时。实际节电幅度、唤醒和音频表现仍须真机测量。

- 新增两秒 Vokie 开机音，以适中音量在音频任务中每次开机播放一次；开始语音输入时让位给录音，随后静音并丢弃开机播放期间的麦克风采样。
- 在 Vokie 完成 BLE 握手前，状态界面保持 OFFLINE；未连接时按 DOWN 或 OK 也不会再误显示 READY。
- 将 VOICE/SEND/UNDO 放在靠近右边缘的位置，用低调的细线指向对应按键。每个右侧按键仅显示自身文案与指引线。单项提示统一使用原来 SEND 所在的中间位置及单行紧凑背景，指引线从这里连向对应实体按键；换键时替换上一项提示。开机保留原来的三项位置和指引线路径，展示全部三项，三秒后淡出。独立硬件电源键不提供应用层按下事件。
- 将语音状态及说明放在标志上方，READY 顶部对齐上方按键指引线；Vokie Power 移至标志下方，文字底部对齐下方按键指引线。标志的视觉中心对齐 SEND 行，主要内容保持水平居中。
- 电池图标和电量百分比以低调灰色（`#646C78`）常驻屏幕右上角，亮屏时持续可见，每 30 秒刷新。实体按键选中的提示在录音或处理中保持显示，空闲按键提示超时 3 秒后淡出；开机全部提示固定三秒后淡出，即使主机进入活动状态也不延长。状态变化和电量刷新不会唤出提示，电量刷新保留原有屏幕变暗和熄屏策略。标题采用 22 px Barlow Condensed Bold 窄体粗字，移除重复的状态圆点。20% 及以下标红；SOC 无效时显示实测电压，读数均不可用时显示 `--%`。
- 使用 FoloToy PR #38 的原装 520 mAh 电池参数初始化 CW2017，恢复未配置电量计的有效 SOC，并提供读回校验、有界就绪等待和避免重复写入。

- 在固件旁新增独立的 AI Passport Vokie Plugin，支持导入兼容的 Vokie 版本并开发自定义桌面端集成；同时明确当前 Vokie 的内置连接为默认使用路径。
- 用 Vokie AI Passport 语音固件替换硬件 Demo 菜单：实现并公开 BLE V1 外设协议、16 kHz 单声道 IMA ADPCM 传输、实体 PTT/编辑按键、主机驱动的状态界面和三级背光省电策略。
- 增加 fork 专用项目 README、BLE 协议说明、第三方声明和独立的 Vokie 品牌素材条款，为公开分发做好准备。
- 移除继承自上游的自动同步工作流；本独立衍生项目的下游 fork 通过显式评审合并公共仓库更新。

- 按功能域整理文档并采用双入口：根目录 `AGENTS.md` 变为薄路由（只保留硬约束与任务路由），详细的 AI 开发工作流下沉到 `docs/development/ai-guide.md`，`agent-guide.md` 并入其中。为 `docs/development/` 增加二级分区（`engineering/`、`ci/`、`release/`），把 `plays/` 应用档案与 `experiences/` 移入带专属 README 的 `docs/reference/` 参考区；删除 `docs/software-design/`（空脚手架）；把 `assets/{fonts,images,music}/README` 三个叶子 README 并入 `assets/` README；把 `project-completion` 的六个子文档压平为单文件；并把每个目录统一为单一 README，消除所有 `INDEX` 文件与一处重复经验索引。所有交叉引用与文献链接已更新；未丢弃任何内容。

- 将小程序 BLE 安装兼容提升为二创模板强制契约：固定保护 `cardid`/Recovery 分区，
  保留上键持续 5 秒进入 Recovery 的 bootloader hook，并在 CI 强制校验合并镜像结构、
  分区表 MD5/范围、3 MB 应用上限和保护分区数据不入包。
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `project-completion`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
