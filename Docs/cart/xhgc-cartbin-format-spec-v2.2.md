# XHGC 卡带镜像（cart.bin）格式规范 v2.2（中文）

> 适用范围：本规范描述 `cart.bin` 的**固定 Header + 固定映射表（地址表）+ 若干数据段**的二进制布局。  
> 目标：让 **PC 打包器**与各端解析保持一致；并为后续扩展（MANF/INDEX/DATA/A8 等）预留空间。

---

## 1. 总览

`cart.bin` 是一份"卡带镜像"，由以下部分组成：

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

## 3. 文件整体布局

### 3.1 完整镜像布局

| 区段 | 起始偏移 | 大小 | 用途 |
|---|---:|---:|---|
| HEADER | `0x0000_0000` | 4096 | 元信息 + 映射表 + Header CRC |
| ICON（ARGB8888/BGRA bytes, 200×200） | `0x0000_1000` | 160000 | launcher 图标原始像素 |
| PADDING | after ICON | 到 4KB 对齐 | 补齐到下一段起点 |
| MANF | 4KB 对齐起点 | 变长 | manifest 二进制元数据（由 pack.json meta 生成） |
| PADDING | after MANF | 到 4KB 对齐 | 补齐 |
| INDEX | 4KB 对齐起点 | 变长 | 文件索引表（打包器自动生成） |
| PADDING | after INDEX | 到 4KB 对齐 | 补齐 |
| DATA | 4KB 对齐起点 | 变长 | 所有 LUA / RES 文件数据 |
| PADDING | after DATA | 到 4KB 对齐 | 文件末尾补齐 |

### 3.2 最小镜像（仅 ICON）

最小镜像只包含 Header + ICON，其余 slot 全 0，向后兼容：

| 区段 | 起始偏移 | 结束偏移 | 大小 |
|---|---:|---:|---:|
| HEADER | `0x0000_0000` | `0x0000_0FFF` | 4096 |
| ICON | `0x0000_1000` | `0x0002_80FF` | 160000 |
| PADDING | `0x0002_8100` | `…` | 到 4KB 对齐 |

### 3.3 对齐规则

- Header 长度固定：`4096` bytes。
- 各段 `offset` **建议（SHOULD）** 4KB 对齐（`4096` 对齐），便于裸写/扩展。
- 文件末尾 **建议（SHOULD）** pad 到 4KB 对齐。
- Padding 字节填 `0x00`。

---

## 4. Header（固定 4096 bytes）

### 4.1 Header 基本字段（固定偏移）

| 字段 | 偏移 | 类型 | 长度 | 说明 |
|---|---:|---|---:|---|
| `magic` | `0x0000` | bytes | 8 | 固定 `"XHGC_PAC"` |
| `header_version` | `0x0008` | u32 | 4 | 固定 `2` |
| `header_size` | `0x000C` | u32 | 4 | 固定 `4096` |
| `flags` | `0x0010` | u32 | 4 | 预留，当前填 0 |
| `cart_id` | `0x0014` | u64 | 8 | 卡带唯一 ID（对应 pack.json `meta.cart_id`，little-endian u64） |
| `title` | `0x001C` | char[64] | 64 | 主标题（UTF-8，对应 `meta.title`） |
| `title_zh` | `0x005C` | char[64] | 64 | 中文标题（UTF-8，对应 `meta.title_zh`，可为空） |
| `publisher` | `0x009C` | char[64] | 64 | 发行方/作者（对应 `meta.publisher`，可为空） |
| `version_str` | `0x00DC` | char[32] | 32 | 版本号字符串（对应 `meta.version`） |
| `entry` | `0x00FC` | char[128] | 128 | 入口脚本路径（对应 `meta.entry`，如 `app/main.lua`） |
| `min_fw` | `0x017C` | char[32] | 32 | 最低固件版本（对应 `meta.min_fw`，可为空） |
| `reserved` | `0x019C..0x0EFF` | bytes | - | 预留区，**必须填 0** |
| `addr_table` | `0x0F00..0x0FEF` | bytes | 240 | 固定地址表（映射表），15 个 16B 槽 |
| `addr_table_reserved` | `0x0FF0..0x0FFB` | bytes | 12 | 地址表后预留区，**必须填 0** |
| `header_crc32` | `0x0FFC..0x0FFF` | u32 | 4 | Header CRC32/IEEE（见第 6 节） |

