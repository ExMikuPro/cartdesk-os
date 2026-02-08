#pragma once
#include "main.h"

#define READ_Bytes 0x1u
#define WRITE_Bytes 0x1u

#define GTP_ADDR_LENGTH       2

struct i2c_msg {
  uint8_t addr;
  uint16_t flags;
  uint16_t len;
  uint8_t *buf;
};


void Touch_Init(void);
void Touch_Test(void);
static int32_t GTP_I2C_Read(uint8_t client_addr, uint8_t *buf, int32_t len);
int32_t I2C_Transfer(struct i2c_msg *msgs, int num);
static int32_t GTP_I2C_Write(uint8_t client_addr,uint8_t *buf,int32_t len);
void Goodix_TS_Work_Func(void);
static void GTP_Touch_Up( int32_t id);
static void GTP_Touch_Down(int32_t id,int32_t x,int32_t y,int32_t w);