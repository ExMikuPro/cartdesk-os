#include "uid.h"

/* 你可以用 HAL，也可以不用。这里优先用 HAL 的宏函数（更稳更通用） */
#include "stm32h7xx_hal.h"
#include <string.h>

/* ================== 基础 UID ================== */

bsp_uid96_t BSP_UID_Read96(void)
{
    bsp_uid96_t id;
    id.w0 = HAL_GetUIDw0();
    id.w1 = HAL_GetUIDw1();
    id.w2 = HAL_GetUIDw2();
    return id;
}

void BSP_UID_ReadBytes(uint8_t out12[12])
{
    if (!out12) return;
    bsp_uid96_t id = BSP_UID_Read96();
    /* 按小端内存布局拷贝 */
    memcpy(&out12[0],  &id.w0, 4);
    memcpy(&out12[4],  &id.w1, 4);
    memcpy(&out12[8],  &id.w2, 4);
}

void BSP_UID_ToHex(char out25[25])
{
    if (!out25) return;
    bsp_uid96_t id = BSP_UID_Read96();

    static const char HEX[] = "0123456789ABCDEF";

    /* 拼成 24 hex：w0 w1 w2（每个 8 hex） */
    uint32_t w[3] = { id.w0, id.w1, id.w2 };
    int pos = 0;
    for (int k = 0; k < 3; k++) {
        for (int i = 7; i >= 0; i--) {
            uint8_t nib = (uint8_t)((w[k] >> (i * 4)) & 0xFu);
            out25[pos++] = HEX[nib];
        }
    }
    out25[pos] = '\0';
}

/* ================== DEVID / REVID ================== */

uint32_t BSP_Chip_GetDEVID(void)
{
    return HAL_GetDEVID();
}

uint32_t BSP_Chip_GetREVID(void)
{
    return HAL_GetREVID();
}

/* ================== Hash: FNV-1a 32 ================== */

uint32_t BSP_Hash_FNV1a32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* ================== CRC32（IEEE 802.3 poly 0x04C11DB7 reflected 0xEDB88320） ================== */

static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t m = (uint32_t)-(int32_t)(c & 1u);
            c = (c >> 1) ^ (0xEDB88320u & m);
        }
    }
    return ~c;
}

/* ================== Base32（Crockford，去掉 I/L/O/U，适合做人类可读 ID） ================== */

static void base32_crockford_u64(char *out, size_t out_len, uint64_t v)
{
    /* Crockford alphabet */
    static const char A[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    /* out_len 包含 '\0'，至少需要 2 */
    if (!out || out_len < 2) return;

    /* 生成不定长，再左侧补 0 到固定长度（便于对齐） */
    char tmp[32];
    size_t n = 0;
    do {
        tmp[n++] = A[(uint8_t)(v & 31u)];
        v >>= 5;
    } while (v && n < sizeof(tmp));

    /* 固定输出长度：out_len-1 个字符 */
    size_t want = out_len - 1;
    for (size_t i = 0; i < want; i++) {
        size_t src = (i < n) ? (n - 1 - i) : (size_t)-1;
        out[i] = (src != (size_t)-1) ? tmp[src] : '0';
    }
    out[want] = '\0';
}

/* ================== Short ID：UID + (devid,revid) + salt -> CRC32 + pack -> Base32 ================== */

void BSP_UID_MakeShortID_Base32(char out14[14], uint32_t salt)
{
    if (!out14) return;

    uint8_t uid12[12];
    BSP_UID_ReadBytes(uid12);

    uint32_t devid = BSP_Chip_GetDEVID();
    uint32_t revid = BSP_Chip_GetREVID();

    /* 组成输入：UID(12) + devid(4) + revid(4) + salt(4) => 24 bytes */
    uint8_t buf[24];
    memcpy(&buf[0],  uid12, 12);
    memcpy(&buf[12], &devid, 4);
    memcpy(&buf[16], &revid, 4);
    memcpy(&buf[20], &salt,  4);

    uint32_t c = crc32_ieee(buf, sizeof(buf));

    /* 把信息打包成 64bit：高 32 = crc，低 16 = devid低16，低 16 = revid低16（够区分批次） */
    uint64_t pack = ((uint64_t)c << 32)
                  | ((uint64_t)(devid & 0xFFFFu) << 16)
                  |  (uint64_t)(revid & 0xFFFFu);

    /* 13 位 Base32 + '\0' => out14 */
    base32_crockford_u64(out14, 14, pack);
}