---

## 5. 固定地址表（映射表 / Address Table）

### 5.1 位置与大小（固定）

- 地址表区域 **必须（MUST）** 固定：`0x0F00 .. 0x0FEF`（240 bytes）
- `0x0FF0 .. 0x0FFB` 为 Header 内预留区，当前必须填 `0x00`
- 槽位数：`240 / 16 = 15` 个槽（slot0..slot14）
- 每槽固定 16 bytes：

```c
// 16 bytes, little-endian
typedef struct __attribute__((packed)) {
  uint64_t offset;  // 段在镜像内的 byte offset（相对镜像起点）
  uint32_t size;    // 段长度（bytes）
  uint32_t crc32;   // 可选：该段 CRC32/IEEE；未启用填 0
} XhgcAddrSlot;
```

### 5.2 槽位定义（固定编号）

| slot | 表内偏移 | 名称 | 用途 |
|---:|---:|---|---|
| 0 | `0x0F00` | ICON | 200×200 ARGB8888 语义、BGRA 字节序图标（对应 pack.json `icon`） |
| 1 | `0x0F10` | THMB | 缩略图（可选） |
| 2 | `0x0F20` | MANF | manifest 二进制元数据（对应 pack.json `meta` 全量内容） |
| 3 | `0x0F30` | ENTRY | 入口脚本数据块（可选：若不走文件路径） |
| 4 | `0x0F40` | INDEX | 文件索引表（打包器自动生成，DATA 区的文件目录） |
| 5 | `0x0F50` | DATA | 数据区（LUA + RES 文件数据，对应 pack.json `chunks` 中 LUA/RES） |
| 6 | `0x0F60` | BNR | banner（可选） |
| 7 | `0x0F70` | COVR | cover（可选） |
| 8 | `0x0F80` | TITLE_A8 | 应用名 A8 mask（可选） |
| 9..13 | `0x0F90..0x0FD0` | RESV | 预留 |
| 14 | `0x0FE0` | IMAGE_CRC | 整镜像 CRC32（可选，对应 pack.json `hash.image_crc32`） |

### 5.3 未使用槽位填写规则

- 未使用的槽位：`offset = 0`，`size = 0`，`crc32 = 0`。
- 解析端：`size == 0` 表示该段不存在，应跳过。

---

## 6. CRC 规则

### 6.1 Header CRC（推荐启用）

- CRC 字段位置：Header 最后 4 bytes：`0x0FFC..0x0FFF`
- 算法：**CRC-32/IEEE**（等价 Python `zlib.crc32`）
  - Poly: `0x04C11DB7`
  - Init: `0xFFFFFFFF`
  - RefIn/RefOut: true
  - XorOut: `0xFFFFFFFF`

**计算规则（必须一致）：**

1. 计算时将 `0x0FFC..0x0FFF` 视为 `0x00000000`
2. 对整段 4096 bytes Header 计算 CRC32/IEEE
3. 将结果以 little-endian 写回 `0x0FFC..0x0FFF`

### 6.2 段 CRC（可选）

- 地址表每槽里的 `crc32` 字段可用于保存该段的 CRC32/IEEE。
- 当前未启用时 **必须（MUST）** 填 0。
- 对应 pack.json `hash.per_chunk_crc32 = true` 时打包器写入。

### 6.3 整镜像 CRC（可选）

- 对应 pack.json `hash.image_crc32 = true`。
- 写入 slot14 (IMAGE_CRC)：`offset = 0`，`size = cart.bin 文件大小`，`crc32 = 整镜像 CRC32/IEEE`。
- 未启用时 slot14 填 0。

---

## 7. 段格式定义

### 7.1 ICON（slot0）

- 来源：pack.json `icon` 字段，经打包器预处理后写入。
- 尺寸固定：`200 × 200`
- 像素语义：`ARGB8888`
- 存储顺序：row-major，从上到下、从左到右
- 每像素 4 bytes：`B, G, R, A`（little-endian ARGB8888 字节序，建议 A=0xFF 表示不透明）
- 大小固定：`200 × 200 × 4 = 160000` bytes

