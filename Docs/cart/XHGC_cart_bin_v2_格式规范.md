# XHGC 卡带镜像（cart.bin）格式规范 v2.1（中文）

> 适用范围：本规范描述 `cart.bin` 的**固定 Header + 固定映射表（地址表）+ 若干数据段**的二进制布局。  
> 目标：让 **PC 打包器**与 **STM32 端解析/显示**保持一致；并为后续扩展（MANF/INDEX/DATA/A8 等）预留空间。

---

## 1. 总览

`cart.bin` 是一份“卡带镜像”，由以下部分组成：

1. **Header（固定 4096 bytes）**：元信息、映射表（地址表）、Header CRC。
2. **数据段（Segments）**：ICON / THMB / MANF / ENTRY / INDEX / DATA / …（通过映射表定位）。
3. **Padding（对齐填充）**：用于让后续段从 4KB 边界开始写入。

> 本规范默认偏移均为**相对镜像起点（文件起点）**的字节偏移（byte offset）。  
> 无论 `cart.bin` 放在 SD 文件系统还是 eMMC 裸区，解析逻辑一致。

---

## 2. 端序与编码

- 所有整数类型字段 **必须（MUST）** 为 **Little-Endian**。
- 字符串字段 **必须（MUST）** 为 **UTF-8**，并采用定长写法：
  - 以 `\0` 终止（如果不足定长）
  - 剩余字节填 `0x00`
  - 若字符串正好填满且无 `\0`，解析端按定长截断处理

---

## 3. 文件整体布局（当前“最小镜像”）

当前你已实现/使用的最小镜像为：

| 区段 | 起始偏移 | 结束偏移 | 大小 | 用途 |
|---|---:|---:|---:|---|
| HEADER | `0x0000_0000` | `0x0000_0FFF` | 4096 | 元信息 + 映射表 + Header CRC |
| ICON（ARGB8888, 200×200） | `0x0000_1000` | `0x0002_80FF` | 160000 | launcher 图标原始像素 |
| PADDING | `0x0002_8100` | `…` | 到 4KB 对齐 | 预留给后续段 |

### 3.1 对齐规则

- Header 长度固定：`4096` bytes。
- 各段 `offset` **建议（SHOULD）** 4KB 对齐（`4096` 对齐），便于裸写/扩展。
- 文件末尾 **建议（SHOULD）** pad 到 4KB 对齐。

---

## 4. Header（固定 4096 bytes）

### 4.1 Header 基本字段（固定偏移）

> 这些偏移已在你的实现中实际使用，建议冻结，避免打包器/固件端分叉。

| 字段 | 偏移 | 类型 | 长度 | 说明 |
|---|---:|---|---:|---|
| `magic` | `0x0000` | bytes | 8 | 固定 `"XHGC_PAC"` |
| `header_version` | `0x0008` | u32 | 4 | 固定 `2` |
| `header_size` | `0x000C` | u32 | 4 | 固定 `4096` |
| `flags` | `0x0010` | u32 | 4 | 预留，当前填 0 |
| `cart_id` | `0x0014` | u64 | 8 | 卡带唯一 ID（建议全局唯一） |
| `title` | `0x001C` | char[64] | 64 | 主标题（UTF-8） |
| `title_zh` | `0x005C` | char[64] | 64 | 中文标题（UTF-8，可为空） |
| `publisher` | `0x009C` | char[64] | 64 | 发行方/作者（可为空） |
| `version_str` | `0x00DC` | char[32] | 32 | 版本号字符串 |
| `entry` | `0x00FC` | char[128] | 128 | 入口脚本路径（如 `app/main.lua`） |
| `min_fw` | `0x017C` | char[32] | 32 | 最低固件版本（可为空） |
| `reserved` | `0x019C..0x0EFF` | bytes | - | 预留区，**必须填 0** |
| `addr_table` | `0x0F00..0x0FFB` | bytes | 252 | 固定地址表（映射表） |
| `header_crc32` | `0x0FFC..0x0FFF` | u32 | 4 | Header CRC32/IEEE（见第 6 节） |

---

## 5. 固定地址表（映射表 / Address Table）

### 5.1 位置与大小（固定）

- 地址表区域 **必须（MUST）** 固定：`0x0F00 .. 0x0FFB`（252 bytes）
- 槽位数：`252 / 16 = 15` 个槽（slot0..slot14）
- 每槽固定 16 bytes：

```c
// 16 bytes, little-endian
typedef struct __attribute__((packed)) {
  uint64_t offset;  // 段在镜像内的 byte offset（相对镜像起点）
  uint32_t size;    // 段长度（bytes）
  uint32_t crc32;   // 可选：该段 CRC32/IEEE；未启用填 0
} XhgcAddrSlot;
```

> 固件端建议用 `memcpy` 从 header 缓冲拷贝到结构体，避免 64-bit 非对齐访问坑。

