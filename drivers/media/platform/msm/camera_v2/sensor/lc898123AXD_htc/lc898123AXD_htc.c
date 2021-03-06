/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "../msm_sensor.h"

#include "lc898123AXD_htc.h"
#include "Ois.h"
#include "FromCode.h"	
#include "md5.h"
#include "OisLc898123AXD.h"
#include "PmemCode.h"

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>


static struct msm_sensor_ctrl_t *g_s_ctrl = NULL;
static struct GYRO_gpio_info *g_GYRO_info = NULL;
#define GYRO_CAL_SIZE 2048
#define		INT16	signed short
#define		INT32	long
#define		INT64	long long
#define		UINT8	unsigned char
#define		UINT16	unsigned short
#define		UINT32	unsigned int
#define		UINT64	unsigned long long

#define VERIFY_SIZE_CRC	4
#define VERIFY_SIZE_MD5	16

unsigned char NVR0_Backup[256];
unsigned char  FLASH_SECTOR_BUFFER[256];
unsigned char  NVR2_Backup[256];

unsigned int CRC_Reg = 0x0000ffff;

void msm_fclose(struct file* file) {
    filp_close(file, NULL);
}

int msm_fwrite(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

struct file* msm_fopen(const char* path, int flags, int rights) {
    struct file* filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if(IS_ERR(filp)) {
        err = PTR_ERR(filp);
    pr_err("[CAM]File Open Error:%s",path);
        return NULL;
    }
    if(!filp->f_op){
    pr_err("[CAM]File Operation Method Error!!");
    return NULL;
    }

    return filp;
}


static int GYRO_Cali_init (struct msm_sensor_ctrl_t *s_ctrl)
{
    pr_info("[OIS_Cali]%s:E\n", __func__);

    g_GYRO_info = kzalloc(sizeof(struct GYRO_gpio_info), GFP_ATOMIC);
    g_GYRO_info->flash_rw = of_get_named_gpio((&s_ctrl->pdev->dev)->of_node,"flash_rw",0);
    pr_info("[OIS_Cali]flash_rw %d\n", g_GYRO_info->flash_rw);
	if (g_GYRO_info->flash_rw < 0) {
		pr_err("[OIS_Cali]%s:%d flash_rw rc %d\n", __func__, __LINE__, g_GYRO_info->flash_rw);
	}
    return 0;
}

int GYRO_Cali_release(void)
{
	pr_info("[OIS_Cali]%s\n", __func__);

	kfree(g_GYRO_info);

	return 0;
}

unsigned char	htc_RdStatus( unsigned char UcStBitChk )
{
	unsigned int	UlReadVal ;

	RamRead32A( CMD_READ_STATUS , &UlReadVal );
	if( UcStBitChk ){
		UlReadVal &= READ_STATUS_INI ;
	}
	if( !UlReadVal ){
		return( SUCCESS );
	}else{
		return( FAILURE );
	}
}
#define		read_FW					0x8000

unsigned char htc_GyroReCalib(struct msm_sensor_ctrl_t *s_ctrl, int cam_id)
{
	unsigned char	UcSndDat = 1 ;
	unsigned int	UlRcvDat;
	unsigned int	UlFWDat;
	unsigned int	UlFctryX, UlFctryY;
	unsigned int	UlCurrX, UlCurrY;
	unsigned int	UlGofX, UlGofY ;
	stReCalib pReCalib = {0};
    int rc = 0;
    struct file*fp = NULL;
    uint8_t *m_path= "/data/misc/camera/GYRO_main_result.txt";
    uint8_t *f_path= "/data/misc/camera/GYRO_front_result.txt";
    char gyro_mem[1024];
    int count = 0;
    g_s_ctrl = s_ctrl;
	pr_info("[OIS_Cali]%s:E\n", __func__);

    if (g_s_ctrl == NULL)
        return -1;
    WitTim(100);

    GYRO_Cali_init(s_ctrl);

	rc = RamWrite32A( CMD_CALIBRATION , 0x00000000 ) ;

	RamRead32A(0x8000,&UlFWDat );
	pr_info("[OIS_Cali]%s FW Ver = %x\n", __func__, UlFWDat);

	if (rc != 0)
	{
        pr_info("[OIS_Cali]%s: RamWrite32A = %d return -1\n",__func__, rc);
        return -1;
	}

	while( UcSndDat ) {
		UcSndDat = htc_RdStatus(1);
	}
	RamRead32A( CMD_CALIBRATION , &UlRcvDat ) ;
	UcSndDat = (unsigned char)(UlRcvDat >> 24);

	
	FlashNVR_ReadData_ByteA( CALIBRATION_DATA_ADDRESS, FLASH_SECTOR_BUFFER, 256	);

	_GET_UINT32( UlCurrX,											GYRO_OFFSET_VALUE_X ) ;
	_GET_UINT32( UlCurrY,											GYRO_OFFSET_VALUE_Y ) ;
	_GET_UINT32( UlFctryX,											GYRO_OFFSET_FCTRY_X ) ;
	_GET_UINT32( UlFctryY,											GYRO_OFFSET_FCTRY_Y ) ;
	if( UlFctryX == 0xFFFFFFFF )
		pReCalib.SsFctryOffX = (UlCurrX >> 16) ;
	else
		pReCalib.SsFctryOffX = (UlFctryX >> 16) ;

	if( UlFctryY == 0xFFFFFFFF )
		pReCalib.SsFctryOffY = (UlCurrY >> 16) ;
	else
		pReCalib.SsFctryOffY = (UlFctryY >> 16) ;

	RamRead32A(  GYRO_RAM_GXOFFZ , &UlGofX ) ;
	RamRead32A(  GYRO_RAM_GYOFFZ , &UlGofY ) ;

	pReCalib.SsRecalOffX = (UlGofX >> 16) ;
	pReCalib.SsRecalOffY = (UlGofY >> 16) ;
	pReCalib.SsDiffX = ((short)pReCalib.SsFctryOffX - (short)pReCalib.SsRecalOffX) > 0 ?  ((short)pReCalib.SsFctryOffX - (short)pReCalib.SsRecalOffX) : ((short)pReCalib.SsRecalOffX - (short)pReCalib.SsFctryOffX);
	pReCalib.SsDiffY = ((short)pReCalib.SsFctryOffY - (short)pReCalib.SsRecalOffY) > 0 ?  ((short)pReCalib.SsFctryOffY - (short)pReCalib.SsRecalOffY) : ((short)pReCalib.SsRecalOffY - (short)pReCalib.SsFctryOffY);
    pr_info("[OIS_Cali]%s: %u, pReCalib->SsDiffX = %d (%#x), pReCalib->SsDiffY = %d (%#x)\n", __func__, UcSndDat, pReCalib.SsDiffX, pReCalib.SsDiffX, pReCalib.SsDiffY, pReCalib.SsDiffY);

    if (cam_id == 0)
    {
        fp=msm_fopen (m_path, O_CREAT|O_RDWR|O_TRUNC, 0666);
    } else if (cam_id == 1)
    {
        fp=msm_fopen (f_path, O_CREAT|O_RDWR|O_TRUNC, 0666);
    }else
        pr_info("Can't write result.\n");

    if (fp != NULL)
    {
        count += sprintf(gyro_mem + count,"UcSndDat : %u \n", UcSndDat);
        count += sprintf(gyro_mem + count,"FW Ver = %x \n", UlFWDat);
        count += sprintf(gyro_mem + count,"pReCalib->SsFctryOffX = %d (%#x), pReCalib->SsFctryOffY = %d (%#x) \n", pReCalib.SsFctryOffX, pReCalib.SsFctryOffX, pReCalib.SsFctryOffY, pReCalib.SsFctryOffY);
        count += sprintf(gyro_mem + count,"pReCalib->SsRecalOffX = %d (%#x), pReCalib->SsRecalOffY = %d (%#x) \n", pReCalib.SsRecalOffX, pReCalib.SsRecalOffX, pReCalib.SsRecalOffY, pReCalib.SsRecalOffY);
        count += sprintf(gyro_mem + count,"pReCalib->SsDiffX = %d (%#x), pReCalib->SsDiffY = %d (%#x) \n", pReCalib.SsDiffX, pReCalib.SsDiffX, pReCalib.SsDiffY, pReCalib.SsDiffY);
        msm_fwrite (fp, 0, gyro_mem, strlen(gyro_mem)+1);
        msm_fclose (fp);
    }else
        pr_info("Can't write result.\n");

	if (UcSndDat != 0)
	{
        GYRO_Cali_release();
		return (int)UcSndDat;
	}else if(pReCalib.SsDiffX >= 0x226 || pReCalib.SsDiffY >= 0x226)
	{
	    pr_info("[OIS_Cali]%s:Threadhold check failed.\n", __func__);
        GYRO_Cali_release();
		return -1;
	}
	else
		return (int)UcSndDat;
}

short htc_WrGyroOffsetData( void )
{
	unsigned int	UlFctryX, UlFctryY;
	unsigned int	UlCurrX, UlCurrY;
	unsigned int	UlGofX, UlGofY;
	unsigned short iRetVal = 0;
    int rc = 0;
    pr_info("[OIS_Cali]%s: E\n", __func__);
	rc = RamRead32A(  GYRO_RAM_GXOFFZ , &UlGofX ) ;
	rc = RamWrite32A( StCaliData_SiGyroOffset_X ,	UlGofX ) ;
	
	rc = RamRead32A(  GYRO_RAM_GYOFFZ , &UlGofY ) ;
	rc = RamWrite32A( StCaliData_SiGyroOffset_Y ,	UlGofY ) ;

	if (rc != 0)
	{
        pr_info("[OIS_Cali]%s i2c read/write fail. return -1\n", __func__);
        return -1;
	}

	
	
	
	
	
	
	iRetVal = Calibration_VerifyUpdate_PreRead();
	if( iRetVal != 0 )
    {
        pr_info("[OIS_Cali]%s: Calibration_VerifyUpdate_PreRead failed. iRetVal = %d\n", __func__, iRetVal);
        GYRO_Cali_release();
        return( iRetVal );
    }

	_GET_UINT32( UlCurrX,											GYRO_OFFSET_VALUE_X ) ;
	_GET_UINT32( UlCurrY,											GYRO_OFFSET_VALUE_Y ) ;
	_GET_UINT32( UlFctryX,											GYRO_OFFSET_FCTRY_X ) ;
	_GET_UINT32( UlFctryY,											GYRO_OFFSET_FCTRY_Y ) ;
	if( UlFctryX == 0xFFFFFFFF )
		_PUT_UINT32( UlCurrX,										GYRO_OFFSET_FCTRY_X	) ;

	if( UlFctryY == 0xFFFFFFFF )
		_PUT_UINT32( UlCurrY,										GYRO_OFFSET_FCTRY_Y	) ;

	_PUT_UINT32( UlGofX,											GYRO_OFFSET_VALUE_X	) ;
	_PUT_UINT32( UlGofY,											GYRO_OFFSET_VALUE_Y	) ;


	
	iRetVal = Calibration_VerifyUpdate();

    pr_info("[OIS_Cali]%s: X  iRetVal = %d\n", __func__, iRetVal);
    GYRO_Cali_release();
	return iRetVal;
}


signed short FlashWrite_NVRVerify( void )
{
    #if 0
	UINT16 UsNum;
	UINT8 UcNvrData[2];
	UINT32 UlReadVal[4];	
	UINT8 UcCnt;
    #else
    unsigned short UsNum;
	unsigned char UcNvrData[2];
	unsigned int UlReadVal[4];	
	unsigned char UcCnt;
    #endif
    md5_context ctx;
	CRC_Reg = 0x0000ffff;
	
	
	FlashResetRelease();		
	
	FlashAutoConfig();
	
	IOWrite32A( FLASHROM_TPGS, 118 );			
	IOWrite32A( FLASHROM_TPROG , 70 );			
	IOWrite32A( FLASHROM_TERASES , 92 );		
	IOWrite32A( FLASHROM_ADR , 0x00010000 );	
	IOWrite32A( FLASHROM_ACSCNT , (256 - 1) );	
	IOWrite32A( FLASHROM_CMD , 1 );				
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UsNum = 0; UsNum <= 0xFF;  UsNum+=4 )
	{
		IORead4times32A( UlReadVal )  ;
		for (UcCnt= 0 ; UcCnt< 4 ; UcCnt++){
			NVR0_Backup[UsNum + UcCnt]   = (unsigned char)UlReadVal[UcCnt];
			FLASH_SECTOR_BUFFER[UsNum + UcCnt]	     = (unsigned char)(UlReadVal[UcCnt]>>8);
			NVR2_Backup[UsNum + UcCnt]   = (unsigned char)(UlReadVal[UcCnt]>>16);
		}
	}
	
	IOWrite32A( FLASHROM_WPB , 1 );							
	WPBCtrl(WPB_OFF) ;										
	if ( ReadWPB() != 1 ){									
		WPBCtrl(WPB_ON) ;									
		FlashReset();
		return ( 5 );
	}		

	IOWrite32A( FLASHROM_ADR , 0x00010000 );				
	IOWrite32A( FLASHROM_SEL , 0x04  );				
	IOWrite32A( FLASHROM_CMD , 4 );							
	
	
	for ( UsNum = 0; UsNum <= 0xFF; UsNum++ )	
	{
		
		UcNvrData[0] = NVR0_Backup[ UsNum ];	
		UcNvrData[1] = FLASH_SECTOR_BUFFER[ UsNum ];		
		CRC16_main( UcNvrData, 2 );
	}
	NVR2_Backup[ 0x22 ] = (unsigned char)(CRC_Reg>>8);
	NVR2_Backup[ 0x23 ] = (unsigned char)CRC_Reg;

	md5_starts( &ctx );
	for ( UsNum = 0; UsNum <= 0xFF; UsNum++ )	
	{
		
		UcNvrData[0] = FLASH_SECTOR_BUFFER[ UsNum ];		
		UcNvrData[1] = NVR0_Backup[ UsNum ];	
		md5_update( &ctx, UcNvrData, 2);
	}
	md5_finish( &ctx, &(NVR2_Backup[ 0x10 ]) );
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_INT ) ;	
	for ( UsNum  = 0; UsNum  < 10; UsNum ++ )						
	{
		RamRead32A(  CMD_IO_DAT_ACCESS, UlReadVal ) ;
		if( !(UlReadVal[0] ==  0x80) ){
			break;
		}
		WitTim( 2 );
	}
	IOWrite32A( FLASHROM_ACSCNT , 0 );						
	IOWrite32A( FLASHROM_ADR , 0x00010000 );				
	for ( UsNum = 0; UsNum <= 0x24; UsNum++ )				
	{
		IOWriteDouble32A( FLASHROM_WDAT, ((UINT32)(NVR2_Backup[UsNum])<<16),
						  FLASHROM_CMD,   2 );	
		
	}
	IOWrite32A( FLASHROM_WPB, 0  );							
	WPBCtrl(WPB_ON) ;										
	IOWrite32A( FLASHROM_ADR, 0x00010000  );				
	IOWrite32A( FLASHROM_ACSCNT, (0x24 -1)  );				
	IOWrite32A( FLASHROM_CMD , 1  );						

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UsNum = 0; UsNum <= (0x24-1); UsNum+=4 )
	{
		IORead4times32A( UlReadVal )  ;
		for ( UcCnt= 0; UcCnt< 4; UcCnt++ )
		{
			if ( (unsigned char)(UlReadVal[ UcCnt ]  >>16 ) != NVR2_Backup[UsNum + UcCnt] ){	FlashReset();	return(-1); }
		}
	}
	
	FlashReset();
		
	return ( 0 );
}