### 7.2 MANF（slot2）

- 来源：pack.json `chunks` 中 `type = "MANF"` 且 `source = "inline_meta"` 的条目，由打包器从 `meta` 字段自动生成。
- 格式：**自定义二进制格式**（带偏移表），解析端直接按 field_id 查表读取，无需 JSON 解析器。
- 大小：变长，由实际字段内容决定。
- 只写入有值的字段，空字段跳过。

> Header 里的 `title` / `entry` 等字段是为**快速读取**而设的定长副本，MANF 是完整结构化元数据，两者内容必须一致。

**结构体定义：**

```c
// MANF Header（固定 16 bytes）
typedef struct __attribute__((packed)) {
    uint32_t magic;        // 固定 0x464E414D ("MANF")
    uint32_t version;      // 固定 1
    uint32_t total_size;   // 整个 MANF 段字节数
    uint32_t field_count;  // 字段数量
} XhgcManfHeader;

// 偏移表条目（每条 8 bytes，紧跟 Header 后）
typedef struct __attribute__((packed)) {
    uint8_t  field_id;     // 字段 ID（见下表）
    uint8_t  reserved[3];  // 填 0
    uint32_t offset;       // 相对 MANF 段起点的字节偏移
} XhgcManfFieldEntry;

// 字段数据（变长，紧跟偏移表后）
typedef struct __attribute__((packed)) {
    uint16_t size;         // 数据长度 bytes
    // 紧跟 size bytes 的实际数据
} XhgcManfField;
```

**字段 ID 表：**

| field_id | 字段 | 类型 |
|---|---|---|
| `0x01` | title | UTF-8 字符串 |
| `0x02` | title_zh | UTF-8 字符串 |
| `0x03` | publisher | UTF-8 字符串 |
| `0x04` | version | UTF-8 字符串 |
| `0x05` | cart_id | u64 little-endian |
| `0x06` | entry | UTF-8 字符串 |
| `0x07` | min_fw | UTF-8 字符串 |
| `0x08` | id | UTF-8 字符串 |
| `0x09` | description_default | UTF-8 字符串 |
| `0x0A` | description_zh | UTF-8 字符串 |
| `0x0B` | category | UTF-8 字符串 |
| `0x0C` | tags | UTF-8 字符串，多个 tag 用 `\n` 分隔 |
| `0x0D` | author_name | UTF-8 字符串 |
| `0x0E` | author_contact | UTF-8 字符串 |

**内存布局：**

```
[ XhgcManfHeader (16B)              ]
[ XhgcManfFieldEntry #0 (8B)        ]
[ XhgcManfFieldEntry #1 (8B)        ]
...
[ XhgcManfFieldEntry #N-1 (8B)      ]
[ XhgcManfField #0: size(2B) + data ]
[ XhgcManfField #1: size(2B) + data ]
...
```

**实际验证示例（14 个字段，388 bytes）：**

```
magic       = 0x464E414D ("MANF")
version     = 1
total_size  = 388
field_count = 14

[0x01] title              = "Hatsune Miku"
[0x02] title_zh           = "演示游戏"
[0x03] publisher          = "Nixie Studio"
[0x04] version            = "0.1.0"
[0x05] cart_id            = 0x0123456789ABCDEF
[0x06] entry              = "app/main.lua"
[0x07] min_fw             = "0.8.0"
[0x08] id                 = "com.nixie.studio.hatsune-miku"
[0x09] description_default= "A demo game featuring Hatsune Miku"
[0x0A] description_zh     = "一个以初音未来为主题的演示游戏"
[0x0B] category           = "game"
[0x0C] tags               = "demo\ngame\nhatsune-miku"
[0x0D] author_name        = "Nixie Studio"
[0x0E] author_contact     = "contact@nixie.studio"
```

### 7.3 INDEX（slot4）