### 5.2 槽位定义（固定编号）

| slot | 表内偏移 | 名称 | 用途 |
|---:|---:|---|---|
| 0 | `0x0F00` | ICON | 200×200 ARGB8888 图标 |
| 1 | `0x0F10` | THMB | 缩略图（可选） |
| 2 | `0x0F20` | MANF | manifest（JSON/CBOR，可选） |
| 3 | `0x0F30` | ENTRY | 入口脚本数据块（可选：若不走文件路径） |
| 4 | `0x0F40` | INDEX | 索引表（chunk entry/string table 等） |
| 5 | `0x0F50` | DATA | 数据区起点（chunk blobs） |
| 6 | `0x0F60` | BNR | banner（可选） |
| 7 | `0x0F70` | COVR | cover（可选） |
| 8 | `0x0F80` | TITLE_A8 | 应用名 A8 mask（可选） |
| 9..14 | `0x0F90..0x0FE0` | RESV | 预留 |

### 5.3 当前最小镜像的实际填写（示例）

最小镜像只包含 ICON：

- slot0(ICON)：
  - `offset = 4096`（`0x0000_1000`）
  - `size   = 160000`（`200*200*4`）
  - `crc32  = 0`（当前未启用段校验）
- slot1..slot14：全 0

---

## 6. CRC 规则（与 STM32 硬件 CRC 匹配）

### 6.1 Header CRC（推荐启用）

- CRC 字段位置：Header 最后 4 bytes：`0x0FFC..0x0FFF`
- 算法：**CRC-32/IEEE**（等价 Python `zlib.crc32`）
  - Poly: `0x04C11DB7`
  - Init: `0xFFFFFFFF`
  - RefIn/RefOut: true
  - XorOut: `0xFFFFFFFF`

**计算规则（必须一致）：**

1) 计算时将 `0x0FFC..0x0FFF` 视为 `0x00000000`  
2) 对整段 4096 bytes Header 计算 CRC32/IEEE  
3) 将结果以 little-endian 写回 `0x0FFC..0x0FFF`

### 6.2 段 CRC（可选）

- 地址表每槽里的 `crc32` 字段可用于保存该段的 CRC32/IEEE。
- 当前未启用时 **必须（MUST）** 填 0。

---

## 7. 段格式定义

### 7.1 ICON（slot0）

- 尺寸固定：`200 × 200`
- 像素格式：`ARGB8888`
- 存储顺序：
  - row-major：从上到下、从左到右
  - 每像素 4 bytes：`A, R, G, B`（建议 A=0xFF 表示不透明；也可存真实透明度）
- 大小固定：`200*200*4 = 160000` bytes

### 7.2 TITLE_A8（slot8，可选）

- 高度固定：`20 px`
- 宽度不固定：`w`
- 像素格式：`A8`（每像素 1 byte alpha）
- 存储顺序：row-major
- 段大小：`size = w * 20`

> A8 仅提供 alpha 遮罩，颜色由 STM32/LVGL 渲染时指定（tint）。

---

## 8. STM32 端解析建议（最省内存）

### 8.1 只读 title（你已实现）

- `f_open("0:/cart.bin")`
- `f_lseek(0x001C)` 读 64 bytes 到 `title[65]`，补 `\0`

### 8.2 读取 ICON（推荐逐行读）

- `f_lseek(0x0F00)` 读 16 bytes 得到 slot0（ICON）
- `f_lseek(icon_offset)`
- 按行读取 `200*4 = 800` bytes（共 200 行）
- 若 framebuffer/LTDC layer 使用 ARGB8888：可直接写到 framebuffer
- 否则：写入 ARGB8888 缓冲再交给 LVGL，或在写入时做像素格式转换

> 若 framebuffer 在 cacheable SDRAM，写完后需要 clean DCache（否则 LTDC 可能看不到更新）。

---

## 9. Quick Reference（速查表）

**Header（4096B）关键偏移：**

- magic：`0x0000`（8B）
- version：`0x0008`（u32）
- header_size：`0x000C`（u32）
- flags：`0x0010`（u32）
- cart_id：`0x0014`（u64）
- title：`0x001C`（64B）
- title_zh：`0x005C`（64B）
- publisher：`0x009C`（64B）
- version_str：`0x00DC`（32B）
- entry：`0x00FC`（128B）
- min_fw：`0x017C`（32B）
- addr_table：`0x0F00`（252B）
- header_crc32：`0x0FFC`（u32）

**地址表：**

- base：`0x0F00`
- slot_size：`0x10`
- slot0(ICON) = `0x0F00`
- slot8(TITLE_A8) = `0x0F80`

---

## 10. 版本记录

- v2.1：在 v2 基础上修订 ICON 段为 ARGB8888（200×200，160000B），其余布局不变。
