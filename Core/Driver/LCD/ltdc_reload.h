#ifndef LTDC_RELOAD_H
#define LTDC_RELOAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LTDC VBlank reload (VBR) 所有权登记。
 *
 * LTDC 只有一个 SRCR.VBR 请求位和一次 RR 完成事件，但生产路径上有多个互相
 * 独立的提交者：
 *
 *   - LVGL DIRECT flush：切换 HW Layer1 CFBAR 到新的 render buffer
 *   - Launcher hardware pan：只平移 HW Layer1 的 source 起始地址
 *   - Layer0 static swap：低频切换固定 UI 的 front/back
 *   - Mode switch：一次性 atomic latch 整套 layer 配置
 *
 * 如果不在 arm 时登记 owner，一次 Launcher pan 的 ReloadEvent 会被
 * HAL_LTDC_ReloadEventCallback 无条件当成 LVGL flush 完成，从而确认一个
 * 根本不存在的 LVGL frame（帧所有权串线）。
 *
 * 因此约定：任何 VBR 请求在写 SRCR 之前必须先 ltdc_reload_arm(owner)，
 * 完成事件由 ltdc_reload_complete_from_irq() 归还给登记的 owner，再由调用方
 * 分派。本模块是纯逻辑，不依赖 HAL，可在 host 上直接测试。
 */

typedef enum {
    LTDC_RELOAD_OWNER_NONE = 0,
    LTDC_RELOAD_OWNER_LVGL_FLUSH,
    LTDC_RELOAD_OWNER_LAUNCHER_PAN,
    LTDC_RELOAD_OWNER_LAYER0_SWAP,
    LTDC_RELOAD_OWNER_MODE_SWITCH,
    LTDC_RELOAD_OWNER_COUNT
} ltdc_reload_owner_t;

typedef struct {
    ltdc_reload_owner_t pending_owner;   /* 已 arm 尚未完成 */
    uint32_t pending_generation;
    ltdc_reload_owner_t last_owner;      /* 最近一次完成 */
    uint32_t last_generation;
    uint32_t arming_count;               /* arm 次数 */
    uint32_t completion_count;           /* 完成事件次数 */
    uint32_t orphan_completions;         /* 没有 pending 却收到 RR */
    uint32_t overrun_count;              /* pending 未完成时再次 arm */
    uint32_t owner_completions[LTDC_RELOAD_OWNER_COUNT];
    uint32_t owner_arms[LTDC_RELOAD_OWNER_COUNT];
    uint32_t crosstalk_count;            /* 完成归还给了错误 owner */
    bool initialized;
} ltdc_reload_state_t;

/** 复位登记表（启动时调用一次） */
void ltdc_reload_init(void);

/**
 * @brief  登记一次即将提交的 VBlank reload
 * @param  owner  本次 VBR 的提交者；不能是 LTDC_RELOAD_OWNER_NONE
 * @return 本次请求的 generation；owner 非法时返回 0 且不改变状态
 * @note   必须在临界区内、写 LTDC SRCR 之前调用，保证 ISR 看到一致的
 *         owner/generation 组合
 */
uint32_t ltdc_reload_arm(ltdc_reload_owner_t owner);

/**
 * @brief  在 LTDC RR 中断里归还完成事件
 * @return 本次完成事件所属的 owner；无 pending 时返回 LTDC_RELOAD_OWNER_NONE
 * @note   只做状态迁移与计数，不做任何阻塞或复杂调用，ISR 安全
 */
ltdc_reload_owner_t ltdc_reload_complete_from_irq(void);

/**
 * @brief  放弃当前 pending（提交失败或切换取消）
 * @param  owner  必须是当前 pending owner，否则计数 crosstalk 且不改变状态
 * @retval true=已放弃, false=owner 不匹配或没有 pending
 */
bool ltdc_reload_abandon(ltdc_reload_owner_t owner);

/** 当前是否有未完成的 reload */
bool ltdc_reload_is_pending(void);

/** 当前 pending 的 owner；无 pending 时返回 LTDC_RELOAD_OWNER_NONE */
ltdc_reload_owner_t ltdc_reload_pending_owner(void);

/** 当前 pending 的 generation；无 pending 时返回 0 */
uint32_t ltdc_reload_pending_generation(void);

/** 只读快照，供 dump / host test 使用 */
const ltdc_reload_state_t *ltdc_reload_state(void);

/** owner 名字（trace/dump 用），非法输入返回 "?" */
const char *ltdc_reload_owner_name(ltdc_reload_owner_t owner);

/**
 * @brief  校验 owner 是否从未串线
 * @retval true=无 orphan 完成、无 overrun、无 crosstalk
 */
bool ltdc_reload_is_consistent(void);

/*
 * 外部 owner 分派钩子。
 *
 * LTDC RR 中断只能有一个入口（HAL_LTDC_ReloadEventCallback）。LVGL flush 的
 * 完成回调在 drv_display 内部，可以直接调用；但 Launcher pan / Layer0 swap /
 * mode switch 的消费者在上层 app_screen，drv_display 不能反向链接它。
 * 因此这里提供一个注册点：非 LVGL_FLUSH 的完成事件由注册的回调接手。
 */
typedef void (*ltdc_reload_external_cb_t)(ltdc_reload_owner_t owner,
                                          uint32_t generation,
                                          void *user);

/** 注册/注销外部 owner 分派回调（NULL 表示注销） */
void ltdc_reload_set_external_dispatch(ltdc_reload_external_cb_t cb, void *user);

/**
 * @brief  把一次完成事件转交给外部回调
 * @retval true=有回调接手, false=owner 非法或尚未注册回调
 * @note   尚未注册回调只计入 unhandled_external（启动期预期），不视为错误
 */
bool ltdc_reload_notify_external(ltdc_reload_owner_t owner, uint32_t generation);

/** 尚无消费者接手的外部完成事件数（信息性，不参与一致性判定） */
uint32_t ltdc_reload_unhandled_external(void);

#ifdef __cplusplus
}
#endif

#endif /* LTDC_RELOAD_H */
