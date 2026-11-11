# ROG Flow Z13 2025 GZ302EA BIOS Unlocker

**GZ302EA · BIOS 314 · V5 · Experimental / 实验性版本**

[中文说明](#中文说明) | [English guide](#english-guide) | [Contributors / 贡献者](#contributors--贡献者)

## 中文说明

### 1. 项目简介

这是面向 **ASUS ROG Flow Z13 2025 GZ302EA、314 版 BIOS、x64 UEFI 环境**的实验性 EFI 工具。它临时显示更多 AMD Overclocking（AOD）菜单选项，并提供原生 AMD CBS 页面的直接入口，包括 `Thermal Control → TjMax`。

工具不会刷写 BIOS 镜像，也不会将隐藏菜单永久加入原厂 BIOS 界面。退出 AOD 页面时，它会尝试恢复内存中的原始表单；已经成功保存的配置不会因此撤销。

**菜单可见、配置保存、芯片实际采用设置是三个不同阶段。** 此工具不保证所有高级选项都受本机硬件支持，也不保证手动 CPU/GPU 电压或高于平台默认值的温度限制生效。

### 2. 压缩包内容

本文对应的发布包名称为 `GZ302EA-BIOS-Unlocker.zip`，内容如下：

```text
GZ302EA-BIOS-Unlocker.zip
├── EFI/
│   └── BOOT/
│       └── BOOTX64.EFI     # V5 EFI 工具，28,672 字节
└── GZ302EAAS.314          # 随包附带的 BIOS 314 镜像，33,556,480 字节
```

- `BOOTX64.EFI` 是实际启动的工具。
- `GZ302EAAS.314` 是 BIOS 镜像，不是 EFI 应用。运行工具不需要刷写或加载这个镜像。
- 包内没有独立命名的 `GZ302EA314_MENU_PROBE.efi`；本教程使用实际存在的 `EFI/BOOT/BOOTX64.EFI`。

### 3. 使用前准备

1. 确认设备是 **GZ302EA**，当前安装的 BIOS 为 **314**。不要将此构建用于其他型号或固件版本。
2. 准备 FAT32 格式的 U 盘或其他 UEFI 启动介质，并备份重要数据。复制文件前，另存介质上已有的同名 EFI 启动文件。
3. 解压压缩包，将其中的 `EFI` 文件夹复制到启动介质根目录。最终路径必须是 `X:\EFI\BOOT\BOOTX64.EFI`，其中 `X:` 是实际盘符，不能多套一层工具包目录。
4. 保留原始 BIOS 设置记录和本机恢复方式。错误的电压、频率或温控设置可能导致不稳定、无法启动或数据损坏。第一次进入建议先查看并记录原值，再放弃改动退出。

应用未签名，可能被 Secure Boot 策略拒绝启动；工具不会自动更改 Secure Boot。若需更改启动安全设置，先确认本机的磁盘加密恢复密钥和恢复流程。

### 4. 启动和版本确认

通过设备的 UEFI 启动菜单选择启动介质，具体按键以设备手册为准。也可以在 UEFI Shell 中进入介质对应的文件系统，运行：

```text
\EFI\BOOT\BOOTX64.EFI
```

**不要使用 EZ Flash 刷写 `BOOTX64.EFI`。** 随包附带 BIOS 镜像不表示本工具要求先刷 BIOS。

工具标题应包含：

```text
GZ302EA .314 V5 - AOD/CBS SAVE REPAIR + THERMAL MENU - EXPERIMENTAL
```

核对屏幕上的 BIOS、Product 和表单匹配信息。若报告型号、版本或布局不匹配，停止使用并保留完整提示，不要仅重命名文件后强行使用。

### 5. 主菜单按键

- **`U` / `2`**：打开临时显示更多选项的 AOD 菜单。
- **`O` / `1`**：打开原始 AOD 菜单。
- **`T`**：直达原生 `AMD CBS → SMU Common Options`，其中包含 `Thermal Control / TjMax`。
- **`B`**：打开完整的原生 AMD CBS 菜单。
- **`C`**：重新查询 SMU 能力。
- **`R`**：读取当前所选配置库。启动后及使用 `U/O` 后为 AOD；使用 `T/B` 后为 CBS。
- **`Q` / `Esc`**：退出工具。

字母不区分大小写。在原厂页面中，按该页面说明使用 `Enter`、`Esc` 和 `F10`。数字键若只显示 `Ignored key` 或反复返回主菜单，请改用字母快捷键，并记录 `Unicode / Scan` 行。

CBS 页面保持原生表单，不套用 AOD 的七处菜单显示补丁。AOD 的 Curve 控件仍保留 `PBO = Advanced` 的启用条件。

### 6. 修改与保存

常用入口如下，具体可用项目以本机原厂页面为准：

- **CPU Curve Optimizer**：`U → Precision Boost Overdrive → Advanced → Curve Optimizer`。
- **核显 Curve Optimizer**：`U → Precision Boost Overdrive → Advanced → GFX Curve Optimizer`。
- **CBS 温度设置**：`T → Thermal Control = Manual → TjMax`。
- **AOD 温度设置**：`U → Precision Boost Overdrive → Advanced → Platform Thermal Throttle Ctrl = Manual → Platform Thermal Throttle Limit`。

CPU Curve 可按界面选择 `All Cores` 或 `Per Core`。`Negative / Positive` 表示曲线移动方向，`Magnitude` 是曲线档位，不是固定的毫伏数。核显 Curve 同样使用方向和档位。固定频率／电压的手动超频属于另一种调节方式，不能与 Curve 混为一谈。

保存步骤：

1. 记下原值，只修改明确理解的项目；数值输入后先按 `Enter` 完成编辑。
2. 在原厂表单中按 **`F10`**，按界面提示确认保存，并记录工具的保存报告。
3. 保存流程完成后重启，再进入相同页面或查看 `STARTUP` / `R` 的回读，确认配置是否保留。逐核设置应逐项核对，不能只看全核摘要。
4. 进入系统后检查实际运行限制、性能和稳定性，确认硬件是否采用设置。

V5 将提交交给原厂保存回调处理。遇到符合条件的保护拒绝时，它会核对请求并尝试一次临时写入上下文重试，随后恢复保护并检查回读。该修复覆盖所支持的 **AOD/CBS 配置库中的普通 F10 提交**；其他配置库及独立操作按钮的保存路径不在此范围内，不能保证所有高级设置都可应用。

工具不预设电压、Curve 或 TjMax 数值。若只是查看，退出时选择放弃保存；原厂回调仍可能初始化或规范化变量，因此这也不是严格只读的取证工具。

### 7. TjMax 与 Windows 温控覆盖

CBS 的 `Thermal Control / TjMax` 与 AOD 的 `Platform Thermal Throttle` 属于不同配置库，工具不会自动同步两者。

例如，设置 `TjMax = 98°C` 后，重启回读到 `ThermalControl=1、TjMax=98`，只能证明 CBS 配置保留，不能证明芯片运行阈值已提高到 98°C。原厂 CBS 帮助注明仍执行 IRM 限制，AOD 帮助将其温度功能描述为降低允许温度；菜单中的可输入范围不构成突破平台限制的保证。

Windows 驱动、厂商服务和模式管理软件还可能重新下发温度及功耗策略。例如 BIOS 保留 98°C，但 G-Helper 的 `Read Limits` 显示 `Temp: 95°C`，表示该软件此时回读到的运行时限制仍是 95°C。AIDA64 的最高实测温度本身也不足以判断限制是否已改变。

G-Helper 的 `Read Limits` 是读取操作，但启动 G-Helper 可能应用性能模式；高级页未勾选 `Auto Apply` 不表示模式初始化停用。诊断时记录软件启动、模式切换和限制回读的先后顺序。

**此压缩包不包含 G-Helper 修复或 Windows 温度恢复程序。** EFI 工具不驻留 Windows，也不会在性能模式切换或睡眠唤醒后自动重设温度。

### 8. 故障排查

- **无法启动**：检查 FAT32、根目录 EFI 路径、UEFI 启动方式和 Secure Boot 策略。
- **版本不符、表单缺失或七处匹配不齐**：保留完整提示，核对固件版本和表单状态，不强行套用补丁。
- **保存后重启恢复默认**：保留 `First native save status`、`Native save retry`、回读验证和恢复保护等完整报告；不能只凭保存对话框判断成功。
- **数值保留但效果不变**：核对配置库及启用条件，再检查系统中的实际限制与模式软件覆盖情况。
- **`0x800000000000000F`**：访问被拒绝，可能涉及写保护或原厂安全校验；单凭此码不能确定唯一原因。
- **`0x800000000000000E`**：相应对象未找到。
- **`0x8000000000000003`**：相应操作不支持。
- **`0x8000000000000021`**：本工具用于报告数据布局或回读校验不匹配。

反馈问题时请提供：机型、BIOS 版本、工具标题、按键及菜单路径、修改前后数值、F10 完整报告、重启后的回读，以及 Windows 中使用的模式管理软件与实际限制。解释状态码时应结合其前面的标签。

### 9. 文件校验与验证范围

以下为本次核对的文件 SHA-256，可用于确认文件是否与本文对应版本一致：

```text
EFI/BOOT/BOOTX64.EFI
8a94625780dde70c1e3ca010d30e27b6b674541e1016eff1d2240e7567cfb522

GZ302EAAS.314
accf8cad56b556b007689b82b426d27a1e36fc9722508a84a86d99f1c0d22ba7
```

V5 构建记录包含 49 项离线检查，覆盖菜单补丁、模拟 UEFI 浏览／恢复、AOD/CBS 原厂保存重试与回读等路径。这些检查不是所有实机高级设置生效的证明；实际降压效果、温度阈值和长期稳定性仍需在目标设备上分别验证。

## English guide

### 1. Overview

This experimental EFI application targets the **ASUS ROG Flow Z13 2025 GZ302EA with BIOS 314 in an x64 UEFI environment**. It temporarily exposes additional AMD Overclocking (AOD) options and provides direct access to native AMD CBS pages, including `Thermal Control → TjMax`.

The tool does not flash a BIOS image or permanently add menus to the stock BIOS interface. After an AOD browser session, it attempts to restore the original in-memory forms. This does not undo configuration that was successfully saved.

**Menu visibility, configuration persistence, and hardware acceptance are separate stages.** The tool does not guarantee that every advanced option is supported, that manual CPU/GPU voltage control works, or that a temperature limit above the platform default takes effect.

### 2. Package contents

This README describes `GZ302EA-BIOS-Unlocker.zip`:

```text
GZ302EA-BIOS-Unlocker.zip
├── EFI/
│   └── BOOT/
│       └── BOOTX64.EFI     # V5 EFI application, 28,672 bytes
└── GZ302EAAS.314          # Included BIOS 314 image, 33,556,480 bytes
```

- `BOOTX64.EFI` is the application to launch.
- `GZ302EAAS.314` is a BIOS image, not an EFI application. Running the tool does not require flashing or loading that image.
- There is no separately named `GZ302EA314_MENU_PROBE.efi` in this package. These instructions use the included `EFI/BOOT/BOOTX64.EFI`.

### 3. Preparation

1. Confirm that the device is a **GZ302EA** running **BIOS 314**. Do not use this build on another model or firmware version.
2. Prepare FAT32 USB or other UEFI boot media and back up important data. Back up any existing EFI boot file at the destination before replacing it.
3. Extract the ZIP and copy its `EFI` folder to the media root. The final path must be `X:\EFI\BOOT\BOOTX64.EFI`, where `X:` is the actual drive letter, without an extra package directory above `EFI`.
4. Record the original BIOS settings and the device recovery procedure. Incorrect voltage, frequency, or thermal settings can cause instability, boot failure, or data corruption. On the first visit, inspect and record the original values, then exit without saving changes.

The application is unsigned and may be rejected by Secure Boot policy. It does not change Secure Boot automatically. Before changing boot security settings, confirm that disk-encryption recovery keys and the device recovery procedure are available.

### 4. Launch and version check

Select the boot media from the device's UEFI boot menu; consult the device manual for the exact keys. Alternatively, select the media filesystem in a UEFI Shell and run:

```text
\EFI\BOOT\BOOTX64.EFI
```

**Do not flash `BOOTX64.EFI` with EZ Flash.** Including a BIOS image does not mean that flashing the BIOS is a prerequisite for this tool.

The title should contain:

```text
GZ302EA .314 V5 - AOD/CBS SAVE REPAIR + THERMAL MENU - EXPERIMENTAL
```

Check the BIOS, Product, and form-matching messages. If the model, version, or layout does not match, stop and retain the complete message rather than renaming files and forcing the build to run.

### 5. Main-menu keys

- **`U` / `2`**: open AOD with temporary visibility edits.
- **`O` / `1`**: open the original AOD menu.
- **`T`**: open native `AMD CBS → SMU Common Options`, including `Thermal Control / TjMax`.
- **`B`**: open the complete native AMD CBS menu.
- **`C`**: query SMU capabilities again.
- **`R`**: read the selected configuration store: AOD at startup and after `U/O`, or CBS after `T/B`.
- **`Q` / `Esc`**: exit the tool.

Letters are case-insensitive. Inside native pages, use `Enter`, `Esc`, and `F10` as described by the firmware interface. If a number key produces `Ignored key` or repeated main menus, use a letter shortcut and record the `Unicode / Scan` line.

CBS pages retain their native forms and do not receive the seven AOD visibility edits. AOD Curve controls retain their `PBO = Advanced` dependency.

### 6. Editing and saving

Common entry points are listed below; available settings depend on the native pages on your device:

- **CPU Curve Optimizer**: `U → Precision Boost Overdrive → Advanced → Curve Optimizer`.
- **Integrated-GPU Curve Optimizer**: `U → Precision Boost Overdrive → Advanced → GFX Curve Optimizer`.
- **CBS thermal settings**: `T → Thermal Control = Manual → TjMax`.
- **AOD thermal settings**: `U → Precision Boost Overdrive → Advanced → Platform Thermal Throttle Ctrl = Manual → Platform Thermal Throttle Limit`.

For CPU Curve, choose `All Cores` or `Per Core` as provided by the interface. `Negative / Positive` controls the curve direction, and `Magnitude` uses curve steps, not a fixed number of millivolts. The integrated-GPU Curve also uses a sign and magnitude. Manual overclocking with fixed frequency/voltage is a separate control from Curve Optimizer.

To save:

1. Record the original values and change only settings you understand. Press `Enter` to finish editing a numerical field.
2. Press **`F10` inside the native form**, confirm as prompted, and record the tool's save report.
3. After the save process completes, reboot and revisit the same page or inspect `STARTUP` / `R` readbacks to check persistence. Check per-core entries individually rather than relying on an all-core summary.
4. In the operating system, check actual runtime limits, performance, and stability to establish hardware acceptance.

V5 delegates submission to the native firmware save callback. For an eligible protection rejection, it validates the request and attempts one retry in a temporary write context, then restores protection and checks readback. This repair covers **ordinary F10 submissions in the supported AOD/CBS configuration stores**. Other stores and separate action-button save paths are outside its scope; successful application of every advanced setting is not guaranteed.

The tool does not preset voltage, Curve, or TjMax values. If you are only inspecting settings, discard changes on exit. Native callbacks can still initialize or normalize variables, so this is not a strictly read-only forensic browser.

### 7. TjMax and Windows thermal-policy overrides

CBS `Thermal Control / TjMax` and AOD `Platform Thermal Throttle` belong to separate configuration stores. The tool does not synchronize them automatically.

For example, after setting `TjMax = 98°C`, a post-reboot readback of `ThermalControl=1, TjMax=98` establishes CBS configuration persistence only. It does not prove that the chip uses a 98°C runtime limit. Native CBS help states that the IRM limit remains enforced, while AOD help describes its thermal control as decreasing the allowed temperature. An editable range is not a guarantee that platform limits can be exceeded.

Windows drivers, vendor services, and mode-management software may reapply thermal and power policies. If the BIOS retains 98°C but G-Helper `Read Limits` reports `Temp: 95°C`, the runtime limit read back by that software is still 95°C at that time. AIDA64's highest observed temperature alone is also insufficient to determine whether a limit changed.

G-Helper `Read Limits` is a read operation, but launching G-Helper may apply a performance mode. Leaving `Auto Apply` unchecked on its Advanced page does not disable mode initialization. Record the order of application launch, mode changes, and limit readback when diagnosing overrides.

**This package includes neither a G-Helper fix nor a Windows temperature-restoration utility.** The EFI application is not resident in Windows and does not reapply temperature after performance-mode changes or sleep/resume.

### 8. Troubleshooting

- **Cannot launch**: check FAT32, the root EFI path, UEFI boot selection, and Secure Boot policy.
- **Version mismatch, missing forms, or fewer than seven expected matches**: retain the complete message and verify firmware and form state; do not force the patch.
- **Defaults return after saving and rebooting**: retain the complete `First native save status`, `Native save retry`, readback-verification, and protection-restoration reports. A save dialog alone does not establish success.
- **Values persist without an observed effect**: check the store and enable conditions, then inspect runtime limits and mode-management overrides.
- **`0x800000000000000F`**: access denied; write protection or native security validation may be involved. The code alone does not identify one definitive cause.
- **`0x800000000000000E`**: the relevant object was not found.
- **`0x8000000000000003`**: the relevant operation is unsupported.
- **`0x8000000000000021`**: used by this tool for data-layout or readback-verification mismatches.

When reporting an issue, include the model, BIOS version, tool title, keys and menu path, original/requested values, complete F10 report, post-reboot readback, and Windows mode-management software/runtime limits. Interpret status codes together with their preceding labels.

### 9. File integrity and validation scope

SHA-256 values of the files checked for this README:

```text
EFI/BOOT/BOOTX64.EFI
8a94625780dde70c1e3ca010d30e27b6b674541e1016eff1d2240e7567cfb522

GZ302EAAS.314
accf8cad56b556b007689b82b426d27a1e36fc9722508a84a86d99f1c0d22ba7
```

The V5 build records contain 49 offline checks covering menu patching, mocked UEFI browsing/restoration, native AOD/CBS save retries, and readback paths. These checks do not establish that every advanced setting works on hardware. Actual undervolting effects, thermal limits, and long-term stability require separate validation on the target device.

## Contributors / 贡献者

- [@KWOLnet](https://github.com/KWOLnet) — Project maintenance, device testing, and validation. / 项目维护、设备实测与验证。
- **[OpenAI Codex](https://openai.com/codex/) (AI assistant / AI 助手)** — Assisted with firmware analysis, EFI tooling implementation and debugging, save-path investigation, and bilingual documentation. / 协助固件分析、EFI 工具实现与调试、保存路径排查及中英文文档编写。
