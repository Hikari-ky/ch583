/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2019/11/05
 * Description        : 外设从机应用主函数及任务系统初始化
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "CONFIG.h"
#include "CH58x_common.h"
#include "HAL.h"
#include "GATTprofile.h"
#include "Peripheral.h"
#include "OTA.h"
#include "OTAprofile.h"

/* 记录当前的Image */
unsigned char CurrImageFlag = 0xff;

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4)))  u32 MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if (defined (BLE_MAC)) && (BLE_MAC == TRUE)
u8C MacAddr[6] = { 0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02 };
#endif


/* 注意：关于程序升级后flash的操作必须先执行，不开启任何中断，防止操作中断和失败 */
/*******************************************************************************
 * Function Name  : ReadImageFlag
 * Description    : 读取当前的程序的Image标志，DataFlash如果为空，就默认是ImageA，如果为ImageA，则跳转ImageA
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
void ReadImageFlag( void )
{
  OTADataFlashInfo_t p_image_flash;

  EEPROM_READ( OTA_DATAFLASH_ADD, &p_image_flash, 4 );
  CurrImageFlag = p_image_flash.ImageFlag;

  /* 程序第一次执行，或者没有更新过，以后更新后在擦除DataFlash */
  if ( (CurrImageFlag != IMAGE_B_FLAG) && (CurrImageFlag != IMAGE_A_FLAG) && (CurrImageFlag != IMAGE_OTA_FLAG) )
  {
    CurrImageFlag = IMAGE_A_FLAG;
  }

  PRINT( "Image Flag %02x\n", CurrImageFlag );

  if( CurrImageFlag == IMAGE_A_FLAG )
  {
    PRINT("jump App \n");
    mDelaymS(5);
    jumpApp();
  }
}

/*******************************************************************************
* Function Name  : Main_Circulation
* Description    : 主循环
* Input          : None
* Output         : None
* Return         : None
*******************************************************************************/
__HIGH_CODE
void Main_Circulation()
{
  while(1){
    TMOS_SystemProcess( );
  }
}

/*******************************************************************************
 * Function Name  : main
 * Description    : 主函数
 * Input          : None
 * Output         : None
 * Return         : None
 *******************************************************************************/
int main( void )
{
#if (defined (DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
  PWR_DCDCCfg( ENABLE );
#endif
  SetSysClock( CLK_SOURCE_PLL_60MHz );
#if (defined (HAL_SLEEP)) && (HAL_SLEEP == TRUE)
  GPIOA_ModeCfg( GPIO_Pin_All, GPIO_ModeIN_PU );
  GPIOB_ModeCfg( GPIO_Pin_All, GPIO_ModeIN_PU );
#endif
#ifdef DEBUG
  GPIOA_SetBits( bTXD1 );
  GPIOA_ModeCfg( bTXD1, GPIO_ModeOut_PP_5mA );
  UART1_DefInit();
#endif   
  PRINT( "%s\n", VER_LIB );
  FLASH_ROM_LOCK(0);
  ReadImageFlag();
  if( (R8_RESET_STATUS&RB_RESET_FLAG)==RST_FLAG_SW )
  {
    // 软复位不跳APP
  }
  else
  {
    if( CurrImageFlag == IMAGE_OTA_FLAG )
    {
      PRINT("jump App \n");
      mDelaymS(5);
      jumpApp();
    }
  }
  CH57X_BLEInit();
  HAL_Init();
  GAPRole_PeripheralInit();
  Peripheral_Init();
  Main_Circulation();
}

/******************************** endfile @ main ******************************/