unsigned char PmemWriteByBoot( void )
{
	#if 0
	UINT8  UcCnt;
	UINT32 UlCnt;
	UINT32 UlNum;
	#else
    unsigned char  UcCnt;
	unsigned int UlCnt;
	unsigned int UlNum;
	#endif
#ifdef _Upload_Ver2_
	RamWrite32A( PMEMCMD + 4,  20 );
	for ( UlCnt = 0, UlNum = 0x80000; UlCnt < (sizeof (ContinuouslyTranslationCode))/4 ; UlCnt+=5, UlNum += 8 )
	{
	
    #if 0
	UINT32 UlData[5];
    #else
    unsigned int UlData[5];
    #endif
    
		RamWrite32A( PMEMCMD + 8, UlNum );
		RamWrite32A( PMEMCMD + 12       ,  ContinuouslyTranslationCode[0+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 4	,  ContinuouslyTranslationCode[1+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 8	,  ContinuouslyTranslationCode[2+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 12	,  ContinuouslyTranslationCode[3+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 16	,  ContinuouslyTranslationCode[4+ UlCnt] );
		RamWrite32A( PMEMCMD, 1 );
#if 1	
		RamWrite32A( PMEMCMD, 2 );
		RamRead32A( PMEMCMD + 12 		,	&(UlData[0]) );
		RamRead32A( PMEMCMD + 12 + 4 	,	&(UlData[1]) );
		RamRead32A( PMEMCMD + 12 + 8 	,	&(UlData[2]) );
		RamRead32A( PMEMCMD + 12 + 12	,	&(UlData[3]) );
		RamRead32A( PMEMCMD + 12 + 16	,	&(UlData[4]) );
		for (UcCnt=0; UcCnt<5; UcCnt++){
			if( ContinuouslyTranslationCode[UcCnt+UlCnt] != UlData[UcCnt] )	return(2);	
		}
#endif
	}
#else
    
	#if 0
	UINT8 UcBuf[16];
	#else
    unsigned char UcBuf[16];
	#endif

	RamWrite32A( PMEMCMD + 4,  20 );
	for ( UlCnt = 0, UlNum = 0x81280; UlCnt < (sizeof (ExtraPmemCode)/4) ; UlCnt+=5, UlNum += 8 )
	{
		RamWrite32A( PMEMCMD + 8, UlNum );
		RamWrite32A( PMEMCMD + 12       ,  ExtraPmemCode[0+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 4	,  ExtraPmemCode[1+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 8	,  ExtraPmemCode[2+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 12	,  ExtraPmemCode[3+ UlCnt] );
		RamWrite32A( PMEMCMD + 12 + 16	,  ExtraPmemCode[4+ UlCnt] );
		RamWrite32A( PMEMCMD, 1 );
	}
	
	RamWrite32A( CmdEventCommand, 0x081280 ) ;		

	for ( UlCnt = 0; UlCnt <=  sizeof (ContinuouslyTranslationCode) ; UlCnt+=15 )
	{
		UcBuf[0] = 0xEE;
		for (UcCnt=0; UcCnt<15; UcCnt++){
			UcBuf[ UcCnt +1 ] = ContinuouslyTranslationCode[ UcCnt+UlCnt ];
		}
		CntWrt( UcBuf, 16 ) ;
	}

	UcBuf[0] = 0xEF;
    
    #if 1
    UcBuf[1] = 0x00;
    CntWrt( UcBuf, 2 );
    #else
	CntWrt( UcBuf, 1 ) ;				
	#endif
	
#endif
	return(0);
}

signed short FlashWrite_CalibID( const unsigned int CalibId )
{
	signed short iRetVal;

	
	
	
	
	
	
	iRetVal = Calibration_VerifyUpdate_PreRead();

	if( iRetVal != 0 ) return( iRetVal );

	_PUT_UINT32( CalibId,					CALIBRATION_ID	) ;

	iRetVal = Calibration_VerifyUpdate();
	return iRetVal;
}

unsigned char FlashUpdateMain( const unsigned char* NVRUploadTable, const unsigned long* MainUploadTable, const unsigned char* CRCParity, const unsigned char* MD5Parity, const unsigned int CalibId)
{
	#if 0
	UINT32 UlNum;
	UINT32 UlReadVal; 
	UINT8  UcData[ 64 ] ;
	UINT8  UcCnt;
	#else
	unsigned int UlNum;
	unsigned int UlReadVal; 
	unsigned char  UcData[ 64 ] ;
	unsigned char  UcCnt;
#endif
pr_info("[OIS_Cali]%s:0. Start up to boot exection.\n", __func__);

	
	FlashResetRelease();				
	
	FlashAutoConfig();

	IORead32A( SYS_DSP_REMAP	, &UlReadVal ) ;
	if( UlReadVal != 0) {
		
		IOWrite32A( FLASHROM_TPGS	, 0x73 ) ;					
		IOWrite32A( FLASHROM_TPROG	, 0x43 ) ;					

		WPBCtrl(WPB_OFF) ;								
		if ( ReadWPB() != 1 )
		{
			WPBCtrl(WPB_ON) ;									
			return ( 5 );										
		}

		
		IOWrite32A( FLASHROM_ADR	, 0x00010001) ;				
		IOWrite32A( FLASHROM_ACSCNT	, 0 ) ;						
		IOWrite32A( FLASHROM_SEL	, 1 ) ;						
		IOWrite32A( FLASHROM_WDAT	, 0 ) ;						
		IOWrite32A( FLASHROM_CMD	, 2 ) ;						
		
		IOWrite32A( FLASHROM_WPB	, 0 ) ;						
		WPBCtrl(WPB_ON) ;										

		RamWrite32A(CMD_REBOOT , 0 ) ;							
		UcCnt = 0;
		do{		
			WitTim( 40 );		
			
			IORead32A( SYS_DSP_REMAP	, &UlReadVal ) ;	 	
			if(++UcCnt >= 10 )		return ( 1 );				
		}while(UlReadVal != 0);
	}
pr_info("[OIS_Cali]%s:1. PMEM code donload for continuously I2C translation.\n", __func__);

	
	IORead32A( CVER_123	, &UlReadVal) ;

#ifdef __OIS_TYPE_XC__					
	if ( (unsigned char)UlReadVal== 0xB3 || (unsigned char)UlReadVal == 0xB5 )
#else
	if ( (unsigned char)UlReadVal== 0xB4 || (unsigned char)UlReadVal == 0xB6 )
#endif
	{
		if( PmemWriteByBoot() != 0) return (2);
	}else{
		return (4);
	}

pr_info("[OIS_Cali]%s:2. PMEM Execute & Full Erase.\n", __func__);

	WPBCtrl(WPB_OFF) ;				 						
	if ( ReadWPB() != 1 )
	{
		WPBCtrl(WPB_ON) ;									
		return ( 5 );										
	}

	
	RamWrite32A( CMD_REMAP, 0 ) ;								
	
	UcCnt = 0;
	do{		
		WitTim( 40 );
		CntRd( 0xE0, UcData, 1 ) ;						
		if(++UcCnt >= 4 ){
			
			UcData[ 0 ] = 0xE0;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return ( 6 );		
		}
	}while(UcData[0] == 0);
pr_info("[OIS_Cali]%s:3. Updata Main Code translation.\n", __func__);

	
	for( UlNum=0; UlNum< 4096 ; UlNum += 32 ){ 
		UcData[ 0 ] = 0xE1;		
		for(UcCnt = 0; UcCnt < 16; UcCnt++ ){
			UcData[ (UcCnt * 3) + 1] = (unsigned char)(MainUploadTable[ UcCnt + UlNum ]  );
			UcData[ (UcCnt * 3) + 2] = (unsigned char)(MainUploadTable[ UcCnt + UlNum ] >> 8 );
			UcData[ (UcCnt * 3) + 3] = (unsigned char)(MainUploadTable[ UcCnt + UlNum ] >>16);
		}	
		CntWrt( UcData, 49 ) ;

		UcData[ 0 ] = 0xE2;		
		for(UcCnt = 0; UcCnt < 16; UcCnt++ ){
			UcData[ (UcCnt * 3) + 1] = (unsigned char)(MainUploadTable[ UcCnt + UlNum +16 ]  );
			UcData[ (UcCnt * 3) + 2] = (unsigned char)(MainUploadTable[ UcCnt + UlNum +16 ] >> 8 );
			UcData[ (UcCnt * 3) + 3] = (unsigned char)(MainUploadTable[ UcCnt + UlNum +16 ] >>16);
		}	
		CntWrt( UcData, 49 ) ;
	}
	
	UcCnt = 0;
	do{		
		WitTim( 1 );
		CntRd( 0xE2, UcData, 1 ) ;						
		if(++UcCnt >= 3 ){
			
			UcData[ 0 ] = 0xE5;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return ( 6 );			
		}
	}while(UcData[0] != 0);
pr_info("[OIS_Cali]%s:4. Update NVR Data translation.\n", __func__);

	
	UcData[ 0 ] = 0xE3;		
	for(UcCnt = 0; UcCnt < 32; UcCnt++ ){
		UcData[ UcCnt+ 1] = NVRUploadTable[ UcCnt ] ;
	}
	CntWrt( UcData, 33 ) ;
	UcCnt = 0;
	do{		
		WitTim( 1 );
		CntRd( 0xE3, UcData, 1 ) ;						
		if(++UcCnt >= 3 ){
			
			UcData[ 0 ] = 0x17;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return ( 6 );			
		}
	}while(UcData[0] != 0);
pr_info("[OIS_Cali]%s:5. Verify execute.\n", __func__);

	
	UcData[ 0 ] = 0xE4;		
	UcData[ 1 ] = 0x01;
	for(UcCnt = 0; UcCnt < 16 ; UcCnt++ ){
		UcData[ UcCnt+2 ] = MD5Parity[ UcCnt ];
	}
	CntWrt( UcData, 18 ) ;	

	UcCnt = 0;
	do{		
		WitTim( 20 );
		CntRd( 0xE4, UcData, 1 ) ;						
		if(++UcCnt >= 8 )
		{
			
			UcData[ 0 ] = 0xE5;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return ( 6 );			
		}
	}while(UcData[0] != 0);
pr_info("[OIS_Cali]%s:6. Verify Check.\n", __func__);

	WPBCtrl(WPB_ON) ;										

	
    
    #if 0
    
	CntRd( 0xE5, UcData, VERIFY_SIZE_CRC + VERIFY_SIZE_MD5 ) ;
    
    #else
    CntRd( 0xE5, UcData, VERIFY_SIZE_CRC ) ;
    #endif
    
	for(UcCnt = 0; UcCnt < VERIFY_SIZE_CRC; UcCnt++ ){
		if(UcData[UcCnt] != CRCParity[UcCnt]){
			
			UcData[ 0 ] = 0xE5;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return( 3 );	
		}
	}
    
#if 0
    
	for(UcCnt = 0; UcCnt < VERIFY_SIZE_MD5; UcCnt++ ){
		if(UcData[UcCnt+ VERIFY_SIZE_CRC] != MD5Parity[UcCnt]){
			
			UcData[ 0 ] = 0xE5;		
			UcData[ 1 ] = 0x01;
			CntWrt( UcData, 2 ) ;
			return( 3 );	
		}
	}
    
#endif
    


	
	UcData[ 0 ] = 0xE5;		
	UcData[ 1 ] = 0x00;
	CntWrt( UcData, 2 ) ;
pr_info("[OIS_Cali]%s:7. Update NVR MD5.\n", __func__);

	UcCnt = 0;
	do{		
		WitTim( 40 );		
		
		IORead32A( SYS_DSP_REMAP	, &UlReadVal ) ;	 	
		if(++UcCnt >= 10 )		return ( 1 );				
	}while(UlReadVal == 0);
	FlashWrite_CalibID( CalibId );

	return ( FlashWrite_NVRVerify() );

}

#define REPEAT_NUM 1

signed short FlashUpdateM(void)
{
	
#if 0
	UINT8 UcCnt;
	UINT8 UcAns;
#else
	unsigned char UcCnt;
	unsigned char UcAns;
#endif
    

	for(UcCnt = 0; UcCnt < REPEAT_NUM; UcCnt++ ){
		UcAns = FlashUpdateMain( CcMagicCode_M, ClFromCode_M, CcCRC16Code_M, CcHashCode_M, CALID_M );
		if( UcAns == 0 ){
			break;
		}else{
			
			WPBCtrl(WPB_ON) ;
			FlashReset();	
			WitTim( 50 );
		}
	}
	return ( (signed short)UcAns );
}

signed short FlashUpdateF(void)
{
#if 0
	UINT8 UcCnt;
	UINT8 UcAns;
#else
	unsigned char UcCnt;
	unsigned char UcAns;
#endif
    

	for(UcCnt = 0; UcCnt < REPEAT_NUM; UcCnt++ ){
		UcAns = FlashUpdateMain( CcMagicCode_F, ClFromCode_F, CcCRC16Code_F, CcHashCode_F, CALID_F );
		if( UcAns == 0 ){
			break;
		}else{
			
			WPBCtrl(WPB_ON) ;
			FlashReset();	
			WitTim( 50 );
		}
	}
	return ( (signed short)UcAns );
}

#define		VERNUM_2		0x0305028B

#define		BASEVWNUM_M		0x0009
#define		BASEVWNUM_F		0x000B

int htc_checkFWUpdate(struct msm_sensor_ctrl_t *s_ctrl)
{
    int rc = 0;
    unsigned int UlFWDat;
    g_s_ctrl = s_ctrl;

    pr_info("[OIS_Cali]%s VERNUM_M = %x\n", __func__, VERNUM_M);
    pr_info("[OIS_Cali]%s VERNUM_F = %x\n", __func__, VERNUM_F);
    WitTim(100);

    RamRead32A(0x8000,&UlFWDat );
    pr_info("[OIS_Cali]%s CAM:%d FW Ver = %x\n", __func__,g_s_ctrl->id, UlFWDat);

    GYRO_Cali_init(s_ctrl);

    if((g_s_ctrl->id == 0)&&((UlFWDat&0xF) >= (BASEVWNUM_M&0xF))&&((VERNUM_M&0xF)>(UlFWDat&0xF))&&((UlFWDat&0xF)!=(VERNUM_M&0xF)))
    {
        pr_info("[OIS_Cali]%s:main camera FW update. %x -> %x", __func__, UlFWDat, VERNUM_M);
        rc = FlashUpdateM();
        if(rc!=0)
            pr_info("[OIS_Cali]%s:FlashUpdateM = %d  fail.", __func__, rc);
    }else if ((g_s_ctrl->id == 1)&&((UlFWDat&0xF) >= (BASEVWNUM_F&0xF))&&((VERNUM_F&0xF)>(UlFWDat&0xF))&&((UlFWDat&0xF)!=(VERNUM_F&0xF))){
        pr_info("[OIS_Cali]%s:front camera FW update. %x -> %x", __func__, UlFWDat, VERNUM_F);
        rc = FlashUpdateF();
        if(rc!=0)
            pr_info("[OIS_Cali]%s:FlashUpdateF = %d  fail.", __func__, rc);
    }else
        pr_info("[OIS_Cali]%s:FlashUpdate camera ID %d no need to update.", __func__, g_s_ctrl->id);

    GYRO_Cali_release();

    RamRead32A(0x8000,&UlFWDat );
    pr_info("[OIS_Cali]%s rc = %d\n", __func__, rc);

    return rc;
}

signed short Calibration_VerifyUpdate_PreRead( void )
{
	unsigned char UcCnt;
	unsigned short UsNum;
	unsigned int UlReadVal[4];

	
	FlashResetRelease();		
	
	FlashAutoConfig();
	
	IOWrite32A( FLASHROM_TPGS, 118 );			
	IOWrite32A( FLASHROM_TPROG , 70 );			
	IOWrite32A( FLASHROM_TERASES , 92 );		
	IOWrite32A( FLASHROM_ADR , 0x00010000 );	
	IOWrite32A( FLASHROM_ACSCNT , (256 -1) );	
	IOWrite32A( FLASHROM_CMD , 1 );		
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UsNum = 0; UsNum <= 0xFF; UsNum+=4 )
	{
		IORead4times32A( UlReadVal );
		for (UcCnt= 0 ; UcCnt< 4 ; UcCnt++){
			NVR0_Backup[UsNum + UcCnt]   = (unsigned char)UlReadVal[UcCnt];
			FLASH_SECTOR_BUFFER[UsNum + UcCnt]	     = (unsigned char)(UlReadVal[UcCnt]>>8);
			NVR2_Backup[UsNum + UcCnt]   = (unsigned char)(UlReadVal[UcCnt]>>16);
		}
	}
	
	IOWrite32A( FLASHROM_WPB , 1 );							
	WPBCtrl(WPB_OFF) ;										
	if ( ReadWPB() != 1 ){									
		WPBCtrl(WPB_ON) ;									
		FlashReset(); 
		return ( 5 );
	}

	IOWrite32A( FLASHROM_ADR , 0x00010000 );				
	IOWrite32A( FLASHROM_SEL , 0x06  );			
	IOWrite32A( FLASHROM_CMD , 4 );							
	
	return( 0 );
}

void FlashResetRelease(void)
{
	unsigned int UlReadVal;
	
	IORead32A( SOFTRESET	, &UlReadVal ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, (UlReadVal | 0x00000010) ) ;	
}

void FlashAutoConfig( void )
{
	IOWrite32A( FLASHROM_WPB	, 1 );			
	IOWrite32A( FLASHROM_SEL	, 7 );			
	IOWrite32A( FLASHROM_ADR	, 0x00010100 );	
	IOWrite32A( FLASHROM_ACSCNT	, 7 );			
	IOWrite32A( FLASHROM_CMD	, 7 );			
}

signed short Calibration_VerifyUpdate( void )
{
	unsigned char UcCnt;
	unsigned short UsNum;
	unsigned char UcNvrData[2];
	unsigned int UlReadVal[4];
    md5_context ctx;
	CRC_Reg = 0x0000ffff;
	
	for ( UsNum = 0; UsNum <= 0xFF; UsNum++ )	
	{
		
		UcNvrData[0] = NVR0_Backup[ UsNum ];	
		UcNvrData[1] = FLASH_SECTOR_BUFFER[ UsNum ];		
		CRC16_main( UcNvrData, 2 );
	}
	NVR2_Backup[ 0x22 ] = (unsigned char)(CRC_Reg>>8);
	NVR2_Backup[ 0x23 ] = (unsigned char)CRC_Reg;
	md5_starts( &ctx );
	for ( UsNum = 0; UsNum <= 0xFF; UsNum++ )	
	{
		
		UcNvrData[0] = FLASH_SECTOR_BUFFER[ UsNum ];		
		UcNvrData[1] = NVR0_Backup[ UsNum ];	
		md5_update( &ctx, UcNvrData, 2);
	}
	md5_finish( &ctx, &(NVR2_Backup[ 0x10 ]) );
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_INT ) ;	
	for ( UsNum  = 0; UsNum  < 10; UsNum ++ )						
	{
		RamRead32A(  CMD_IO_DAT_ACCESS, UlReadVal ) ;
		if( !(UlReadVal[0] ==  0x80) ){
			break;
		}
		WitTim( 2 );
	}
	IOWrite32A( FLASHROM_ACSCNT , 0 );						
	IOWrite32A( FLASHROM_ADR , 0x00010000 );				
	for ( UsNum = 0; UsNum <= 0x7F; UsNum++ )				
	{
		IOWriteDouble32A( FLASHROM_WDAT, ((unsigned int)(NVR2_Backup[UsNum])<<16)+((unsigned int)(FLASH_SECTOR_BUFFER[UsNum])<<8),
						  FLASHROM_CMD,   2 );	
		
	}
	IOWrite32A( FLASHROM_ADR , 0x00010000 + 0x80 );			
	for ( UsNum = 0; UsNum <= 0x7F; UsNum++ )				
	{
		IOWriteDouble32A( FLASHROM_WDAT, ((unsigned int)(NVR2_Backup[UsNum+0x80])<<16)+((unsigned int)(FLASH_SECTOR_BUFFER[UsNum+0x80])<<8),
					 	  FLASHROM_CMD , 2 );	
		
	}
	IOWrite32A( FLASHROM_WPB, 0  );							
	WPBCtrl(WPB_ON) ;										
	IOWrite32A( FLASHROM_ADR, 0x00010000  );				
	IOWrite32A( FLASHROM_ACSCNT, (256 -1)  );				
	IOWrite32A( FLASHROM_CMD , 1  );						

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UsNum = 0; UsNum <= 0xFF; UsNum+=4 )
	{
		IORead4times32A( UlReadVal )  ;
		for ( UcCnt= 0; UcCnt< 4; UcCnt++ )
		{
			if ( (unsigned char)(UlReadVal[UcCnt]  >>8  ) != FLASH_SECTOR_BUFFER[UsNum + UcCnt] ){	FlashReset();	return(-1); }
			if ( (unsigned char)(UlReadVal[UcCnt]  >>16 ) != NVR2_Backup[UsNum + UcCnt] )		 {	FlashReset();	return(-1); }
		}
	}
	
	FlashReset();
		
	return ( 0 );
}

void IOWrite32A( unsigned int IOadrs, unsigned int IOdata )
{
#ifdef __EXTRA_E0_COMMAND__
	unsigned char UcBuf[9];
	UcBuf[0] = 0xE8;
	UcBuf[1] = (unsigned char)(IOdata >> 24);
	UcBuf[2] = (unsigned char)(IOdata >> 16);
	UcBuf[3] = (unsigned char)(IOdata >> 8);
	UcBuf[4] = (unsigned char)(IOdata >> 0);
	UcBuf[5] = (unsigned char)(IOadrs >> 16);
	UcBuf[6] = (unsigned char)(IOadrs >> 8);
	UcBuf[7] = (unsigned char)(IOadrs >> 0);
	CntWrt( UcBuf, 8 ) ;
#else
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, IOdata ) ;
#endif	
};

void IORead32A( unsigned int IOadrs, unsigned int *IOdata )
{
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs ) ;
	RamRead32A ( CMD_IO_DAT_ACCESS, IOdata ) ;
};

void IORead4times32A( unsigned int* Dat )
{
#ifdef __EXTRA_E0_COMMAND__
	UINT8 UcBuf[16];
	CntRd( 0xE8, UcBuf, 16 )  ;
	
	Dat[0] = ((UINT32)UcBuf[0]<<24) | ((UINT32)UcBuf[1]<<16) | ((UINT32)UcBuf[2]<<8) | (UINT32)UcBuf[3] ;
	Dat[1] = ((UINT32)UcBuf[4]<<24) | ((UINT32)UcBuf[5]<<16) | ((UINT32)UcBuf[6]<<8) | (UINT32)UcBuf[7] ;
	Dat[2] = ((UINT32)UcBuf[8]<<24) | ((UINT32)UcBuf[9]<<16) | ((UINT32)UcBuf[10]<<8) | (UINT32)UcBuf[11] ;
	Dat[3] = ((UINT32)UcBuf[12]<<24) | ((UINT32)UcBuf[13]<<16) | ((UINT32)UcBuf[14]<<8) | (UINT32)UcBuf[15] ;	


#else	
	RamRead32A( CMD_IO_DAT_ACCESS , &Dat[0] ) ;
	RamRead32A( CMD_IO_DAT_ACCESS , &Dat[1] ) ;
	RamRead32A( CMD_IO_DAT_ACCESS , &Dat[2] ) ;
	RamRead32A( CMD_IO_DAT_ACCESS , &Dat[3] ) ;
#endif
}

void CRC16_main( unsigned char *p, int Num )
{
	unsigned int tmp0, tmp5, tmp12;
	unsigned int temp, data;
	int i = 0, j = 0;

	for(i=0 ; i<Num ; i++) {
		temp = (unsigned int)*p++;		

		for(j=0 ; j<8 ; j++) {
			data = temp & 0x00000001;	
			temp = temp >> 1;

            tmp0 = ((CRC_Reg >> 15) ^ data) & 0x00000001;
            tmp5 = (((tmp0 << 4) ^ CRC_Reg) & 0x00000010) << 1;
            tmp12 = (((tmp0 << 11) ^ CRC_Reg) & 0x00000800) << 1;
            CRC_Reg = (CRC_Reg << 1) & 0x0000efde;
            CRC_Reg = CRC_Reg | tmp0 | tmp5 | tmp12;
		}
	}
}

void IOWriteDouble32A( unsigned int IOadrs1, unsigned int IOdata1, unsigned int IOadrs2, unsigned int IOdata2 )
{
#ifdef __EXTRA_E0_COMMAND__
	unsigned char UcBuf[15];
	UcBuf[0] = 0xE8;
	UcBuf[1] = (UINT8)(IOdata1 >> 24);
	UcBuf[2] = (UINT8)(IOdata1 >> 16);
	UcBuf[3] = (UINT8)(IOdata1 >> 8);
	UcBuf[4] = (UINT8)(IOdata1 >> 0);
	UcBuf[5] = (UINT8)(IOadrs1 >> 16);
	UcBuf[6] = (UINT8)(IOadrs1 >> 8);
	UcBuf[7] = (UINT8)(IOadrs1 >> 0);
	UcBuf[8] = (UINT8)(IOdata2 >> 24);
	UcBuf[9] = (UINT8)(IOdata2 >> 16);
	UcBuf[10] = (UINT8)(IOdata2 >> 8);
	UcBuf[11] = (UINT8)(IOdata2 >> 0);
	UcBuf[12] = (UINT8)(IOadrs2 >> 16);
	UcBuf[13] = (UINT8)(IOadrs2 >> 8);
	UcBuf[14] = (UINT8)(IOadrs2 >> 0);
	CntWrt( UcBuf, 15 ) ;
#else
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs1 ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, IOdata1 ) ;					
	RamWrite32A( CMD_IO_ADR_ACCESS, IOadrs2 ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, IOdata2 ) ;					
#endif	
};

void FlashNVR_ReadData_ByteA( unsigned short SetAddress, unsigned char * ReadPtr, unsigned short Num )
{
	FlashNVR_ReadData_Byte( MakeNVRSelIdx(SetAddress), (unsigned char)(SetAddress & 0xFF), ReadPtr, Num ) ;
}

unsigned short MakeNVRSelIdx( unsigned short UsAddress )
{
	
	
	
	return ((UsAddress >> 8) & 0x03);
}

unsigned char ReadWPB( void )

{
#ifdef __OIS_TYPE_XC__					
	return ( 1 ) ;
#else		
#if 0
	UINT32	UlReadVal, UlCnt=0;
#else
    unsigned int UlReadVal, UlCnt=0;
#endif
	do{
        IORead32A( IOPLEVR  , &UlReadVal ) ;
        pr_info("%s:UlReadVal = %u UlCnt = %u \n", __func__, UlReadVal, UlCnt);
		if( (UlReadVal & 0x0400) != 0 )	return ( 1 ) ;
		WitTim( 1 );		
	}while ( UlCnt++ < 10 );
    pr_info("[OIS_Cali]%s:return 0  \n", __func__);

	return ( 0 );
#endif
}

void WitTim( unsigned short	UsWitTim )
{
    mdelay(UsWitTim);
}

int RamWrite32A( unsigned short RamAddr, unsigned int RamData )
{
	int rc = 0;
	uint8_t data[4] = {0,0,0,0};
	struct msm_sensor_ctrl_t *s_ctrl = g_s_ctrl;

	data[0] = (RamData >> 24) & 0xFF;
	data[1] = (RamData >> 16) & 0xFF;
	data[2] = (RamData >> 8)  & 0xFF;
	data[3] = (RamData) & 0xFF;
	
	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_seq(
		s_ctrl->sensor_i2c_client, RamAddr, &data[0], 4);
	if (rc < 0)
		pr_err("[OIS_Cali] %s : write failed\n", __func__);

    return rc;
}

int RamRead32A( unsigned short RamAddr, unsigned int * ReadData )
{
	int rc = 0;
	uint8_t buf[4] = {0,0,0,0};
	struct msm_sensor_ctrl_t *s_ctrl = g_s_ctrl;

	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read_seq(
		s_ctrl->sensor_i2c_client, RamAddr, &buf[0], 4);
	if (rc < 0)
		pr_err("[OIS_Cali] %s : read failed\n", __func__);
	else
		*ReadData = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    return rc;
}

int CntWrt( unsigned char * PcSetDat, unsigned short UsDatNum)
{
    int rc = 0;
    int temp = 0;
    struct msm_sensor_ctrl_t *s_ctrl = g_s_ctrl;
    temp = s_ctrl->sensor_i2c_client->addr_type;
    s_ctrl->sensor_i2c_client->addr_type = MSM_CAMERA_I2C_BYTE_ADDR;
    rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_write_seq(s_ctrl->sensor_i2c_client, PcSetDat[0], &PcSetDat[1], UsDatNum-1);
    s_ctrl->sensor_i2c_client->addr_type = temp;
    if (rc < 0) {
        pr_err("%s:i2c write sequence error:%d\n", __func__, rc);
        return rc;
    }
    return rc;
}

int CntRd( unsigned int addr, unsigned char * PcSetDat, unsigned short UsDatNum )
{
    int rc = 0;
    int temp = 0;
    struct msm_sensor_ctrl_t *s_ctrl = g_s_ctrl;
    temp = s_ctrl->sensor_i2c_client->addr_type;
    s_ctrl->sensor_i2c_client->addr_type = MSM_CAMERA_I2C_BYTE_ADDR;
    rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read_seq(s_ctrl->sensor_i2c_client, addr, PcSetDat, UsDatNum);
    s_ctrl->sensor_i2c_client->addr_type = temp;

    if (rc < 0) {
        pr_err("%s:i2c write sequence error:%d\n", __func__, rc);
        return rc;
    }
    return rc;
}

unsigned short MakeNVRSel( unsigned short UsAddress )
{
        
        
        
        return 1 << ((UsAddress >> 8) & 0x03);
}
 
unsigned int MakeNVRDat( unsigned short UsAddress, unsigned char UcData )
{
        
        
        
        return (unsigned int)UcData << (((UsAddress >> 8) & 0x03) * 8);
}
 
unsigned char MakeDatNVR( unsigned short UsAddress, unsigned int UlData )
{
        return (unsigned char)((UlData >> (((UsAddress >> 8) & 0x03) * 8)) & 0xFF);
}
 
void WPBCtrl( unsigned char UcCtrl )
{
    int rc = 0;
    pr_info("[OIS_Cali]%s:E\n", __func__);
        if (UcCtrl == 0)
        {       
            rc = gpio_request_one(g_GYRO_info->flash_rw, 0, "flash_rw");
            pr_info("[OIS_Cali]%s : Write Protect ON  flash_rw = %d\n", __func__, g_GYRO_info->flash_rw);
            if (rc < 0)
                pr_err("[OIS_Cali]%s:GPIO(%d) request failed", __func__,g_GYRO_info->flash_rw);

            if (g_GYRO_info->flash_rw != 0){
                gpio_set_value_cansleep(g_GYRO_info->flash_rw,0);
                mdelay(5);
                gpio_free(g_GYRO_info->flash_rw);
            }
            else
                pr_err("[OIS_Cali]%s:GPIO(%d) g_GYRO_info->flash_rw failed\n", __func__, g_GYRO_info->flash_rw);

            pr_info("[OIS_Cali]%s:Write Protect ON \n", __func__);
        } else {
            
            rc = gpio_request_one(g_GYRO_info->flash_rw, 0, "flash_rw");
                pr_info("[OIS_Cali]%s:Write Protect OFF  flash_rw = %d\n", __func__,g_GYRO_info->flash_rw);
            if (rc < 0)
                pr_err("[OIS_Cali]%s:GPIO(%d) request failed", __func__,g_GYRO_info->flash_rw);

            if (g_GYRO_info->flash_rw != 0){
                gpio_set_value_cansleep(g_GYRO_info->flash_rw,1);
                mdelay(5);
                gpio_free(g_GYRO_info->flash_rw);
            }
            else
                pr_err("[OIS_Cali]%s:GPIO(%d) g_GYRO_info->flash_rw failed\n", __func__, g_GYRO_info->flash_rw);

            pr_info("[OIS_Cali]%s:Write Protect OFF \n", __func__);
        }
}









void FlashInitialSetting( char val )
{
	unsigned int UlReadVal = 0;
	int i;

	
	RamWrite32A( CMD_IO_ADR_ACCESS, CVER_123 ) ;
	RamRead32A(  CMD_IO_DAT_ACCESS, &UlReadVal ) ;
	if( UlReadVal > 0xB4 ) {
		return ;
	}

	
	RamWrite32A( CMD_IO_ADR_ACCESS, SOFTRESET );
	RamRead32A ( CMD_IO_DAT_ACCESS, &UlReadVal );
	UlReadVal |= 0x00000010;									
	
	RamWrite32A( CMD_IO_DAT_ACCESS, UlReadVal );

	
	if( val ) {
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_TPGS ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 118 ) ;					

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_TPROG ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 70 ) ;					

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_TERASES ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 92 ) ;					

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_TERASEC ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 115 ) ;					

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 7 ) ;

		
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 0x00000000	  ) ;		
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT ) ;		
		RamWrite32A( CMD_IO_DAT_ACCESS, 0 ) ;
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_WPB ) ;		
		RamWrite32A( CMD_IO_DAT_ACCESS, 1 ) ;
		WPBCtrl(WPB_OFF) ;										

		for( i = 0; i < 8; i++ )
		{
			RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_WDAT ) ;
			RamWrite32A( CMD_IO_DAT_ACCESS, 0xFFFFFFFF ) ; 

			RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
			RamWrite32A( CMD_IO_DAT_ACCESS, 3) ;  				
		}

		
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 0x00010100	  ) ;		
		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 7 ) ;

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
		RamWrite32A( CMD_IO_DAT_ACCESS, 7 ) ;  					

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;		
		RamWrite32A( CMD_IO_DAT_ACCESS, 0 ) ;

		RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_WPB ) ;		
		RamWrite32A( CMD_IO_DAT_ACCESS, 0 ) ;
		WPBCtrl(WPB_ON) ;										
	}
}



