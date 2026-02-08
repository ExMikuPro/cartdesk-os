#include "touch.h"

#include <string.h>

#include "i2c.h"

static void Touch_Restart(void) {
  HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TOUCH_INT_GPIO_Port, TOUCH_INT_Pin, GPIO_PIN_RESET);
  HAL_Delay(20);
  HAL_GPIO_WritePin(TOUCH_RST_GPIO_Port, TOUCH_RST_Pin, GPIO_PIN_SET);

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = TOUCH_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(TOUCH_INT_GPIO_Port, &GPIO_InitStruct);
}

void Touch_Init(void) {
  Touch_Restart();
}
void Touch_Test(void) {
  uint8_t test[3] = {0x8050 >> 8, 0x8050 & 0xff};
  uint8_t retry = 0;
  int8_t ret = -1;

  while(retry++ < 5)
  {
    ret = GTP_I2C_Read(0xBA, test, 3);
    if (ret > 0)
    {
      break;
    }
  }
}

int32_t I2C_Transfer(struct i2c_msg *msgs, int num) {
  int32_t im = 0;
  int32_t ret = 0;
  for (im = 0; ret == 0 && im != num; im++) {
    if (msgs[im].flags & READ_Bytes) {
      ret = HAL_I2C_Master_Receive(&hi2c2,msgs[im].addr,msgs[im].buf,msgs[im].len,1000);
    } else {
      ret = HAL_I2C_Master_Transmit(&hi2c2,msgs[im].addr,msgs[im].buf,msgs[im].len,1000);
    }
  }
  if (ret) return ret;
  return im;
}

static int32_t GTP_I2C_Read(uint8_t client_addr, uint8_t *buf, int32_t len)
{
  struct i2c_msg msgs[2];
  int32_t ret=-1;
  int32_t retries = 0;

  msgs[0].flags = !0x0001;					//д��
  msgs[0].addr  = client_addr;					//IIC�豸��ַ
  msgs[0].len   = GTP_ADDR_LENGTH;	//�Ĵ�����ַΪ2�ֽ�(��д�����ֽڵ�����)
  msgs[0].buf   = &buf[0];						//buf[0~1]�洢����Ҫ��ȡ�ļĴ�����ַ

  msgs[1].flags = 0x0001;					//��ȡ
  msgs[1].addr  = client_addr;					//IIC�豸��ַ
  msgs[1].len   = len - GTP_ADDR_LENGTH;	//Ҫ��ȡ�����ݳ���
  msgs[1].buf   = &buf[GTP_ADDR_LENGTH];	//buf[GTP_ADDR_LENGTH]֮��Ļ������洢����������

  while(retries < 5)
  {
    ret = I2C_Transfer( msgs, 2);					//����IIC���ݴ�����̺�������2���������
    if(ret == 2)break;
    retries++;
  }
  if((retries >= 5))
  {

  }
  return ret;
}
static int32_t GTP_I2C_Write(uint8_t client_addr,uint8_t *buf,int32_t len)
{
  struct i2c_msg msg;
  int32_t ret = -1;
  int32_t retries = 0;

  /*һ��д���ݵĹ���ֻ��Ҫһ���������:
   * 1. IIC���� д�� ���ݼĴ�����ַ������
   * */
  msg.flags = !0x0001;			//д��
  msg.addr  = client_addr;			//���豸��ַ
  msg.len   = len;							//����ֱ�ӵ���(�Ĵ�����ַ����+д��������ֽ���)
  msg.buf   = buf;						//ֱ������д�뻺�����е�����(�����˼Ĵ�����ַ)

  while(retries < 5)
  {
    ret = I2C_Transfer(&msg, 1);	//����IIC���ݴ�����̺�����1���������
    if (ret == 1)break;
    retries++;
  }
  if((retries >= 5))
  {

  }
  return ret;
}


