# DriftSeq firmware v1 — scope plan（真机验证第一批）

**日期**: 2026-10-16 · **状态**: proposal（John 拍板范围后开工）
**触发**: John——"我感觉现在可以开始写 firmware 了，至少单步录音、播放、编辑已经讨论得
比较充分，可以准备真机验证了。realtime 录音、microstep 可以稍等。"

## v1 范围（真机第一批 = "能编、能播、能听"）

**进**：
1. **引擎移植**（host/driftseq.c 已是纯 C，零改动或近零改动——pi32v2 编译即可）：
   ds_tick / step-entry 覆盖制 / OCT 簇 / GLO+K1 START / rest-tie / voice add-remove-set /
   Euclid floor 形（Toussaint 规范相位）+ eu_rot 生效（03 §4.2 纸面定稿，golden 重录一次）
2. **UI 移植**（emu rev.14g 的 draw 分支照抄，canvas → LCD）：HOME hero / MELODY GRID
   （24px bar + 嵌页 EDIT）/ DRUM GRID（9×11 lanes + 单大卡）/ 底栏 prefix-free
3. **输入层**（fm1-ctrl 已验证的 keyscan/encoders/leds/midi_io 拷贝）：
   focus-layer 路由（EDIT 键 toggle）/ 覆盖制弹键 / 黑键选鼓 / 白键 toggle
4. **USB MIDI out**（fm1-ctrl 传输层；gm1-ctrl 的 byte-wire 契约）
5. **时钟**：内部 BPM（ALGORITHM 旋钮），无外同步（v1.1）

**不进（等真机验证后第二批）**：
- 实时录音（REC arm/quantize/overdub/note repeat）——02 §5 表仍纸面
- microstep / per-voice prob（03 §8 #7/#12，动 ds_note 结构）
- EAS GM 监听（04 spec，+2-3 天增量；v1 用 USB out 到外部音源听）
- Euclid/ARP 页、GLO 全局页的完整版、pattern 存储/chain

## 复用清单（0 重写）

| 模块 | 来源 | 状态 |
|---|---|---|
| keyscan/encoders/leds/midi_io | fm1-ctrl `firmware/` | host-tested，直接拷 |
| 平台层（crt0/usb/lcd/gfx/panel.c） | fm1-ctrl vendored Felucca | 同上 |
| 引擎 host/driftseq.c | 本 repo | golden 契约在（5828982717F322ED）|
| 屏绘 draw* | web/emu.js → C 翻译 | 语义已锁定（rev.14g）|

## 真机验证清单（第一批固件的标准）

1. 步录入：弹键写步（单音/和弦≤4）、覆盖制改写、cursor 前进、st64 死停
2. GRID 导航：K1 cursor 轴、GLO+K1 START、K2 LEN、亮度 loop 范围可见
3. EDIT 层：EDIT 键 toggle、弹键不前进、同音删、满槽替换最高音、SELECT 切卡、K1-K4 参数
4. 鼓轨：黑键选鼓、白键 toggle、lanes 视图、单大卡参数
5. 播放：PLAY/STOP、per-track loop 相位漂移在 hero 可见、mute、prob 生效（听觉）
6. USB out：MPC/电脑 DAW 收到正确字节流（对比 emu 的 out[] dump）

## 关键数字（进固件前的内存账）

- pattern RAM：14.3 KB（1 个编辑 pattern）——96 KiB RAM 充裕
- 引擎代码 <4 KB（host 编译 ~2 KB，pi32v2 相近）
- UI 屏绘：无 framebuffer 压力（直接 LCD 直绘，emu 已验证 240×240 预算全成立）

## 待 John 拍板（开工前最后一轮）

1. **GM 监听进不进 v1**？——USB-only 意味着真机上听不到声（除非接电脑/MPC）；
   +EAS = +2-3 天但"开箱能听"。建议：进（Polly 配方现成，且"草稿出声"是支柱 5）
2. **HOME 是否画 cursor 刻度**（上轮遗留：hero K4 PROB 盲拧）——1 行代码，建议顺手加
3. **USB ident**：建议 `FM-1_91001` 段（避开 Polly FM-1_3\d{6} / sloop FM-1_900 / bubba FM-1_003）