void FlashReset(void)
{
	unsigned int UlReadVal;

	
	IORead32A( SOFTRESET	, &UlReadVal ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, (UlReadVal & (~0x00000010)) ) ;
}


signed short FlashNVRSectorErase_Byte( unsigned char SetAddress )
{
	unsigned char UcCnt;
	unsigned int UlReadVal = 0;

	FlashInitialSetting(1);										

	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 0x00010000	  ) ;
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, MakeNVRSel( SetAddress ) ) ;

	WPBCtrl(WPB_OFF) ;											
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_WPB ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 1 ) ;
	
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 4 ) ;						

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_INT ) ;	
	for ( UcCnt = 0; UcCnt < 100; UcCnt++ )						
	{
		RamRead32A(  CMD_IO_DAT_ACCESS, &UlReadVal ) ;
		if( !(UlReadVal ==  0x80) ){
			break;
		}
	}
	
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_WPB ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS,0 ) ;
	WPBCtrl(WPB_ON) ;											

	FlashReset();												
	return ( 0 );
}


void FlashNVR_ReadData_Byte( unsigned char Sel, unsigned char SetAddress, unsigned char * ReadPtr, unsigned short Num )
{
	unsigned short UsNum;
	unsigned int UlReadVal;

	if( Sel >= 3 ) return;
	if( Num == 0 || Num > 256 ) return; 
	if( SetAddress + Num > 256 ) return; 

	
	FlashResetRelease();
	
	FlashAutoConfig();

	
	IOWrite32A( FLASHROM_ACSCNT	,  Num -1 ) ;			
	
	IOWrite32A( FLASHROM_SEL	, (1<<Sel) ) ;			

	
	IOWrite32A( FLASHROM_ADR	, 0x00010000 +  SetAddress ) ;		
	
	IOWrite32A( FLASHROM_CMD	, 1 ) ;					

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UsNum = 0; UsNum < Num; UsNum++ )
	{
		RamRead32A(  CMD_IO_DAT_ACCESS, &UlReadVal ) ;
		ReadPtr[ UsNum ] = (unsigned char)(UlReadVal>>(Sel*8) );
	}

	FlashReset();												
}