- 来源：打包器根据 DATA 区内容**自动生成**，pack.json 中无需声明。
- 用途：DATA 区的文件目录，解析端通过 INDEX 定位 DATA 内某个文件。
- INDEX 与 DATA 是**独立的段**，INDEX 仅存储目录信息，不包含文件数据本身。
- INDEX 中的 `data_off` 是**相对 DATA 段起点的偏移**，不是文件在镜像中的绝对偏移。

**XHGCIDX2 Header（32 bytes）：**

```c
typedef struct __attribute__((packed)) {
  char     magic[8];      // "XHGCIDX2"
  uint16_t version;       // 1
  uint16_t entry_size;    // 32
  uint32_t count;         // 文件条目数量
  uint32_t entries_off;   // entry 表相对 INDEX 段起点的偏移，当前为 32
  uint32_t strings_off;   // string table 相对 INDEX 段起点的偏移
  uint32_t strings_size;  // string table 字节数
  uint32_t flags;         // 0
} XhgcIndex2Header;
```

**XHGCIDX2 Entry（每条目 32 bytes）：**

```c
#define XHGC_RES_IMAGE       1
#define XHGC_RES_SCRIPT      2
#define XHGC_IMG_NONE        0
#define XHGC_IMG_BGRA8888    1

typedef struct __attribute__((packed)) {
  uint32_t path_hash;     // FNV-1a 32-bit，基于 cart 内相对路径
  uint32_t path_off;      // 路径字符串在 string table 内的偏移
  uint32_t data_off;      // 相对 DATA 段起点的字节偏移
  uint32_t size;          // 资源 blob 字节数
  uint32_t crc32;         // 资源 blob CRC32/IEEE
  uint8_t  type;          // XHGC_RES_*
  uint8_t  format;        // XHGC_IMG_*，非图片为 0
  uint16_t width;         // 图片宽度，非图片为 0
  uint16_t height;        // 图片高度，非图片为 0
  uint16_t flags;         // 0
  uint32_t reserved;      // 0
} XhgcIndex2Entry;
```

**INDEX 整体内存布局：**

```
[ XhgcIndex2Header (32B)       ]
[ XhgcIndex2Entry #0 (32B)     ]
[ XhgcIndex2Entry #1 (32B)     ]
...
[ XhgcIndex2Entry #N-1 (32B)   ]
[ string table: "path\0path\0..." ]
```

- 所有条目按包内路径**字典序升序**排列（与 pack.json `order = "lex"` 对应），支持二分查找。
- 路径字符串为 UTF-8 且以 `\0` 结尾；`path_off` 指向 string table 内对应字符串。
- `path_hash` 使用 FNV-1a 32-bit，对 cart 内相对路径的 UTF-8 字节计算。

**实际示例（来自验证数据）：**

```
INDEX Header:
  magic       = "XHGCIDX2"
  version     = 1
  entry_size  = 32
  count       = 2
  entries_off = 32

Entry[0]:
  type        = XHGC_RES_SCRIPT
  format      = XHGC_IMG_NONE
  data_off    = 0
  size        = app/main.lua 大小
  name        = "app/main.lua"

Entry[1]:
  type        = XHGC_RES_IMAGE
  format      = XHGC_IMG_BGRA8888
  width       = 200
  height      = 200
  size        = 160000
  name        = "assets/test.png"
```

**文件读取流程：**

```
1. 读 slot4(INDEX) → 得到 INDEX 段偏移
2. 遍历/二分查找 Entry，用 path_hash 和 string table 匹配目标路径
3. 读 slot5(DATA) → 得到 DATA 段偏移
4. 实际文件位置 = DATA段偏移 + entry.data_off
5. 读取 entry.size 字节；图片可直接使用 entry.format/width/height
```

### 7.4 DATA（slot5）

- 来源：pack.json `chunks` 中 `type = "LUA"` 和 `type = "RES"` 的条目，按 chunks 列表顺序、同一 chunk 内字典序写入。
- 格式：所有文件数据**连续拼接**，无额外 framing。
- 文件边界由 INDEX 条目的 `data_off` + `size` 定位，DATA 段本身无分隔符。
- 当前 XHGCIDX2 条目不包含压缩算法和解压后大小字段，因此 STM32 解析端应按未压缩文件读取。
- `compress = "lz4"` 为后续扩展预留；启用前必须扩展 INDEX 格式或另行提供每个文件的压缩元数据。
- RES 图片若在 pack.json 中启用 `image_format = "BGRA8888"`，DATA 中写入的是 raw `B,G,R,A` 像素字节；宽高和像素格式写在对应 XHGCIDX2 entry 中。
> 读取流程：INDEX 查找路径 → 得到 `data_off` / `size` → 从 DATA 段偏移读取。

