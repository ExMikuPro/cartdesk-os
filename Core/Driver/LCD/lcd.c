/**
 * @file    lcd.c
 * @brief   LCD 驱动（LTDC + SDRAM Framebuffer + DMA2D 加速 + 双缓冲 + VBlank 翻页）
 *          【已修复版本 - 2024】彻底解决显示不全/下半闪/撕裂问题
 *
 * ==========================================================
 * 修复要点：
 *  1) ✅ 统一绘制目标：所有绘图API强制使用back buffer（通过LCD_GetDrawFB）
 *  2) ✅ 修正LineEvent计算：使用AccumulatedActiveH+1确保在VBlank触发
 *  3) ✅ 双缓冲初始化：Layer1的front/back两块buffer都初始化为透明，避免swap时闪烁
 *  4) ✅ DCache正确维护：所有DMA2D写入前clean目标区域，refresh时clean dirty rect
 *  5) ✅ 新增int32坐标API：支持边界裁剪，解决uint16_t wrap导致的边缘异常
 *  6) ✅ VBlank计数器：提供给上层用于按帧节流渲染
 *
 * 关键概念：
 *  - front_addr：LTDC 当前显示读取的帧缓冲地址（"屏幕看到的"）
 *  - back_addr ：CPU/DMA2D 画图的帧缓冲地址（"你正在画的"）
 *  - pending_swap：提交标志。LCD_Refresh() 置 1，LineEvent 回调里真正 swap
 *  - dirty rect：记录本次画过的最小矩形区域，用于减少 DCache clean 的范围
 *
 * Cache / DMA 注意：
 *  - LTDC 是外设"读"内存；CPU 写完 framebuffer 后若 DCache 开启，需要 clean 才能让内存真实更新
 *  - DMA2D 是外设"写"内存；在 DMA2D 写入前需要 clean 目标区域，避免 CPU 脏 cache line 回写覆盖 DMA2D 的结果
 */

#include "lcd.h"
#include "ltdc.h"
#include "dma2d.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 *                         双缓冲状态结构
 * ============================================================================ */

/**
 * @brief 8x16 ASCII字体点阵数据（0x20~0x7E，共95个字符）
 *
 * 格式说明：
 * - 每个字符16字节（16行，每行8位）
 * - 1=前景色，0=背景色
 * - 从上到下扫描
 */