#ifdef __CRC_VERIFY__
unsigned int CRC_Reg = 0x0000ffff;
void CRC16_main( unsigned char *p, int Num )
{
	unsigned int tmp0, tmp5, tmp12;
	unsigned int temp, data;

	for(int i=0 ; i<Num ; i++) {
		temp = (unsigned int)*p++;		

		for(int j=0 ; j<8 ; j++) {
			data = temp & 0x00000001;	
			temp = temp >> 1;

            tmp0 = ((CRC_Reg >> 15) ^ data) & 0x00000001;
            tmp5 = (((tmp0 << 4) ^ CRC_Reg) & 0x00000010) << 1;
            tmp12 = (((tmp0 << 11) ^ CRC_Reg) & 0x00000800) << 1;
            CRC_Reg = (CRC_Reg << 1) & 0x0000efde;
            CRC_Reg = CRC_Reg | tmp0 | tmp5 | tmp12;
		}
	}
}

void FlashMainCrc( unsigned char * pCRC )
{
#if 0
	UINT32 UlNum;
	UINT32 UlReadVal;
	UINT8 UcFlaData[3];
#else
	unsigned int UlNum;
	unsigned int UlReadVal;
	unsigned char UcFlaData[3];
#endif
    

	CRC_Reg = 0x0000ffff;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 7 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 0 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT	 ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 4096 - 1 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 1) ;  					

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for (UlNum= 0 ; UlNum< 4096 ; UlNum++)
	{
		
		RamRead32A( CMD_IO_DAT_ACCESS , &UlReadVal ) ;
		UcFlaData[0] = UlReadVal & 0xFF;
		UcFlaData[1] = (UlReadVal >> 8) & 0xFF;
		UcFlaData[2] = (UlReadVal >> 16) & 0xFF;
		CRC16_main( UcFlaData, 3 );
	}
	pCRC[0] = (UINT8)(CRC_Reg>>8);
	pCRC[1] = (UINT8)CRC_Reg;

}

void FlashNvrCrc( unsigned char * pCRC )
{
#if 0
	UINT32 UlNum;
	UINT32 UlReadVal;	
	UINT8 UcNvrData[2];
#else
	unsigned int UlNum;
	unsigned int UlReadVal;   
	unsigned char UcNvrData[2];
#endif

	CRC_Reg = 0x0000ffff;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 3 ) ;						
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 0x00010000 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 256 - 1 ) ;					
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 1 ) ;  						

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UlNum = 0; UlNum < 256; UlNum++ )
	{
		
		RamRead32A( CMD_IO_DAT_ACCESS, &UlReadVal ) ;
		UcNvrData[0] = UlReadVal & 0xFF;				
		UcNvrData[1] = (UlReadVal >> 8) & 0xFF;			
		CRC16_main( UcNvrData, 2 );
	}
	pCRC[0] = (UINT8)(CRC_Reg>>8);
	pCRC[1] = (UINT8)CRC_Reg;

}
#else
void FlashMainMd5( unsigned char * pMD5 )
{
	unsigned int UlNum;
	unsigned int UlReadVal;
	unsigned char UcFlaData[3];

    md5_context ctx;

	md5_starts( &ctx );

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 7 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 0 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT	 ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 4096 - 1 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 1) ;  					

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;		
	for (UlNum= 0 ; UlNum< 4096 ; UlNum++)
	{
		
		RamRead32A( CMD_IO_DAT_ACCESS , &UlReadVal ) ;
		UcFlaData[0] = (UlReadVal >> 16) & 0xFF;
		UcFlaData[1] = (UlReadVal >> 8) & 0xFF;
		UcFlaData[2] = UlReadVal & 0xFF;
		md5_update( &ctx, (unsigned char *)UcFlaData, 3 );
	}
	md5_finish( &ctx, pMD5 );

}

void FlashNvrMd5( unsigned char * pMD5 )
{
	unsigned char UcNvrData[2];
	unsigned int UlNum;
	unsigned int UlReadVal;
    md5_context ctx;

	md5_starts( &ctx );

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_SEL ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 3 ) ;						
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ADR ) ;			
	RamWrite32A( CMD_IO_DAT_ACCESS, 0x00010000 ) ;
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_ACSCNT ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 256 - 1 ) ;					
	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_CMD ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS, 1 ) ;  						

	RamWrite32A( CMD_IO_ADR_ACCESS, FLASHROM_RDAT ) ;
	for ( UlNum = 0; UlNum < 256; UlNum++ )
	{
		
		RamRead32A( CMD_IO_DAT_ACCESS, &UlReadVal ) ;
		UcNvrData[0] = (UlReadVal >> 8) & 0xFF;			
		UcNvrData[1] = UlReadVal & 0xFF;				
		md5_update( &ctx, (unsigned char *)UcNvrData, 2 );
	}
	md5_finish( &ctx, pMD5 );

}
#endif




void	OscStb( void )
{
	RamWrite32A( CMD_IO_ADR_ACCESS , STBOSCPLL ) ;
	RamWrite32A( CMD_IO_DAT_ACCESS , OSC_STB ) ;
}