### 7.5 TITLE_A8（slot8，可选）

- 高度固定：`20 px`
- 宽度不固定：`w`
- 像素格式：`A8`（每像素 1 byte alpha）
- 存储顺序：row-major
- 段大小：`size = w × 20`

> A8 仅提供 alpha 遮罩，颜色由渲染端在运行时指定（tint）。

---

## 8. Quick Reference（速查表）

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
- addr_table：`0x0F00`（240B，15 slots）
- addr_table_reserved：`0x0FF0..0x0FFB`（12B，填 0）
- header_crc32：`0x0FFC`（u32）

**地址表槽位：**

- slot_size：`0x10`（16B）
- slot0 (ICON)：`0x0F00`
- slot1 (THMB)：`0x0F10`
- slot2 (MANF)：`0x0F20`
- slot3 (ENTRY)：`0x0F30`
- slot4 (INDEX)：`0x0F40`
- slot5 (DATA)：`0x0F50`
- slot6 (BNR)：`0x0F60`
- slot7 (COVR)：`0x0F70`
- slot8 (TITLE_A8)：`0x0F80`

**pack.json → bin 对应关系：**

| pack.json | bin |
|---|---|
| `meta.*` | Header 定长字段 + slot2(MANF) 二进制元数据 |
| `icon` | slot0(ICON) 160000B ARGB8888 语义、BGRA 字节序 |
| `chunks[MANF]` | slot2(MANF) |
| `chunks[LUA]` / `chunks[RES]` | slot5(DATA)，目录在 slot4(INDEX) |

---

## 9. STM32 解析注意事项

- 不建议把文件缓冲区或 Flash 地址直接 cast 成 `struct *` 读取；应使用 `read_le16` / `read_le32` / `read_le64` 逐字节或 `memcpy` 后解析，避免未对齐访问问题。
- 每次使用 `offset + size` 前必须做越界检查，并确认加法没有整数溢出。
- Header 固定只读 4096B；地址表只解析 slot0..slot14，共 15 个槽。
- 对 `size == 0` 的 slot 必须跳过；对未知或预留 slot 不应报错。
- MANF 字符串字段不保证以 `\0` 结尾，解析端应按字段 `size` 复制，并在本地输出缓冲区末尾补 `\0`。
- XHGCIDX2 的路径名存放在 string table 中，路径字符串以 `\0` 结尾；解析端匹配路径时应使用 `path_off` 找到字符串，并可先用 `path_hash` 快速过滤。
- 当前 DATA 文件按未压缩读取；若后续启用压缩，必须先扩展 INDEX 元数据。

---

## 10. 版本记录

| 版本 | 日期 | 变更摘要 |
|---|---|---|
| v2.1 | - | 修订 ICON 段为 ARGB8888（200×200，160000B） |
| v2.2 | 2026-02-21 | 补全 slot2(MANF) / slot4(INDEX) / slot5(DATA) 格式定义；MANF 改为自定义二进制格式（带偏移表，field_id 查表，无需 JSON 解析器）；补充完整镜像布局表；新增 XHGCIDX2 INDEX 结构体定义及实际验证示例；明确 INDEX data_off 为相对 DATA 段起点的偏移；新增文件读取流程；新增 pack.json → bin 对应关系速查；更新槽位说明与 pack.json 字段对应 |
| v2.2-revA | 2026-06-06 | 修正文档与当前打包器实现不一致处：MANF 明确为二进制元数据；地址表修正为 `0x0F00..0x0FEF` 共 240B/15 slots；`0x0FF0..0x0FFB` 标为预留；`min_fw` 修正为 32B；DATA 压缩说明改为后续扩展；新增 STM32 解析注意事项。 |