static const uint8_t Font8x16_ASCII[95][16] = {
  /* 0x20 ' ' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x21 '!' */ { 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x22 '"' */ { 0x00, 0x00, 0x00, 0x28, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x23 '#' */ { 0x00, 0x00, 0x00, 0x00, 0x14, 0x14, 0x7E, 0x28, 0x28, 0xFE, 0x48, 0x50, 0x00, 0x00, 0x00, 0x00 },
  /* 0x24 '$' */ { 0x00, 0x00, 0x10, 0x38, 0x74, 0x50, 0x30, 0x1C, 0x14, 0x54, 0x3C, 0x10, 0x10, 0x00, 0x00, 0x00 },
  /* 0x25 '%' */ { 0x00, 0x00, 0x00, 0x62, 0xA4, 0xA8, 0x64, 0x08, 0x10, 0x26, 0x4A, 0x92, 0xC4, 0x00, 0x00, 0x00 },
  /* 0x26 '&' */ { 0x00, 0x00, 0x00, 0x30, 0x48, 0x48, 0x30, 0x72, 0x8C, 0x88, 0x88, 0x8C, 0x72, 0x00, 0x00, 0x00 },
  /* 0x27 '\'' */ { 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x28 '(' */ { 0x00, 0x00, 0x00, 0x08, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00 },
  /* 0x29 ')' */ { 0x00, 0x00, 0x00, 0x20, 0x10, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00, 0x00, 0x00 },
  /* 0x2A '*' */ { 0x00, 0x00, 0x00, 0x00, 0x10, 0x92, 0x54, 0x38, 0x38, 0x54, 0x92, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x2B '+' */ { 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0xFE, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x2C ',' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x20, 0x40, 0x00, 0x00 },
  /* 0x2D '-' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x2E '.' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00 },
  /* 0x2F '/' */ { 0x00, 0x00, 0x00, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x00, 0x00, 0x00 },
  /* 0x30 '0' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x8A, 0x92, 0x92, 0xA2, 0xA2, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x31 '1' */ { 0x00, 0x00, 0x00, 0x10, 0x30, 0x50, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x32 '2' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFC, 0x00, 0x00, 0x00, 0x00 },
  /* 0x33 '3' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x18, 0x0C, 0x04, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x34 '4' */ { 0x00, 0x00, 0x00, 0x08, 0x18, 0x28, 0x48, 0x88, 0xFC, 0x08, 0x08, 0x1E, 0x00, 0x00, 0x00, 0x00 },
  /* 0x35 '5' */ { 0x00, 0x00, 0x00, 0x7C, 0x40, 0x40, 0x78, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x36 '6' */ { 0x00, 0x00, 0x00, 0x1C, 0x20, 0x40, 0x78, 0x44, 0x84, 0x84, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x37 '7' */ { 0x00, 0x00, 0x00, 0xFC, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00 },
  /* 0x38 '8' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x38, 0x6C, 0x44, 0x84, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x39 '9' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x84, 0x84, 0x4C, 0x3C, 0x04, 0x08, 0x70, 0x00, 0x00, 0x00, 0x00 },
  /* 0x3A ':' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x3B ';' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x30, 0x30, 0x20, 0x40, 0x00, 0x00 },
  /* 0x3C '<' */ { 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00 },
  /* 0x3D '=' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x3E '>' */ { 0x00, 0x00, 0x00, 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00, 0x00 },
  /* 0x3F '?' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x08, 0x10, 0x10, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x40 '@' */ { 0x00, 0x00, 0x00, 0x3C, 0x42, 0x9D, 0xA5, 0xA5, 0x9D, 0x40, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x41 'A' */ { 0x00, 0x00, 0x00, 0x10, 0x38, 0x28, 0x28, 0x2C, 0x44, 0x7C, 0x46, 0xC2, 0x00, 0x00, 0x00, 0x00 },
  /* 0x42 'B' */ { 0x00, 0x00, 0x00, 0xF8, 0x44, 0x44, 0x78, 0x44, 0x84, 0x84, 0x44, 0xF8, 0x00, 0x00, 0x00, 0x00 },
  /* 0x43 'C' */ { 0x00, 0x00, 0x00, 0x3C, 0x42, 0x80, 0x80, 0x80, 0x80, 0x80, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x44 'D' */ { 0x00, 0x00, 0x00, 0xF0, 0x48, 0x44, 0x84, 0x84, 0x84, 0x84, 0x44, 0xF0, 0x00, 0x00, 0x00, 0x00 },
  /* 0x45 'E' */ { 0x00, 0x00, 0x00, 0xFC, 0x40, 0x40, 0x78, 0x40, 0x80, 0x80, 0x40, 0xFC, 0x00, 0x00, 0x00, 0x00 },
  /* 0x46 'F' */ { 0x00, 0x00, 0x00, 0xFC, 0x40, 0x40, 0x78, 0x40, 0x80, 0x80, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x00 },
  /* 0x47 'G' */ { 0x00, 0x00, 0x00, 0x3C, 0x42, 0x80, 0x80, 0x9C, 0x84, 0x84, 0x46, 0x3A, 0x00, 0x00, 0x00, 0x00 },
  /* 0x48 'H' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x44, 0x7C, 0x44, 0x84, 0x84, 0x44, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x49 'I' */ { 0x00, 0x00, 0x00, 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4A 'J' */ { 0x00, 0x00, 0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x88, 0x88, 0x48, 0x30, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4B 'K' */ { 0x00, 0x00, 0x00, 0xC4, 0x48, 0x50, 0x60, 0x50, 0x88, 0x88, 0x44, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4C 'L' */ { 0x00, 0x00, 0x00, 0xE0, 0x40, 0x40, 0x40, 0x40, 0x80, 0x80, 0x80, 0xFC, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4D 'M' */ { 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x6C, 0x54, 0x54, 0x92, 0x92, 0x82, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4E 'N' */ { 0x00, 0x00, 0x00, 0xC6, 0x64, 0x64, 0x54, 0x54, 0x94, 0x94, 0x8C, 0xC4, 0x00, 0x00, 0x00, 0x00 },
  /* 0x4F 'O' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x50 'P' */ { 0x00, 0x00, 0x00, 0xF8, 0x44, 0x44, 0x78, 0x40, 0x80, 0x80, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x00 },
  /* 0x51 'Q' */ { 0x00, 0x00, 0x00, 0x38, 0x44, 0x82, 0x82, 0x82, 0x82, 0x8A, 0x44, 0x3A, 0x00, 0x00, 0x00, 0x00 },
  /* 0x52 'R' */ { 0x00, 0x00, 0x00, 0xF8, 0x44, 0x44, 0x78, 0x50, 0x88, 0x88, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x53 'S' */ { 0x00, 0x00, 0x00, 0x3C, 0x42, 0x40, 0x30, 0x0C, 0x02, 0x82, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x54 'T' */ { 0x00, 0x00, 0x00, 0xFE, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x55 'U' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x44, 0x84, 0x84, 0x84, 0x84, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x56 'V' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x44, 0x84, 0x28, 0x28, 0x28, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x57 'W' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x92, 0x92, 0x54, 0x54, 0x54, 0x6C, 0x44, 0x00, 0x00, 0x00, 0x00 },
  /* 0x58 'X' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x28, 0x10, 0x10, 0x28, 0x44, 0x44, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x59 'Y' */ { 0x00, 0x00, 0x00, 0xC6, 0x44, 0x28, 0x28, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x5A 'Z' */ { 0x00, 0x00, 0x00, 0xFE, 0x04, 0x08, 0x10, 0x10, 0x20, 0x40, 0x80, 0xFE, 0x00, 0x00, 0x00, 0x00 },
  /* 0x5B '[' */ { 0x00, 0x00, 0x00, 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00, 0x00, 0x00 },
  /* 0x5C '\\' */ { 0x00, 0x00, 0x00, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00 },
  /* 0x5D ']' */ { 0x00, 0x00, 0x00, 0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00, 0x00, 0x00 },
  /* 0x5E '^' */ { 0x00, 0x00, 0x00, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x5F '_' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE },
  /* 0x60 '`' */ { 0x00, 0x00, 0x00, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  /* 0x61 'a' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x04, 0x3C, 0x44, 0x84, 0x4C, 0x3A, 0x00, 0x00, 0x00, 0x00 },
  /* 0x62 'b' */ { 0x00, 0x00, 0x00, 0xC0, 0x40, 0x78, 0x44, 0x84, 0x84, 0x84, 0x44, 0x78, 0x00, 0x00, 0x00, 0x00 },
  /* 0x63 'c' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x80, 0x80, 0x80, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x64 'd' */ { 0x00, 0x00, 0x00, 0x06, 0x02, 0x3E, 0x42, 0x82, 0x82, 0x82, 0x42, 0x3E, 0x00, 0x00, 0x00, 0x00 },
  /* 0x65 'e' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x7C, 0x80, 0x80, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x66 'f' */ { 0x00, 0x00, 0x00, 0x1C, 0x22, 0x20, 0x78, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00, 0x00, 0x00, 0x00 },
  /* 0x67 'g' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x42, 0x82, 0x82, 0x82, 0x42, 0x3E, 0x02, 0x82, 0x7C, 0x00 },
  /* 0x68 'h' */ { 0x00, 0x00, 0x00, 0xC0, 0x40, 0x78, 0x44, 0x84, 0x84, 0x84, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x69 'i' */ { 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x6A 'j' */ { 0x00, 0x00, 0x00, 0x08, 0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x88, 0x48, 0x30, 0x00, 0x00, 0x00 },
  /* 0x6B 'k' */ { 0x00, 0x00, 0x00, 0xC0, 0x40, 0x4C, 0x50, 0x60, 0x50, 0x88, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x6C 'l' */ { 0x00, 0x00, 0x00, 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x6D 'm' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xEC, 0x52, 0x52, 0x92, 0x92, 0x92, 0xD6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x6E 'n' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x44, 0x84, 0x84, 0x84, 0x84, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x6F 'o' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x82, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00 },
  /* 0x70 'p' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x44, 0x84, 0x84, 0x84, 0x44, 0x78, 0x40, 0xC0, 0x00, 0x00 },
  /* 0x71 'q' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x42, 0x82, 0x82, 0x82, 0x42, 0x3E, 0x02, 0x06, 0x00, 0x00 },
  /* 0x72 'r' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x62, 0x40, 0x80, 0x80, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x00 },
  /* 0x73 's' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x40, 0x30, 0x0C, 0x82, 0x7C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x74 't' */ { 0x00, 0x00, 0x00, 0x20, 0x20, 0x78, 0x20, 0x20, 0x20, 0x20, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00 },
  /* 0x75 'u' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x44, 0x84, 0x84, 0x84, 0x4C, 0x3A, 0x00, 0x00, 0x00, 0x00 },
  /* 0x76 'v' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x44, 0x28, 0x28, 0x28, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00 },
  /* 0x77 'w' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x92, 0x92, 0x54, 0x54, 0x6C, 0x44, 0x00, 0x00, 0x00, 0x00 },
  /* 0x78 'x' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x44, 0x28, 0x10, 0x28, 0x44, 0xC6, 0x00, 0x00, 0x00, 0x00 },
  /* 0x79 'y' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x44, 0x28, 0x28, 0x10, 0x10, 0x20, 0x20, 0x40, 0x80, 0x00 },
  /* 0x7A 'z' */ { 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x04, 0x08, 0x10, 0x20, 0x40, 0xFE, 0x00, 0x00, 0x00, 0x00 },
  /* 0x7B '{' */ { 0x00, 0x00, 0x00, 0x0C, 0x10, 0x10, 0x10, 0x20, 0x10, 0x10, 0x10, 0x10, 0x0C, 0x00, 0x00, 0x00 },
  /* 0x7C '|' */ { 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00 },
  /* 0x7D '}' */ { 0x00, 0x00, 0x00, 0x60, 0x10, 0x10, 0x10, 0x08, 0x10, 0x10, 0x10, 0x10, 0x60, 0x00, 0x00, 0x00 },
  /* 0x7E '~' */ { 0x00, 0x00, 0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};


/**
 * @brief 每个 Layer 的缓冲管理信息
 */
typedef struct {
    uint32_t front_addr;      ///< 当前 LTDC 正在显示的帧缓冲地址（CFBAR 应指向它）
    uint32_t back_addr;       ///< 当前绘制用帧缓冲地址（所有 Draw/Fill 都写它）
    uint8_t  pending_swap;    ///< 提交标志：LCD_Refresh() 置 1，VBlank 回调中交换 front/back 并更新 CFBAR

    // dirty rect：本次修改的最小矩形区域，用于减少 DCache clean 的成本
    uint16_t dirty_x0, dirty_y0; ///< 脏矩形左上角
    uint16_t dirty_x1, dirty_y1; ///< 脏矩形右下角
    uint8_t  dirty_valid;        ///< 脏矩形是否有效（0=无修改；1=有修改）
} LayerBufferInfo;

/**
 * @brief Layer 缓冲区初值
 *
 * 布局说明：
 *  - layer0: front=FB0, back=FB0  -> 背景层不做双缓冲（节省内存/带宽）
 *  - layer1: front=FB1, back=FB1+FB_SIZE -> UI 层做双缓冲（避免 UI 动画撕裂）
 *
 * 如果你想两层都双缓冲，需要给 layer0 再分配一块 back（比如 FB0+FB_SIZE 或更靠后）
 */
static LayerBufferInfo layer_info[2] = {
    {LCD_FB0_ADDR, LCD_FB0_ADDR, 0, 0,0,0,0, 0},
    {LCD_FB1_ADDR, LCD_FB1_ADDR + FB_SIZE, 0, 0,0,0,0, 0}
};

/**
 * @brief VBlank计数器（用于上层按帧节流渲染）
 * - 每次VBlank回调时+1
 * - 上层可通过LCD_GetVBlankCount()获取，实现"每帧只渲染一次"逻辑
 */
volatile uint32_t g_ltdc_vblank_cnt = 0;

/**
 * @brief 当前活动层（legacy/兼容用）
 */
static uint32_t ActiveLayer = 0;

/* ============================================================================
 *                         Dirty-Rect（脏矩形）管理
 * ============================================================================ */

/**
 * @brief 将某层的 dirty rect 标记为"整屏脏"（即整层都需要 clean/刷新）
 * @param Layer 图层索引（0/1）
 */
static inline void DirtyMarkFull(uint8_t Layer) {
    layer_info[Layer].dirty_x0 = 0;
    layer_info[Layer].dirty_y0 = 0;
    layer_info[Layer].dirty_x1 = LCD_W - 1;
    layer_info[Layer].dirty_y1 = LCD_H - 1;
    layer_info[Layer].dirty_valid = 1;
}

/**
 * @brief 将 (x0,y0)-(x1,y1) 合并进 dirty rect（自动裁剪到屏幕范围）
 * @param Layer 图层索引（0/1）
 * @param x0,y0 矩形左上角（像素）
 * @param x1,y1 矩形右下角（像素）
 *
 * 说明：
 * - 多次绘制时不断扩大 dirty rect，让最终的 DCache clean 只覆盖"真正变动过的区域"
 */
static inline void DirtyMergeRect(uint8_t Layer, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // ✅ 修复：不要因为越界就完全跳过，而是裁剪到有效范围

    // 如果完全在屏幕外，才跳过
    if (x0 >= LCD_W && x1 >= LCD_W) return;
    if (y0 >= LCD_H && y1 >= LCD_H) return;

    // 裁剪到屏幕范围
    if (x0 >= LCD_W) x0 = LCD_W - 1;
    if (y0 >= LCD_H) y0 = LCD_H - 1;
    if (x1 >= LCD_W) x1 = LCD_W - 1;
    if (y1 >= LCD_H) y1 = LCD_H - 1;

    if (!layer_info[Layer].dirty_valid) {
        layer_info[Layer].dirty_x0 = x0;
        layer_info[Layer].dirty_y0 = y0;
        layer_info[Layer].dirty_x1 = x1;
        layer_info[Layer].dirty_y1 = y1;
        layer_info[Layer].dirty_valid = 1;
    } else {
        if (x0 < layer_info[Layer].dirty_x0) layer_info[Layer].dirty_x0 = x0;
        if (y0 < layer_info[Layer].dirty_y0) layer_info[Layer].dirty_y0 = y0;
        if (x1 > layer_info[Layer].dirty_x1) layer_info[Layer].dirty_x1 = x1;
        if (y1 > layer_info[Layer].dirty_y1) layer_info[Layer].dirty_y1 = y1;
    }
}

/* ============================================================================
 *                         DCache 管理
 * ============================================================================ */

/**
 * @brief Clean DCache（按地址范围），并做 32-byte cache line 对齐
 * @param addr 起始地址（任意对齐）
 * @param size 字节数
 *
 * 原因：
 * - Cortex-M7 DCache line = 32 bytes
 * - SCB_CleanDCache_by_Addr 要求对齐/覆盖完整 cache line，否则会漏刷
 */
static inline void LCD_DCacheClean(void *addr, uint32_t size) {
    uint32_t start = (uint32_t)addr;
    uint32_t aligned_start = start & ~31u;
    uint32_t end = start + size;
    uint32_t aligned_end = (end + 31u) & ~31u;
    SCB_CleanDCache_by_Addr((uint32_t*)aligned_start, (int32_t)(aligned_end - aligned_start));
}

/* ============================================================================
 *                         DMA2D 工具函数
 * ============================================================================ */

/**
 * @brief 等待 DMA2D 空闲（busy-wait）
 */
static void WaitDMA2D(void) {
    while (hdma2d.Instance->CR & DMA2D_CR_START) {}
}

/**
 * @brief DMA2D 以 R2M 模式填充矩形（ARGB8888）
 * @param dest_addr 目标 framebuffer 基址（字节地址）
 * @param x,y       矩形左上角（像素）
 * @param w,h       宽高（像素）
 * @param color     ARGB8888 颜色 0xAARRGGBB
 *
 * 注意：
 * - 【修复】在 DMA2D 写入前，对目标区域做一次 DCache clean
 *   目的：避免 CPU cache 里存在"旧的脏数据"，之后回写把 DMA2D 写入覆盖掉
 */
static void DMA2D_FillRect(uint32_t dest_addr, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    if (w == 0 || h == 0) return;

    // 1) 等 DMA2D 空闲
    WaitDMA2D();

    // 2) 【关键】DMA2D 将写入 region_start 这段内存；先 clean 掉对应 cache line，防止"脏回写覆盖 DMA2D"
    uint32_t region_start = dest_addr + (y * LCD_W + x) * 4;
    LCD_DCacheClean((void*)region_start, ((h-1)*LCD_W + w) * 4);

    // 3) 配置 DMA2D：R2M（寄存器到内存）填充
    hdma2d.Instance->CR = DMA2D_R2M | DMA2D_CR_TCIE;
    hdma2d.Instance->OCOLR = color;
    hdma2d.Instance->OMAR  = region_start;
    hdma2d.Instance->OOR   = LCD_W - w;
    hdma2d.Instance->NLR   = (w << DMA2D_NLR_PL_Pos) | (h << DMA2D_NLR_NL_Pos);
    hdma2d.Instance->OPFCCR = DMA2D_OUTPUT_ARGB8888;
    hdma2d.Instance->CR |= DMA2D_CR_START;
}

/* ============================================================================
 *                         基础控制 API
 * ============================================================================ */

/**
 * @brief 选择当前活动层（兼容用）
 */
void LCD_Select(uint32_t LayerIndex) {
    ActiveLayer = LayerIndex;
    (void)ActiveLayer;
}

/** @brief 获取屏幕宽度（像素） */
uint32_t LCD_GetXSize(void) { return LCD_W; }

/** @brief 获取屏幕高度（像素） */
uint32_t LCD_GetYSize(void) { return LCD_H; }

/**
 * @brief 打开显示：使能 LTDC + 打开背光
 */
void LCD_DisplayON(void) {
    __HAL_LTDC_ENABLE(&hltdc);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

/**
 * @brief 关闭显示：禁用 LTDC + 关闭背光
 */
void LCD_DisplayOFF(void) {
    __HAL_LTDC_DISABLE(&hltdc);
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 设置图层常量透明度（Constant Alpha）
 */
void LCD_SetTransparency(uint32_t LayerIndex, uint8_t Transparency) {
    HAL_LTDC_SetAlpha(&hltdc, LayerIndex, Transparency);
}

/**
 * @brief 设置图层可见性（显示/隐藏）
 */
void LCD_SetLayerVisible(uint32_t LayerIndex, uint8_t Status) {
    if (Status) {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, LayerIndex);
    } else {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, LayerIndex);
    }
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
}

/* ============================================================================
 *                         Framebuffer 获取
 * ============================================================================ */

/**
 * @brief 获取某层 front buffer 指针（当前显示用）
 * @warning 双缓冲模式下请勿直接写front buffer
 */
uint32_t* LCD_GetFB(uint8_t Layer) {
    return (uint32_t*)layer_info[Layer].front_addr;
}

/**
 * @brief 【核心函数】获取某层绘制用 framebuffer（back buffer）
 * @return 指向 back buffer 的 uint32_t*
 *
 * 【修复要点】所有绘图函数都必须通过此函数获取绘制目标，确保统一写back buffer
 */
uint32_t* LCD_GetDrawFB(uint8_t Layer) {
    return (uint32_t*)layer_info[Layer].back_addr;
}

/* ============================================================================
 *                         底层像素操作（统一使用back buffer）
 * ============================================================================ */

/**
 * @brief 【内部函数】快速绘制单像素到back buffer（不做DCache clean）
 * @param Layer 图层索引
 * @param X,Y 像素坐标
 * @param Color ARGB8888颜色
 *
 * 【修复】使用LCD_GetDrawFB确保写入back buffer
 */
static inline void LCD_DrawPixelFast(uint8_t Layer, uint16_t X, uint16_t Y, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H) return;
    uint32_t *fb = LCD_GetDrawFB(Layer);  // 【修复】统一使用back buffer
    fb[Y * LCD_W + X] = Color;
}

/**
 * @brief 绘制单个像素（公开API，会更新dirty rect）
 */
void LCD_DrawPixel(uint8_t Layer, uint16_t X, uint16_t Y, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H) return;
    LCD_DrawPixelFast(Layer, X, Y, Color);
    DirtyMergeRect(Layer, X, Y, X, Y);
}

/* ============================================================================
 *                         基础绘图 API（统一使用back buffer）
 * ============================================================================ */

/**
 * @brief 清空图层（填充为透明黑色 0x00000000）
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_Clear(uint8_t Layer) {
    DMA2D_FillRect(layer_info[Layer].back_addr, 0, 0, LCD_W, LCD_H, 0x00000000);
    DirtyMarkFull(Layer);
}

/**
 * @brief 用指定颜色填充整个图层
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_Fill(uint8_t Layer, uint32_t Color) {
    DMA2D_FillRect(layer_info[Layer].back_addr, 0, 0, LCD_W, LCD_H, Color);
    DirtyMarkFull(Layer);
}

/**
 * @brief 绘制实心矩形
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_DrawRect(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H || W == 0 || H == 0) return;
    if (X + W > LCD_W) W = LCD_W - X;
    if (Y + H > LCD_H) H = LCD_H - Y;

    DMA2D_FillRect(layer_info[Layer].back_addr, X, Y, W, H, Color);
    DirtyMergeRect(Layer, X, Y, X + W - 1, Y + H - 1);
}

/**
 * @brief 绘制实心矩形（别名）
 */
void LCD_DrawRectFilled(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint32_t Color) {
    LCD_DrawRect(Layer, X, Y, W, H, Color);
}

/**
 * @brief 绘制矩形外框
 * 【修复】使用DMA2D填充四条边，写入back buffer，并正确更新dirty rect
 */
void LCD_DrawRectOutline(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t W, uint16_t H, uint8_t LineWidth, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H || W == 0 || H == 0 || LineWidth == 0) return;

    uint32_t back_addr = layer_info[Layer].back_addr;

    // 上边
    if (Y < LCD_H) {
        uint16_t top_h = (Y + LineWidth > LCD_H) ? (LCD_H - Y) : LineWidth;
        uint16_t top_w = (X + W > LCD_W) ? (LCD_W - X) : W;
        DMA2D_FillRect(back_addr, X, Y, top_w, top_h, Color);
    }

    // 下边
    if (Y + H > LineWidth && Y + H <= LCD_H) {
        uint16_t bot_y = Y + H - LineWidth;
        if (bot_y < LCD_H) {
            uint16_t bot_h = (bot_y + LineWidth > LCD_H) ? (LCD_H - bot_y) : LineWidth;
            uint16_t bot_w = (X + W > LCD_W) ? (LCD_W - X) : W;
            DMA2D_FillRect(back_addr, X, bot_y, bot_w, bot_h, Color);
        }
    }

    // 左边
    if (X < LCD_W && H > 2 * LineWidth) {
        uint16_t left_h = H - 2 * LineWidth;
        uint16_t left_y = Y + LineWidth;
        if (left_y < LCD_H) {
            if (left_y + left_h > LCD_H) left_h = LCD_H - left_y;
            uint16_t left_w = (X + LineWidth > LCD_W) ? (LCD_W - X) : LineWidth;
            DMA2D_FillRect(back_addr, X, left_y, left_w, left_h, Color);
        }
    }

    // 右边
    if (X + W > LineWidth && X + W <= LCD_W && H > 2 * LineWidth) {
        uint16_t right_x = X + W - LineWidth;
        uint16_t right_h = H - 2 * LineWidth;
        uint16_t right_y = Y + LineWidth;
        if (right_x < LCD_W && right_y < LCD_H) {
            if (right_y + right_h > LCD_H) right_h = LCD_H - right_y;
            uint16_t right_w = (right_x + LineWidth > LCD_W) ? (LCD_W - right_x) : LineWidth;
            DMA2D_FillRect(back_addr, right_x, right_y, right_w, right_h, Color);
        }
    }

    // 更新dirty rect
    uint16_t x1 = (X + W - 1 >= LCD_W) ? (LCD_W - 1) : (X + W - 1);
    uint16_t y1 = (Y + H - 1 >= LCD_H) ? (LCD_H - 1) : (Y + H - 1);
    DirtyMergeRect(Layer, X, Y, x1, y1);
}

/**
 * @brief 绘制点（方形笔刷）
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_DrawPoint(uint8_t Layer, uint16_t X, uint16_t Y, uint8_t Size, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H || Size == 0) return;
    uint16_t w = (X + Size > LCD_W) ? (LCD_W - X) : Size;
    uint16_t h = (Y + Size > LCD_H) ? (LCD_H - Y) : Size;

    DMA2D_FillRect(layer_info[Layer].back_addr, X, Y, w, h, Color);
    DirtyMergeRect(Layer, X, Y, X + w - 1, Y + h - 1);
}

/**
 * @brief 绘制水平线
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_DrawHLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H || Length == 0) return;
    if (X + Length > LCD_W) Length = LCD_W - X;

    DMA2D_FillRect(layer_info[Layer].back_addr, X, Y, Length, 1, Color);
    DirtyMergeRect(Layer, X, Y, X + Length - 1, Y);
}

/**
 * @brief 绘制垂直线
 * 【修复】使用DMA2D填充back buffer
 */
void LCD_DrawVLine(uint8_t Layer, uint16_t X, uint16_t Y, uint16_t Length, uint32_t Color) {
    if (X >= LCD_W || Y >= LCD_H || Length == 0) return;
    if (Y + Length > LCD_H) Length = LCD_H - Y;

    DMA2D_FillRect(layer_info[Layer].back_addr, X, Y, 1, Length, Color);
    DirtyMergeRect(Layer, X, Y, X, Y + Length - 1);
}

/* ============================================================================
 *              【新增】int32坐标API - 支持边界裁剪（解决uint16 wrap问题）
 * ============================================================================ */

/**
 * @brief 【新增】绘制矩形外框（int32坐标版，自动裁剪）
 *
 * 用途：解决上层传入"半个图标露出屏幕边缘"时uint16_t wrap导致的异常
 * 原理：在驱动层做屏幕裁剪后再绘制，保证边缘可见部分能稳定画出来
 */
void LCD_DrawRectOutlineI32(uint8_t Layer, int x, int y, int w, int h, int lineWidth, uint32_t Color) {
    if (w <= 0 || h <= 0 || lineWidth <= 0) return;

    // 裁剪到屏幕范围
    int x1 = x;
    int y1 = y;
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    if (x2 < 0 || y2 < 0 || x1 >= LCD_W || y1 >= LCD_H) return; // 完全在屏幕外

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= LCD_W) x2 = LCD_W - 1;
    if (y2 >= LCD_H) y2 = LCD_H - 1;

    uint32_t back_addr = layer_info[Layer].back_addr;

    // 绘制四条边
    // 上边
    if (y >= y1 && y <= y2) {
        int top_y = (y < 0) ? 0 : y;
        int top_h = lineWidth;
        if (top_y + top_h > LCD_H) top_h = LCD_H - top_y;
        if (top_h > 0 && top_y >= 0 && top_y < LCD_H) {
            DMA2D_FillRect(back_addr, x1, top_y, x2 - x1 + 1, top_h, Color);
        }
    }

    // 下边
    int bot_y = y + h - lineWidth;
    if (bot_y >= y1 && bot_y <= y2) {
        if (bot_y < 0) bot_y = 0;
        int bot_h = lineWidth;
        if (bot_y + bot_h > LCD_H) bot_h = LCD_H - bot_y;
        if (bot_h > 0 && bot_y >= 0 && bot_y < LCD_H) {
            DMA2D_FillRect(back_addr, x1, bot_y, x2 - x1 + 1, bot_h, Color);
        }
    }

    // 左边
    if (x >= x1 && x <= x2 && h > 2 * lineWidth) {
        int left_x = (x < 0) ? 0 : x;
        int left_y = y + lineWidth;
        int left_h = h - 2 * lineWidth;

        if (left_y < 0) {
            left_h += left_y;
            left_y = 0;
        }
        if (left_y + left_h > LCD_H) left_h = LCD_H - left_y;

        int left_w = lineWidth;
        if (left_x + left_w > LCD_W) left_w = LCD_W - left_x;

        if (left_h > 0 && left_w > 0 && left_x >= 0 && left_y >= 0) {
            DMA2D_FillRect(back_addr, left_x, left_y, left_w, left_h, Color);
        }
    }

    // 右边
    int right_x = x + w - lineWidth;
    if (right_x >= x1 && right_x <= x2 && h > 2 * lineWidth) {
        if (right_x < 0) right_x = 0;
        int right_y = y + lineWidth;
        int right_h = h - 2 * lineWidth;

        if (right_y < 0) {
            right_h += right_y;
            right_y = 0;
        }
        if (right_y + right_h > LCD_H) right_h = LCD_H - right_y;

        int right_w = lineWidth;
        if (right_x + right_w > LCD_W) right_w = LCD_W - right_x;

        if (right_h > 0 && right_w > 0 && right_x >= 0 && right_y >= 0 && right_x < LCD_W) {
            DMA2D_FillRect(back_addr, right_x, right_y, right_w, right_h, Color);
        }
    }

    // ✅ 关键：更新dirty rect（使用裁剪后的坐标）
    DirtyMergeRect(Layer, (uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2);
}

/**
 * @brief 【修复版】绘制实心矩形（int32坐标版，自动裁剪）
 */
void LCD_DrawRectFilledI32(uint8_t Layer, int x, int y, int w, int h, uint32_t Color) {
    if (w <= 0 || h <= 0) return;

    // 裁剪到屏幕范围
    int x1 = x;
    int y1 = y;
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    if (x2 < 0 || y2 < 0 || x1 >= LCD_W || y1 >= LCD_H) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= LCD_W) x2 = LCD_W - 1;
    if (y2 >= LCD_H) y2 = LCD_H - 1;

    uint16_t clip_w = x2 - x1 + 1;
    uint16_t clip_h = y2 - y1 + 1;

    DMA2D_FillRect(layer_info[Layer].back_addr, x1, y1, clip_w, clip_h, Color);

    // ✅ 关键：更新dirty rect（使用裁剪后的坐标）
    DirtyMergeRect(Layer, (uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2);
}

/* ============================================================================
 *                         任意方向直线（Bresenham算法）
 * ============================================================================ */

/**
 * @brief 绘制任意方向直线
 * 【修复】使用back buffer
 */
void LCD_DrawLine(uint8_t Layer, int x0, int y0, int x1, int y1, uint8_t Size, uint32_t Color) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int minx = (x0 < x1) ? x0 : x1;
    int maxx = (x0 > x1) ? x0 : x1;
    int miny = (y0 < y1) ? y0 : y1;
    int maxy = (y0 > y1) ? y0 : y1;

    while (1) {
        for (int i = 0; i < Size; i++) {
            for (int j = 0; j < Size; j++) {
                int px = x0 + i;
                int py = y0 + j;
                if (px >= 0 && px < LCD_W && py >= 0 && py < LCD_H) {
                    LCD_DrawPixelFast(Layer, px, py, Color);
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/* ============================================================================
 *                         圆形绘制（Bresenham圆算法）
 * ============================================================================ */

/**
 * @brief 绘制空心圆
 * 【修复】使用back buffer
 */
void LCD_DrawCircle(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint8_t LineWidth, uint32_t Color) {
    if (Radius == 0 || LineWidth == 0) return;

    int cx = CenterX, cy = CenterY, r = Radius;
    int minx = cx - r - LineWidth, maxx = cx + r + LineWidth;
    int miny = cy - r - LineWidth, maxy = cy + r + LineWidth;

    for (uint16_t rr = Radius; rr > 0 && rr >= Radius - LineWidth; rr--) {
        int x = 0, y = rr, d = 3 - 2 * rr;

        #define DRAW8(cx, cy, x, y) \
            do { \
                LCD_DrawPixelFast(Layer, cx + x, cy + y, Color); \
                LCD_DrawPixelFast(Layer, cx - x, cy + y, Color); \
                LCD_DrawPixelFast(Layer, cx + x, cy - y, Color); \
                LCD_DrawPixelFast(Layer, cx - x, cy - y, Color); \
                LCD_DrawPixelFast(Layer, cx + y, cy + x, Color); \
                LCD_DrawPixelFast(Layer, cx - y, cy + x, Color); \
                LCD_DrawPixelFast(Layer, cx + y, cy - x, Color); \
                LCD_DrawPixelFast(Layer, cx - y, cy - x, Color); \
            } while(0)

        DRAW8(cx, cy, x, y);
        while (x < y) {
            if (d < 0) {
                d = d + 4 * x + 6;
            } else {
                d = d + 4 * (x - y) + 10;
                y--;
            }
            x++;
            DRAW8(cx, cy, x, y);
        }
        #undef DRAW8
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/**
 * @brief 绘制实心圆
 * 【修复】使用back buffer
 */
void LCD_DrawCircleFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius, uint32_t Color) {
    if (Radius == 0) return;

    int cx = CenterX, cy = CenterY;
    int x = 0, y = Radius, d = 3 - 2 * Radius;
    int minx = cx - Radius, maxx = cx + Radius;
    int miny = cy - Radius, maxy = cy + Radius;

    uint32_t *fb = LCD_GetDrawFB(Layer);  // 【修复】使用back buffer

    while (x <= y) {
        for (int xx = cx - x; xx <= cx + x; xx++) {
            if (xx >= 0 && xx < LCD_W) {
                if (cy + y >= 0 && cy + y < LCD_H) fb[(cy + y) * LCD_W + xx] = Color;
                if (cy - y >= 0 && cy - y < LCD_H) fb[(cy - y) * LCD_W + xx] = Color;
            }
        }
        for (int xx = cx - y; xx <= cx + y; xx++) {
            if (xx >= 0 && xx < LCD_W) {
                if (cy + x >= 0 && cy + x < LCD_H) fb[(cy + x) * LCD_W + xx] = Color;
                if (cy - x >= 0 && cy - x < LCD_H) fb[(cy - x) * LCD_W + xx] = Color;
            }
        }

        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/* ============================================================================
 *                         三角形/多边形绘制
 * ============================================================================ */

/**
 * @brief 绘制三角形外框
 */
void LCD_DrawTriangle(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t LineWidth, uint32_t Color) {
    LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
    LCD_DrawLine(Layer, x1, y1, x2, y2, LineWidth, Color);
    LCD_DrawLine(Layer, x2, y2, x0, y0, LineWidth, Color);
}

/**
 * @brief 绘制实心三角形（扫描线填充）
 * 【修复】使用back buffer
 */
void LCD_DrawTriangleFilled(uint8_t Layer, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t Color) {
    // 按y坐标排序顶点
    if (y0 > y1) { int tx=x0,ty=y0; x0=x1; y0=y1; x1=tx; y1=ty; }
    if (y0 > y2) { int tx=x0,ty=y0; x0=x2; y0=y2; x2=tx; y2=ty; }
    if (y1 > y2) { int tx=x1,ty=y1; x1=x2; y1=y2; x2=tx; y2=ty; }

    int minx = x0, maxx = x0;
    if (x1 < minx) minx = x1; if (x1 > maxx) maxx = x1;
    if (x2 < minx) minx = x2; if (x2 > maxx) maxx = x2;
    int miny = y0, maxy = y2;

    uint32_t *fb = LCD_GetDrawFB(Layer);  // 【修复】使用back buffer

    // 填充上半部分
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= LCD_H) continue;
        int xa = (y1 == y0) ? x0 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        int xb = (y2 == y0) ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (xa > xb) { int tmp = xa; xa = xb; xb = tmp; }
        if (xa < 0) xa = 0;
        if (xb >= LCD_W) xb = LCD_W - 1;
        for (int x = xa; x <= xb && x < LCD_W; x++) {
            if (x >= 0) fb[y * LCD_W + x] = Color;
        }
    }

    // 填充下半部分
    for (int y = y1 + 1; y <= y2; y++) {
        if (y < 0 || y >= LCD_H) continue;
        int xa = (y2 == y1) ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        int xb = (y2 == y0) ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        if (xa > xb) { int tmp = xa; xa = xb; xb = tmp; }
        if (xa < 0) xa = 0;
        if (xb >= LCD_W) xb = LCD_W - 1;
        for (int x = xa; x <= xb && x < LCD_W; x++) {
            if (x >= 0) fb[y * LCD_W + x] = Color;
        }
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/**
 * @brief 绘制折线
 */
void LCD_DrawPolyline(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color) {
    if (count < 2) return;
    for (uint16_t i = 0; i < count - 1; i++) {
        int x0 = points[i * 2];
        int y0 = points[i * 2 + 1];
        int x1 = points[(i + 1) * 2];
        int y1 = points[(i + 1) * 2 + 1];
        LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
    }
}

/**
 * @brief 绘制多边形外框
 */
void LCD_DrawPolygon(uint8_t Layer, const int16_t *points, uint16_t count, uint8_t LineWidth, uint32_t Color) {
    if (count < 3) return;
    LCD_DrawPolyline(Layer, points, count, LineWidth, Color);
    int x0 = points[(count - 1) * 2];
    int y0 = points[(count - 1) * 2 + 1];
    int x1 = points[0];
    int y1 = points[1];
    LCD_DrawLine(Layer, x0, y0, x1, y1, LineWidth, Color);
}

/**
 * @brief 绘制实心多边形（扫描线填充，简化版）
 * 【修复】使用back buffer
 */
void LCD_DrawPolygonFilled(uint8_t Layer, const int16_t *points, uint16_t count, uint32_t Color) {
    if (count < 3) return;

    int miny = points[1], maxy = points[1];
    int minx = points[0], maxx = points[0];
    for (uint16_t i = 1; i < count; i++) {
        int x = points[i * 2], y = points[i * 2 + 1];
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
    }

    uint32_t *fb = LCD_GetDrawFB(Layer);  // 【修复】使用back buffer

    for (int y = miny; y <= maxy; y++) {
        if (y < 0 || y >= LCD_H) continue;

        int intersections[32];
        int num_intersect = 0;

        for (uint16_t i = 0; i < count; i++) {
            int x0 = points[i * 2];
            int y0 = points[i * 2 + 1];
            int x1 = points[((i + 1) % count) * 2];
            int y1 = points[((i + 1) % count) * 2 + 1];

            if ((y0 <= y && y < y1) || (y1 <= y && y < y0)) {
                int x_intersect = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
                if (num_intersect < 32) {
                    intersections[num_intersect++] = x_intersect;
                }
            }
        }

        for (int i = 0; i < num_intersect - 1; i++) {
            for (int j = i + 1; j < num_intersect; j++) {
                if (intersections[i] > intersections[j]) {
                    int tmp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = tmp;
                }
            }
        }

        for (int i = 0; i < num_intersect; i += 2) {
            if (i + 1 < num_intersect) {
                int x_start = intersections[i];
                int x_end = intersections[i + 1];
                if (x_start < 0) x_start = 0;
                if (x_end >= LCD_W) x_end = LCD_W - 1;
                for (int x = x_start; x <= x_end && x < LCD_W; x++) {
                    if (x >= 0) fb[y * LCD_W + x] = Color;
                }
            }
        }
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/* ============================================================================
 *                         椭圆绘制
 * ============================================================================ */

/**
 * @brief 绘制空心椭圆
 * 【修复】使用back buffer
 */
void LCD_DrawEllipse(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint8_t LineWidth, uint32_t Color) {
    if (RadiusX == 0 || RadiusY == 0 || LineWidth == 0) return;

    int cx = CenterX, cy = CenterY;
    int minx = cx - RadiusX - LineWidth, maxx = cx + RadiusX + LineWidth;
    int miny = cy - RadiusY - LineWidth, maxy = cy + RadiusY + LineWidth;

    for (uint16_t rx_i = RadiusX; rx_i > 0 && rx_i >= RadiusX - LineWidth; rx_i--) {
        for (uint16_t ry_i = RadiusY; ry_i > 0 && ry_i >= RadiusY - LineWidth; ry_i--) {
            int rx = rx_i, ry = ry_i;
            int x = 0, y = ry;
            int rx2 = rx * rx, ry2 = ry * ry;
            int two_rx2 = 2 * rx2, two_ry2 = 2 * ry2;
            int px = 0, py = two_rx2 * y;
            int p = ry2 - rx2 * ry + (rx2 >> 2);

            #define DRAW4E(cx, cy, x, y) \
                do { \
                    LCD_DrawPixelFast(Layer, cx + x, cy + y, Color); \
                    LCD_DrawPixelFast(Layer, cx - x, cy + y, Color); \
                    LCD_DrawPixelFast(Layer, cx + x, cy - y, Color); \
                    LCD_DrawPixelFast(Layer, cx - x, cy - y, Color); \
                } while(0)

            DRAW4E(cx, cy, x, y);
            while (px < py) {
                x++;
                px += two_ry2;
                if (p < 0) {
                    p += ry2 + px;
                } else {
                    y--;
                    py -= two_rx2;
                    p += ry2 + px - py;
                }
                DRAW4E(cx, cy, x, y);
            }

            p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
            while (y >= 0) {
                DRAW4E(cx, cy, x, y);
                y--;
                py -= two_rx2;
                if (p > 0) {
                    p += rx2 - py;
                } else {
                    x++;
                    px += two_ry2;
                    p += rx2 - py + px;
                }
            }
            #undef DRAW4E
        }
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/**
 * @brief 绘制实心椭圆
 * 【修复】使用back buffer
 */
void LCD_DrawEllipseFilled(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t RadiusX, uint16_t RadiusY, uint32_t Color) {
    if (RadiusX == 0 || RadiusY == 0) return;

    int cx = CenterX, cy = CenterY, rx = RadiusX, ry = RadiusY;
    int x = 0, y = ry;
    int rx2 = rx * rx, ry2 = ry * ry;
    int two_rx2 = 2 * rx2, two_ry2 = 2 * ry2;
    int px = 0, py = two_rx2 * y;
    int p = ry2 - rx2 * ry + (rx2 >> 2);

    int minx = cx - rx, maxx = cx + rx;
    int miny = cy - ry, maxy = cy + ry;

    uint32_t *fb = LCD_GetDrawFB(Layer);  // 【修复】使用back buffer

    while (px < py) {
        for (int xx = cx - x; xx <= cx + x; xx++) {
            if (xx >= 0 && xx < LCD_W) {
                if (cy + y >= 0 && cy + y < LCD_H) fb[(cy + y) * LCD_W + xx] = Color;
                if (cy - y >= 0 && cy - y < LCD_H) fb[(cy - y) * LCD_W + xx] = Color;
            }
        }
        x++;
        px += two_ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= two_rx2;
            p += ry2 + px - py;
        }
    }

    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        for (int xx = cx - x; xx <= cx + x; xx++) {
            if (xx >= 0 && xx < LCD_W) {
                if (cy + y >= 0 && cy + y < LCD_H) fb[(cy + y) * LCD_W + xx] = Color;
                if (cy - y >= 0 && cy - y < LCD_H) fb[(cy - y) * LCD_W + xx] = Color;
            }
        }
        y--;
        py -= two_rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += two_ry2;
            p += rx2 - py + px;
        }
    }

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/**
 * @brief 绘制圆弧（使用atan2f计算角度）
 * 【修复】使用back buffer
 */
void LCD_DrawArc(uint8_t Layer, uint16_t CenterX, uint16_t CenterY, uint16_t Radius,
                 int16_t StartAngle, int16_t EndAngle, uint8_t LineWidth, uint32_t Color) {
    if (Radius == 0 || LineWidth == 0) return;

    while (StartAngle < 0) StartAngle += 360;
    while (EndAngle < 0) EndAngle += 360;
    while (StartAngle >= 360) StartAngle -= 360;
    while (EndAngle >= 360) EndAngle -= 360;

    int pad = LineWidth;
    int minx = CenterX - Radius - pad, maxx = CenterX + Radius + pad;
    int miny = CenterY - Radius - pad, maxy = CenterY + Radius + pad;

    int x = 0, y = Radius, d = 3 - 2 * Radius;
    int cx = CenterX, cy = CenterY;

    #define IS_IN_ARC(px, py) \
        ({ \
            int dx = (px) - cx; \
            int dy = cy - (py); \
            float angle_deg = atan2f((float)dy, (float)dx) * 180.0f / 3.14159265f; \
            if (angle_deg < 0) angle_deg += 360.0f; \
            int in_arc = 0; \
            if (StartAngle <= EndAngle) { \
                in_arc = (angle_deg >= (float)StartAngle && angle_deg <= (float)EndAngle); \
            } else { \
                in_arc = (angle_deg >= (float)StartAngle || angle_deg <= (float)EndAngle); \
            } \
            in_arc; \
        })

    #define DRAW_ARC_POINT(px, py) \
        do { \
            if (IS_IN_ARC(px, py)) { \
                for (int i = 0; i < LineWidth; i++) { \
                    LCD_DrawPixelFast(Layer, px, py + i, Color); \
                } \
            } \
        } while(0)

    DRAW_ARC_POINT(cx + x, cy + y);
    DRAW_ARC_POINT(cx - x, cy + y);
    DRAW_ARC_POINT(cx + x, cy - y);
    DRAW_ARC_POINT(cx - x, cy - y);
    DRAW_ARC_POINT(cx + y, cy + x);
    DRAW_ARC_POINT(cx - y, cy + x);
    DRAW_ARC_POINT(cx + y, cy - x);
    DRAW_ARC_POINT(cx - y, cy - x);

    while (x < y) {
        if (d < 0) {
            d = d + 4 * x + 6;
        } else {
            d = d + 4 * (x - y) + 10;
            y--;
        }
        x++;

        DRAW_ARC_POINT(cx + x, cy + y);
        DRAW_ARC_POINT(cx - x, cy + y);
        DRAW_ARC_POINT(cx + x, cy - y);
        DRAW_ARC_POINT(cx - x, cy - y);
        DRAW_ARC_POINT(cx + y, cy + x);
        DRAW_ARC_POINT(cx - y, cy + x);
        DRAW_ARC_POINT(cx + y, cy - x);
        DRAW_ARC_POINT(cx - y, cy - x);
    }

    #undef IS_IN_ARC
    #undef DRAW_ARC_POINT

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= LCD_W) maxx = LCD_W - 1;
    if (maxy >= LCD_H) maxy = LCD_H - 1;
    if (maxx >= minx && maxy >= miny) {
        DirtyMergeRect(Layer, minx, miny, maxx, maxy);
    }
}

/* ============================================================================
 *                         刷新提交 / 双缓冲翻页
 * ============================================================================ */

/**
 * @brief 提交一层的绘制结果（back buffer -> 请求在 VBlank 翻页显示）
 *
 * 行为：
 *  1) 等 DMA2D 空闲（防止刚画完就 clean/翻页）
 *  2) 如果 dirty_valid=1：对 dirty rect 范围做 DCache clean
 *  3) 设置 pending_swap=1：等待 LineEvent 回调里执行 swap & 更新 CFBAR
 *
 * 【修复】只clean dirty rect的back buffer区域，确保LTDC能读到最新数据
 */
void LCD_Refresh(uint8_t Layer) {
    WaitDMA2D();

    // 仅 clean dirty 区域（减少 cache 操作）
    if (layer_info[Layer].dirty_valid) {
        uint16_t x0 = layer_info[Layer].dirty_x0;
        uint16_t y0 = layer_info[Layer].dirty_y0;
        uint16_t x1 = layer_info[Layer].dirty_x1;
        uint16_t y1 = layer_info[Layer].dirty_y1;

        uint32_t w = x1 - x0 + 1;
        uint32_t h = y1 - y0 + 1;

        // back buffer 的 dirty 区域起始地址
        uint32_t *start = (uint32_t*)(layer_info[Layer].back_addr + (y0 * LCD_W + x0) * 4);

        // 按矩形在 framebuffer 中的连续跨度 clean（覆盖整段行跨度）
        uint32_t bytes = ((h - 1) * LCD_W + w) * 4;
        LCD_DCacheClean(start, bytes);

        layer_info[Layer].dirty_valid = 0;
    }

    // 提交翻页请求：交给 VBlank 回调做 swap
    layer_info[Layer].pending_swap = 1;
}

/**
 * @brief 双缓冲初始化：设置 CFBAR、初始化buffer内容、配置 LineEvent
 *
 * 【修复要点】
 * 1. 使用正确的LineEvent计算：AccumulatedActiveH + 1（确保在VBlank触发）
 * 2. Layer1的front/back两块buffer都初始化为透明，避免swap时"下半闪"
 */
void LCD_DoubleBufferInit(void) {
    // 直接用寄存器偏移获取 Layer 寄存器块（H7: Layer0=0x84，Layer1=0x104）
    LTDC_Layer_TypeDef *layer0 = (LTDC_Layer_TypeDef*)((uint32_t)hltdc.Instance + 0x84);
    LTDC_Layer_TypeDef *layer1 = (LTDC_Layer_TypeDef*)((uint32_t)hltdc.Instance + 0x104);

    // 【修复】确保Layer1的front/back两块buffer都初始化为透明（解决"下半闪"问题）
    // Layer0不需要（front==back，无双缓冲）
    // Layer1需要：清空front buffer
    memset((void*)layer_info[1].front_addr, 0x00, FB_SIZE);
    LCD_DCacheClean((void*)layer_info[1].front_addr, FB_SIZE);

    // 清空back buffer
    memset((void*)layer_info[1].back_addr, 0x00, FB_SIZE);
    LCD_DCacheClean((void*)layer_info[1].back_addr, FB_SIZE);

    // 初始显示 front
    layer0->CFBAR = layer_info[0].front_addr;
    layer1->CFBAR = layer_info[1].front_addr;

    // VBlank reload：让 CFBAR 在 VBlank 生效
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);

    // 【修复】正确计算LineEvent：使用AccumulatedActiveH + 1确保在VBlank区域触发
    // AccumulatedActiveH = VerticalSync + VerticalBackPorch + ActiveHeight - 1
    // 所以 AccumulatedActiveH + 1 就是进入VBlank的第一行
    uint32_t line_event = hltdc.Init.AccumulatedActiveH + 1;
    HAL_LTDC_ProgramLineEvent(&hltdc, line_event);

    // 开启 Line Interrupt（LI）
    __HAL_LTDC_ENABLE_IT(&hltdc, LTDC_IT_LI);

    // 重置VBlank计数器
    g_ltdc_vblank_cnt = 0;
}

/**
 * @brief 【新增】获取VBlank计数（用于上层按帧节流渲染）
 */
uint32_t LCD_GetVBlankCount(void) {
    return g_ltdc_vblank_cnt;
}


/**
 * @brief 检查某层是否有pending swap
 */
uint8_t LCD_IsPendingSwap(uint8_t Layer) {
    if (Layer >= 2) return 0;
    return layer_info[Layer].pending_swap;
}


/**
 * @brief LTDC LineEvent 回调（在 VBlank 区域触发）
 *
 * 【修复要点】
 * 1. 在VBlank时刻交换front/back并更新CFBAR
 * 2. 使用VBlank reload确保无撕裂
 * 3. 重新arm LineEvent保证下一帧继续触发
 * 4. 维护VBlank计数器
 *
 * NOTE：此函数应放在 stm32h7xx_it.c 的 USER CODE 区域，或者在 main.c 中定义
 *       这里提供完整实现供参考
 */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc_param) {
    // VBlank计数+1（用于上层按帧节流）
    g_ltdc_vblank_cnt++;

    // 检查每个layer是否有pending swap
    for (uint8_t layer = 0; layer < 2; layer++) {
        if (layer_info[layer].pending_swap) {
            // 【关键】交换 front/back（翻页）
            uint32_t tmp = layer_info[layer].front_addr;
            layer_info[layer].front_addr = layer_info[layer].back_addr;
            layer_info[layer].back_addr = tmp;

            // 根据 layer 索引计算 Layer 寄存器块地址并更新 CFBAR
            LTDC_Layer_TypeDef *ltdc_layer =
                (LTDC_Layer_TypeDef*)((uint32_t)hltdc_param->Instance + 0x84 + layer * 0x80);

            ltdc_layer->CFBAR = layer_info[layer].front_addr;

            layer_info[layer].pending_swap = 0;
        }
    }

    // 【关键】请求 VBlank reload（让刚写的 CFBAR 在 VBlank 生效，避免撕裂）
    HAL_LTDC_Reload(hltdc_param, LTDC_RELOAD_VERTICAL_BLANKING);

    // 【关键】重新设置下一次 LineEvent（HAL只触发一次，需要每次重新arm）
    uint32_t line_event = hltdc_param->Init.AccumulatedActiveH + 1;
    HAL_LTDC_ProgramLineEvent(hltdc_param, line_event);
}
// // ============================================================================
// // 字体绘制函数实现
// // ============================================================================
//
// /**
//  * @brief 绘制单个ASCII字符（8x16点阵）
//  *
//  * 性能优化策略：
//  * 1. 如果有背景色：先用DMA2D快速填充8x16矩形背景
//  * 2. 然后CPU绘制前景（只写需要的像素）
//  * 3. 边界裁剪避免越界
//  */
// void LCD_DrawChar(uint8_t Layer, int X, int Y, char Char, uint32_t FgColor, uint32_t BgColor)
// {
//     // 检查字符范围
//     if (Char < 0x20 || Char > 0x7E) return;
//
//     // 计算字体数据索引
//     uint8_t char_index = Char - 0x20;
//     const uint8_t *font_data = Font8x16_ASCII[char_index];
//
//     // 边界检查：完全在屏幕外
//     if (X + 8 <= 0 || Y + 16 <= 0 || X >= LCD_W || Y >= LCD_H) return;
//
//     // 计算实际绘制范围（裁剪到屏幕内）
//     int draw_x0 = (X < 0) ? 0 : X;
//     int draw_y0 = (Y < 0) ? 0 : Y;
//     int draw_x1 = (X + 8 > LCD_W) ? LCD_W - 1 : X + 7;
//     int draw_y1 = (Y + 16 > LCD_H) ? LCD_H - 1 : Y + 15;
//
//     int draw_w = draw_x1 - draw_x0 + 1;
//     int draw_h = draw_y1 - draw_y0 + 1;
//
//     if (draw_w <= 0 || draw_h <= 0) return;
//
//     // 步骤1: 如果有背景色，先用DMA2D填充背景
//     if ((BgColor & 0xFF000000) != 0) {  // Alpha非0，有背景
//         LCD_DrawRectFilledI32(Layer, X, Y, 8, 16, BgColor);
//     }
//
//     // 步骤2: 绘制前景（字形）
//     uint32_t *fb = LCD_GetDrawFB(Layer);
//
//     // 计算起始偏移（相对于字符左上角）
//     int start_col = (X < 0) ? -X : 0;
//     int start_row = (Y < 0) ? -Y : 0;
//
//     for (int row = start_row; row < 16 && (Y + row) < LCD_H; row++) {
//         if ((Y + row) < 0) continue;
//
//         uint8_t row_data = font_data[row];
//
//         for (int col = start_col; col < 8 && (X + col) < LCD_W; col++) {
//             if ((X + col) < 0) continue;
//
//             // 检查该位是否为1（前景）
//             if (row_data & (0x80 >> col)) {
//                 int px = X + col;
//                 int py = Y + row;
//                 fb[py * LCD_W + px] = FgColor;
//             }
//         }
//     }
//
//     // 更新dirty rect
//     extern void DirtyMergeRect(uint8_t Layer, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
//     DirtyMergeRect(Layer, draw_x0, draw_y0, draw_x1, draw_y1);
// }
//
// /**
//  * @brief 绘制字符串（8x16点阵）
//  */
// uint16_t LCD_DrawString(uint8_t Layer, int X, int Y, const char *Str, uint32_t FgColor, uint32_t BgColor)
// {
//     if (Str == NULL) return 0;
//
//     uint16_t count = 0;
//     int cursor_x = X;
//
//     while (*Str) {
//         char ch = *Str++;
//
//         // 只绘制可打印ASCII字符
//         if (ch >= 0x20 && ch <= 0x7E) {
//             LCD_DrawChar(Layer, cursor_x, Y, ch, FgColor, BgColor);
//             cursor_x += 8;  // 每个字符8像素宽
//             count++;
//         }
//     }
//
//     return count;
// }
//
// /**
//  * @brief 绘制整数（8x16点阵）
//  */
// uint16_t LCD_DrawInt(uint8_t Layer, int X, int Y, int32_t Value, uint32_t FgColor, uint32_t BgColor)
// {
//     char buffer[12];  // 足够容纳 -2147483648 + '\0'
//
//     // 转换为字符串
//     if (Value == 0) {
//         buffer[0] = '0';
//         buffer[1] = '\0';
//     } else {
//         int is_negative = 0;
//         if (Value < 0) {
//             is_negative = 1;
//             Value = -Value;
//         }
//
//         char temp[12];
//         int idx = 0;
//
//         while (Value > 0) {
//             temp[idx++] = '0' + (Value % 10);
//             Value /= 10;
//         }
//
//         int buf_idx = 0;
//         if (is_negative) {
//             buffer[buf_idx++] = '-';
//         }
//
//         // 反转
//         for (int i = idx - 1; i >= 0; i--) {
//             buffer[buf_idx++] = temp[i];
//         }
//         buffer[buf_idx] = '\0';
//     }
//
//     return LCD_DrawString(Layer, X, Y, buffer, FgColor, BgColor);
// }
//
// /**
//  * @brief 绘制浮点数（8x16点阵）
//  */
// uint16_t LCD_DrawFloat(uint8_t Layer, int X, int Y, float Value, uint8_t Decimals, uint32_t FgColor, uint32_t BgColor)
// {
//     if (Decimals > 6) Decimals = 6;
//
//     char buffer[24];
//
//     // 简单的浮点转字符串（避免依赖sprintf）
//     int is_negative = 0;
//     if (Value < 0) {
//         is_negative = 1;
//         Value = -Value;
//     }
//
//     // 整数部分
//     int32_t int_part = (int32_t)Value;
//     float frac_part = Value - (float)int_part;
//
//     // 构建字符串
//     char temp[24];
//     int idx = 0;
//
//     // 整数部分
//     if (int_part == 0) {
//         temp[idx++] = '0';
//     } else {
//         char int_temp[12];
//         int int_idx = 0;
//         int32_t val = int_part;
//         while (val > 0) {
//             int_temp[int_idx++] = '0' + (val % 10);
//             val /= 10;
//         }
//         for (int i = int_idx - 1; i >= 0; i--) {
//             temp[idx++] = int_temp[i];
//         }
//     }
//
//     // 小数点
//     if (Decimals > 0) {
//         temp[idx++] = '.';
//
//         // 小数部分
//         for (uint8_t i = 0; i < Decimals; i++) {
//             frac_part *= 10;
//             int digit = (int)frac_part;
//             temp[idx++] = '0' + digit;
//             frac_part -= digit;
//         }
//     }
//
//     // 添加符号
//     int buf_idx = 0;
//     if (is_negative) {
//         buffer[buf_idx++] = '-';
//     }
//
//     for (int i = 0; i < idx; i++) {
//         buffer[buf_idx++] = temp[i];
//     }
//     buffer[buf_idx] = '\0';
//
//     return LCD_DrawString(Layer, X, Y, buffer, FgColor, BgColor);
// }


// 选择一个字号：24/28/32 三选一
#include "jbmono_thin_a8_20px.h"   // 例：W=18 H=38
// #include "jbmono_a8_24px.h"
// #include "jbmono_a8_32px.h"

#include <stdint.h>
#include <stddef.h>

// 你的项目已有这些符号
#ifndef LCD_W
#define LCD_W 800
#endif
#ifndef LCD_H
#define LCD_H 480
#endif

extern uint32_t *LCD_GetDrawFB(uint8_t Layer);
extern void LCD_DrawRectFilledI32(uint8_t Layer, int x, int y, int w, int h, uint32_t argb);
extern void DirtyMergeRect(uint8_t Layer, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// JetBrains Mono 字格尺寸
#define FONT_W JBMONO_W
#define FONT_H JBMONO_H
#define FONT_ADVANCE_X JBMONO_W  // 等宽：每个字符前进宽度=字格宽

// 强制 A=FF（你之前也强调过 “A 必须 FF”）
static inline uint32_t ARGB8888_Opaque(uint32_t c)
{
    return 0xFF000000u | (c & 0x00FFFFFFu);
}

// dst over fg，a=0..255（a 已经包含 fgA 调制之后）
static inline uint32_t BlendOver_ARGB8888(uint32_t dst, uint32_t fg_rgb_opaque, uint8_t a)
{
    if (a == 0)   return dst;
    if (a == 255) return fg_rgb_opaque;

    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t fr = (fg_rgb_opaque >> 16) & 0xFF, fg = (fg_rgb_opaque >> 8) & 0xFF, fb = fg_rgb_opaque & 0xFF;

    uint32_t ia = 255u - a;
    uint32_t r = (dr * ia + fr * a + 127u) / 255u;
    uint32_t g = (dg * ia + fg * a + 127u) / 255u;
    uint32_t b = (db * ia + fb * a + 127u) / 255u;

    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/**
 * @brief 绘制单个ASCII字符（JetBrains Mono A8 alpha mask）
 *
 * 规则：
 * - BgColor: Alpha!=0 -> 先填背景；Alpha==0 -> 透明背景（不填）
 * - FgColor: 使用其 RGB；其 Alpha 会与 glyph alpha 相乘作为最终混合 alpha
 */
void LCD_DrawChar(uint8_t Layer, int X, int Y, char Char, uint32_t FgColor, uint32_t BgColor)
{
    // 只支持可打印 ASCII（你生成的字库是 0x20..0x7E）
    uint8_t uc = (uint8_t)Char;
    if (uc < JBMONO_FIRST || uc > JBMONO_LAST) return;

    // 完全在屏幕外：快速退出
    if (X + FONT_W <= 0 || Y + FONT_H <= 0 || X >= LCD_W || Y >= LCD_H) return;

    // 计算实际绘制范围（裁剪到屏幕内）
    int draw_x0 = (X < 0) ? 0 : X;
    int draw_y0 = (Y < 0) ? 0 : Y;
    int draw_x1 = (X + FONT_W > LCD_W) ? (LCD_W - 1) : (X + FONT_W - 1);
    int draw_y1 = (Y + FONT_H > LCD_H) ? (LCD_H - 1) : (Y + FONT_H - 1);

    int draw_w = draw_x1 - draw_x0 + 1;
    int draw_h = draw_y1 - draw_y0 + 1;
    if (draw_w <= 0 || draw_h <= 0) return;

    // glyph alpha mask
    const uint8_t *glyph = JBMono_GlyphA8((char)uc);

    // Step1: 背景（如果 alpha != 0）
    if ((BgColor & 0xFF000000u) != 0u) {
        uint32_t bg = ARGB8888_Opaque(BgColor);
        LCD_DrawRectFilledI32(Layer, X, Y, FONT_W, FONT_H, bg);
        // 如果你的 RectFilled 不负责 dirty，这里兜底 merge 一下
        DirtyMergeRect(Layer, (uint16_t)draw_x0, (uint16_t)draw_y0, (uint16_t)draw_x1, (uint16_t)draw_y1);
    }

    // Step2: 前景（A8 alpha 混合）
    uint32_t *fb = LCD_GetDrawFB(Layer);

    uint8_t  fgA = (uint8_t)(FgColor >> 24);              // 用来调制 glyph alpha
    if (fgA == 0) return;                                 // 完全透明前景，直接结束
    uint32_t fgRGB = ARGB8888_Opaque(FgColor);            // 强制 A=FF（只用 RGB）

    // 快路径：完全在屏内，无需每像素边界判断
    if ((unsigned)X <= (unsigned)(LCD_W - FONT_W) &&
        (unsigned)Y <= (unsigned)(LCD_H - FONT_H))
    {
        for (int row = 0; row < FONT_H; row++) {
            uint32_t *dst = fb + (Y + row) * LCD_W + X;
            const uint8_t *a8 = glyph + row * FONT_W;
            for (int col = 0; col < FONT_W; col++) {
                uint8_t a = a8[col];
                if (!a) continue;
                if (fgA != 255) a = (uint8_t)((uint32_t)a * fgA / 255u);
                dst[col] = BlendOver_ARGB8888(dst[col], fgRGB, a);
            }
        }
    } else {
        // 慢路径：带裁剪
        int start_col = (X < 0) ? -X : 0;
        int start_row = (Y < 0) ? -Y : 0;

        for (int row = start_row; row < FONT_H && (Y + row) < LCD_H; row++) {
            int py = Y + row;
            if (py < 0) continue;

            uint32_t *dst = fb + py * LCD_W;
            const uint8_t *a8 = glyph + row * FONT_W;

            for (int col = start_col; col < FONT_W && (X + col) < LCD_W; col++) {
                int px = X + col;
                if (px < 0) continue;

                uint8_t a = a8[col];
                if (!a) continue;
                if (fgA != 255) a = (uint8_t)((uint32_t)a * fgA / 255u);

                uint32_t *p = &dst[px];
                *p = BlendOver_ARGB8888(*p, fgRGB, a);
            }
        }
    }

    // dirty rect
    DirtyMergeRect(Layer, (uint16_t)draw_x0, (uint16_t)draw_y0, (uint16_t)draw_x1, (uint16_t)draw_y1);
}

/**
 * @brief 绘制字符串（支持 \n 换行）
 *
 * 优化点：如果 BgColor alpha != 0，会按“每行”先 DMA2D 填一次整行背景，
 * 然后每个字符用透明背景绘前景，避免每个字都 Fill 一次。
 */
uint16_t LCD_DrawString(uint8_t Layer, int X, int Y, const char *Str, uint32_t FgColor, uint32_t BgColor)
{
    if (!Str) return 0;

    uint16_t count = 0;
    int cursor_x = X;
    int cursor_y = Y;

    while (*Str) {
        // 处理换行
        if (*Str == '\r') { Str++; continue; }
        if (*Str == '\n') { Str++; cursor_x = X; cursor_y += FONT_H; continue; }

        // 先统计这一行可打印字符数，用于一次性填背景
        if ((BgColor & 0xFF000000u) != 0u) {
            const char *p = Str;
            int n = 0;
            while (*p && *p != '\n' && *p != '\r') {
                uint8_t uc = (uint8_t)*p;
                if (uc >= JBMONO_FIRST && uc <= JBMONO_LAST) n++;
                p++;
            }
            if (n > 0) {
                uint32_t bg = ARGB8888_Opaque(BgColor);
                LCD_DrawRectFilledI32(Layer, cursor_x, cursor_y, n * FONT_ADVANCE_X, FONT_H, bg);

                // dirty（兜底）
                int x0 = cursor_x < 0 ? 0 : cursor_x;
                int y0 = cursor_y < 0 ? 0 : cursor_y;
                int x1 = cursor_x + n * FONT_ADVANCE_X - 1;
                int y1 = cursor_y + FONT_H - 1;
                if (x1 >= LCD_W) x1 = LCD_W - 1;
                if (y1 >= LCD_H) y1 = LCD_H - 1;
                if (x0 < LCD_W && y0 < LCD_H && x1 >= 0 && y1 >= 0) {
                    DirtyMergeRect(Layer, (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
                }
            }
        }

        // 逐字符画前景（把 BgColor 设为透明，避免重复填背景）
        while (*Str && *Str != '\n' && *Str != '\r') {
            char ch = *Str++;
            uint8_t uc = (uint8_t)ch;
            if (uc >= JBMONO_FIRST && uc <= JBMONO_LAST) {
                LCD_DrawChar(Layer, cursor_x, cursor_y, ch, FgColor, 0x00000000u);
                cursor_x += FONT_ADVANCE_X;
                count++;
            }
        }
    }

    return count;
}

/**
 * @brief 绘制整数
 * 修复了 INT32_MIN 取反溢出的问题
 */
uint16_t LCD_DrawInt(uint8_t Layer, int X, int Y, int32_t Value, uint32_t FgColor, uint32_t BgColor)
{
    char buffer[12];

    int64_t v = (int64_t)Value;
    uint32_t u;
    int neg = 0;
    if (v < 0) { neg = 1; u = (uint32_t)(-v); } else { u = (uint32_t)v; }

    int idx = 0;
    if (u == 0) {
        buffer[idx++] = '0';
    } else {
        char tmp[10];
        int t = 0;
        while (u > 0) { tmp[t++] = (char)('0' + (u % 10u)); u /= 10u; }
        if (neg) buffer[idx++] = '-';
        while (t--) buffer[idx++] = tmp[t];
    }
    buffer[idx] = '\0';

    return LCD_DrawString(Layer, X, Y, buffer, FgColor, BgColor);
}

/**
 * @brief 绘制浮点数（简单版：截断，不做四舍五入）
 */
uint16_t LCD_DrawFloat(uint8_t Layer, int X, int Y, float Value, uint8_t Decimals, uint32_t FgColor, uint32_t BgColor)
{
    if (Decimals > 6) Decimals = 6;

    char buffer[32];
    int idx = 0;

    if (Value < 0) { buffer[idx++] = '-'; Value = -Value; }

    int32_t int_part = (int32_t)Value;
    float frac_part = Value - (float)int_part;

    // int_part -> string
    char itmp[12];
    int ilen = 0;
    if (int_part == 0) itmp[ilen++] = '0';
    else {
        int32_t v = int_part;
        while (v > 0) { itmp[ilen++] = (char)('0' + (v % 10)); v /= 10; }
    }
    for (int i = ilen - 1; i >= 0; i--) buffer[idx++] = itmp[i];

    if (Decimals) {
        buffer[idx++] = '.';
        for (uint8_t i = 0; i < Decimals; i++) {
            frac_part *= 10.0f;
            int d = (int)frac_part;
            buffer[idx++] = (char)('0' + d);
            frac_part -= (float)d;
        }
    }

    buffer[idx] = '\0';
    return LCD_DrawString(Layer, X, Y, buffer, FgColor, BgColor);
}



