#ifndef CARTDESK_AUDIO_TASK_H
#define CARTDESK_AUDIO_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reserved for the future DMA-driven audio pipeline.
 * The task remains blocked until an audio producer and event contract exist.
 */
void CartdeskAudioTask_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif
