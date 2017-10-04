#include "mbmasterpyro.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mb.h"
#include "mb_m.h"
#include "mbrtu.h"
#include "mb_master_user.h"
#include "mbfunc.h"
#include "adc_pyro_buf.h"
#include "adc_dcmi.h"
#include "udp_send.h"


#define MB_RTU_TASK_STACK_SIZE			2048
#define MB_RTU_POLL_TASK_STACK_SIZE	256

#define SLAVE_PYRO_SQUIB_ADDR					0xA
#define SLAVE_PYRO_SQUIB_TIMEOUT			100
#define SLAVE_PYRO_SQUIB_POLL_PERIOD	1

#define MODBUS_M_BAUDRATE			921600

xSemaphoreHandle xSendRTURegSem;
xSemaphoreHandle xMBRTUMutex;

extern stTCPtoRTURegWrite TCPtoRTURegWrite;
extern stPacket UDPPacket;
extern enADCPyroBufState ADCPyroBufState;
extern uint64_t ADC_Pyro_Timestamp;
extern uint16_t BaseADC_Started_Flag;

void MBMaster_RTU_Task(void *pvParameters);
void MBMaster_RTU_Poll(void *pvParameters);


void MBMaster_RTU_Init(void)
{
	xSendRTURegSem=xSemaphoreCreateBinary();
	xMBRTUMutex=xSemaphoreCreateMutex();
	
	eMBMasterInit(MB_RTU, 2, MODBUS_M_BAUDRATE,  MB_PAR_NONE);
	eMBMasterEnable();
	xTaskCreate(MBMaster_RTU_Task, "MB RTU Task", MB_RTU_TASK_STACK_SIZE, NULL, 2, ( TaskHandle_t * ) NULL);
	xTaskCreate(MBMaster_RTU_Poll, "MB RTU Poll Task", MB_RTU_POLL_TASK_STACK_SIZE, NULL, 2, ( TaskHandle_t * ) NULL);
}


void MBMaster_RTU_Task(void *pvParameters)
{

	while (1)
	{
		eMBMasterPoll();
	}
}



#define HOLDING_REG_POLL_PERIOD		200
#define REG_ADC_0									0
#define REG_PIR_STATE							16
enum
{
  PYRO_SQUIB_STOP=0,
	PYRO_SQUIB_RUN,
};


eMBMasterReqErrCode    MB_Master_ErrorCode = MB_MRE_NO_ERR;

extern USHORT   usMRegInBuf[MB_MASTER_TOTAL_SLAVE_NUM][M_REG_INPUT_NREGS];

void MBMaster_RTU_Poll(void *pvParameters)
{
	static uint16_t tick_counter=0;
	portTickType xLastWakeTime;
	
	xLastWakeTime = xTaskGetTickCount();
	while (1)
	{		

		if( xSemaphoreTake( xMBRTUMutex, portMAX_DELAY ) == pdTRUE )
		{
				MB_Master_ErrorCode = eMBMasterReqReadInputRegister(SLAVE_PYRO_SQUIB_ADDR,M_REG_INPUT_START,M_REG_INPUT_NREGS,SLAVE_PYRO_SQUIB_TIMEOUT);//������ �������� ��������		
				xSemaphoreGive( xMBRTUMutex );
		}

		//��������� ��������� �����������, ���� �������-��������� �����
		
		if((usMRegInBuf[0][REG_PIR_STATE]!=PYRO_SQUIB_STOP) && (MB_Master_ErrorCode == MB_MRE_NO_ERR) && (BaseADC_Started_Flag))
		{
				ADC_PyroBuf_Add((float*)&usMRegInBuf[0][REG_ADC_0]);
				ADC_Pyro_Timestamp=DCMI_ADC_GetCurrentTimestamp();
				ADCPyroBufState=ADC_PYRO_BUF_FILL_START;
		}
		else
		{
				ADCPyroBufState=ADC_PYRO_BUF_FILL_STOP;
		}
		
		if(tick_counter>=HOLDING_REG_POLL_PERIOD)// � ������ �������� ������ holding ��������
		{
				if( xSemaphoreTake( xMBRTUMutex, portMAX_DELAY ) == pdTRUE )
				{
						MB_Master_ErrorCode = eMBMasterReqReadHoldingRegister(SLAVE_PYRO_SQUIB_ADDR,M_REG_HOLDING_START,M_REG_HOLDING_NREGS,SLAVE_PYRO_SQUIB_TIMEOUT);	
						xSemaphoreGive( xMBRTUMutex );
				}

				tick_counter=0;
		}
		tick_counter++;
	
		vTaskDelayUntil( &xLastWakeTime, 1 );
	}
}

eMBMasterReqErrCode MBMaster_RTU_WriteRegs(stTCPtoRTURegWrite *regs)
{
	eMBMasterReqErrCode    err=MB_MRE_NO_ERR; 
	
	if(regs->nRegs==1)
	{
			err = eMBMasterReqWriteHoldingRegister(SLAVE_PYRO_SQUIB_ADDR,regs->regAddr,regs->regBuf[0],SLAVE_PYRO_SQUIB_TIMEOUT);
	}
	else 
	{
			err = eMBMasterReqWriteMultipleHoldingRegister(SLAVE_PYRO_SQUIB_ADDR,regs->regAddr,regs->nRegs,&regs->regBuf[0],SLAVE_PYRO_SQUIB_TIMEOUT);
	}
	
	return err;
}

