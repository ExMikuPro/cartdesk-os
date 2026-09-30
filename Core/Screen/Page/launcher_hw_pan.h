#ifndef LAUNCHER_HW_PAN_H
#define LAUNCHER_HW_PAN_H

#include <stdbool.h>
#include <stdint.h>

#include "ltdc_reload.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Launcher horizontal hardware pan 状态机。
 *
 * 目标：Launcher 滚动只改 HW Layer1 的 CFBAR，不再让 LVGL 每帧 rasterize
 * App slot。steady-state 每个 VBlank 只允许：
 *
 *   1. 写一次 Layer1 CFBAR shadow
 *   2. arm 一次 LTDC_RELOAD_OWNER_LAUNCHER_PAN
 *   3. 请求一次 SRCR.VBR
 *
 * 触摸/逻辑更新可能远快于 panel VBlank，因此必须合并：
 * 若上一笔 VBR 尚未完成，新位置只更新 desired_x，不再发第二个 reload。
 * 完成事件到达后，下一次 tick 再提交"当时最新"的 desired_x。
 * 这就是 latest-position-wins：不试图显示每一个输入采样。
 *
 * 本模块是纯逻辑，通过 ops 注入寄存器写入与 VBR 请求，可在 host 上测试。
 */

typedef struct {
    uintptr_t base_addr;      /* strip 起始地址（字节） */
    uint32_t  stride_bytes;   /* 物理 stride（字节） */
    uint32_t  width;          /* 逻辑宽度（像素） */
    uint32_t  viewport_width; /* 可见窗口宽度（像素） */
    uint32_t  scroll_max;     /* 最大水平平移量 = width - viewport_width */
} launcher_hw_pan_geometry_t;

typedef struct {
    /** 写 HW Layer1 的 CFBAR shadow；不得在此请求 VBR */
    void (*write_source_addr)(uintptr_t addr, void *user);
    /** 请求一次 VBlank reload；返回 true 表示 SRCR.VBR 已受理 */
    bool (*request_vbr)(void *user);
    void *user;
} launcher_hw_pan_ops_t;

typedef struct {
    launcher_hw_pan_geometry_t geo;
    launcher_hw_pan_ops_t ops;

    int32_t desired_x;        /* 最新请求位置 */
    int32_t requested_x;      /* 已写入 shadow 的位置 */
    int32_t latched_x;        /* 已由 ReloadEvent 确认生效的位置 */

    uint32_t pending_generation;
    bool pending;
    bool enabled;

    /* 运行计数（steady-state 性能验收口径） */
    uint32_t submits;         /* 实际提交的 VBR 次数 */
    uint32_t coalesced;       /* 因 pending 被合并掉的位置更新 */
    uint32_t vbr_rejected;    /* HAL 拒绝 VBR 的次数 */
    uint32_t completions;     /* 确认完成的次数 */
    uint32_t generation_mismatch; /* 完成事件 generation 不匹配 */
    int32_t  applied_x;       /* 最后一次成功写入 shadow 的位置 */
} launcher_hw_pan_t;

/** 计算给定 scroll_x 的 source 起始地址；越界返回 0 */
uintptr_t launcher_hw_pan_source_addr(const launcher_hw_pan_geometry_t *geo, int32_t scroll_x);

/** 把 scroll_x 夹到 [0, scroll_max] */
int32_t launcher_hw_pan_clamp_x(const launcher_hw_pan_geometry_t *geo, int32_t scroll_x);

/** 初始化（不写寄存器）；latched/desired 复位到 initial_x */
void launcher_hw_pan_init(launcher_hw_pan_t *pan,
                          const launcher_hw_pan_geometry_t *geo,
                          const launcher_hw_pan_ops_t *ops,
                          int32_t initial_x);

/** 使能/停用 pan 提交；停用时清 pending 但保留计数 */
void launcher_hw_pan_set_enabled(launcher_hw_pan_t *pan, bool enabled);

/**
 * @brief  请求显示位置
 * @note   pending 时只更新 desired_x 并计入 coalesced，不再次提交 VBR
 * @retval true=本次立即提交了 VBR, false=被合并/被停用/位置未变化
 */
bool launcher_hw_pan_set_x(launcher_hw_pan_t *pan, int32_t x);

/**
 * @brief  app task 周期调用：提交尚未生效的最新位置
 * @retval true=提交了新的 VBR
 * @note   只有 desired_x != latched_x 且没有 pending 时才会提交
 */
bool launcher_hw_pan_tick(launcher_hw_pan_t *pan);

/**
 * @brief  ReloadEvent(ISR) 上下文确认完成
 * @param  generation ltdc_reload_complete_from_irq 返回时对应的 generation
 * @note   只做状态迁移与计数，ISR 安全
 */
void launcher_hw_pan_on_reload_complete(launcher_hw_pan_t *pan, uint32_t generation);

/** 是否处于 steady state：无 pending 且已显示最新位置 */
bool launcher_hw_pan_is_settled(const launcher_hw_pan_t *pan);

/** 当前应显示的 source 地址（latched_x） */
uintptr_t launcher_hw_pan_latched_addr(const launcher_hw_pan_t *pan);

/** 几何是否自洽（stride 覆盖逻辑行、scroll_max 与宽度一致） */
bool launcher_hw_pan_geometry_is_valid(const launcher_hw_pan_geometry_t *geo);

#ifdef __cplusplus
}
#endif

#endif /* LAUNCHER_HW_PAN_H */