void Goodix_TS_Work_Func(void)
{
    uint8_t  end_cmd[3] = {0x814E >> 8, 0x814E & 0xFF, 0};
    uint8_t  point_data[2 + 1 + 8 * 5 + 1]={0x814E >> 8, 0x814E & 0xFF};
    uint8_t  touch_num = 0;
    uint8_t  finger = 0;
    static uint16_t pre_touch = 0;
    static uint8_t pre_id[5] = {0};

    uint8_t client_addr=0xBA;
    uint8_t* coor_data = NULL;
    int32_t input_x = 0;
    int32_t input_y = 0;
    int32_t input_w = 0;
    uint8_t id = 0;

    int32_t i  = 0;
    int32_t ret = -1;

    ret = GTP_I2C_Read(client_addr, point_data, 12);//10�ֽڼĴ�����2�ֽڵ�ַ
    if (ret < 0)
    {

        return;
    }

    finger = point_data[GTP_ADDR_LENGTH];//״̬�Ĵ�������

    if (finger == 0x00)		//û�����ݣ��˳�
    {
        return;
    }

    if((finger & 0x80) == 0)//�ж�buffer statusλ
    {
        goto exit_work_func;//����δ������������Ч
    }

    touch_num = finger & 0x0f;//�������
    if (touch_num > 5)
    {
        goto exit_work_func;//�������֧�ֵ����������˳�
    }

    if (touch_num > 1)//��ֹһ����
    {
        uint8_t buf[8 * 5] = {(0x814E + 10) >> 8, (0x814E + 10) & 0xff};

        ret = GTP_I2C_Read(client_addr, buf, 2 + 8 * (touch_num - 1));
        memcpy(&point_data[12], &buf[2], 8 * (touch_num - 1));			//����������������ݵ�point_data
    }



    if (pre_touch>touch_num)				//pre_touch>touch_num,��ʾ�еĵ��ͷ���
    {
        for (i = 0; i < pre_touch; i++)						//һ����һ���㴦��
         {
            uint8_t j;
           for(j=0; j<touch_num; j++)
           {
               coor_data = &point_data[j * 8 + 3];
               id = coor_data[0] & 0x0F;									//track id
              if(pre_id[i] == id)
                break;

              if(j >= touch_num-1)											//������ǰ����id���Ҳ���pre_id[i]����ʾ���ͷ�
              {
                 GTP_Touch_Up( pre_id[i]);
              }
           }
       }
    }


    if (touch_num)
    {
        for (i = 0; i < touch_num; i++)						//һ����һ���㴦��
        {
            coor_data = &point_data[i * 8 + 3];

            id = coor_data[0] & 0x0F;									//track id
            pre_id[i] = id;

            input_x  = coor_data[1] | (coor_data[2] << 8);	//x����
            input_y  = coor_data[3] | (coor_data[4] << 8);	//y����
            input_w  = coor_data[5] | (coor_data[6] << 8);	//size

            {
                GTP_Touch_Down( id, input_x, input_y, input_w);//���ݴ���
            }
        }
    }
    else if (pre_touch)		//touch_ num=0 ��pre_touch��=0
    {
      for(i=0;i<pre_touch;i++)
      {
          GTP_Touch_Up(pre_id[i]);
      }
    }


    pre_touch = touch_num;


exit_work_func:
    {
        ret = GTP_I2C_Write(client_addr, end_cmd, 3);
        if (ret < 0)
        {
            // GTP_INFO("I2C write end_cmd error!");
        }
    }

}

static int16_t pre_x[5] ={-1,-1,-1,-1,-1};
static int16_t pre_y[5] ={-1,-1,-1,-1,-1};

static void GTP_Touch_Up( int32_t id)
{


  /*�������ͷ�,���ڴ�������*/
  // Touch_Button_Up(pre_x[id],pre_y[id]);

  /*****************************************/
  /*�ڴ˴�����Լ��Ĵ������ͷ�ʱ�Ĵ�����̼���*/
  /* pre_x[id],pre_y[id] ��Ϊ���µ��ͷŵ� ****/
  /*******************************************/
  /***idΪ�켣���(��㴥��ʱ�ж�켣)********/


  /*�����ͷţ���pre xy ����Ϊ��*/
  pre_x[id] = -1;
  pre_y[id] = -1;
}
static void GTP_Touch_Down(int32_t id,int32_t x,int32_t y,int32_t w)
{


  /************************************/
  /*�ڴ˴�����Լ��Ĵ����㰴��ʱ������̼���*/
  /* (x,y) ��Ϊ���µĴ����� *************/
  /************************************/

  /*prex,prey����洢��һ�δ�����λ�ã�idΪ�켣���(��㴥��ʱ�ж�켣)*/
  pre_x[id] = x; pre_y[id] =y;

}
