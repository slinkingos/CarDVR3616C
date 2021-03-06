#ifdef __cplusplus
extern "C" {
#endif

/***************************************************
	include
***************************************************/
#include "ctrl_uart_getCommand.h"
#include "base_mw_time.h"

/***************************************************
	macro / enum
***************************************************/
// #define CTRL_UART_GETCOMMAND_BACKUP

//#define CTRL_UART_GETCOMMAND_DEBUG
#ifdef CTRL_UART_GETCOMMAND_DEBUG
#define CUG_Debug(fmt, arg...) fprintf(stdout, "[ CUG ] : %s() <%d> "fmt, __func__, __LINE__, ##arg);
#else
#define CUG_Debug(fmt, arg...)
#endif

#define CUG_FuncIn() CUG_Debug("in\n")
#define CUG_FuncOut() CUG_Debug("out\n")
#define CUG_iVal(iVal) CUG_Debug("%s <%d> @ %p\n", #iVal, iVal, &iVal)
#define CUG_lVal(lVal) CUG_Debug("%s <%ld> @ %p\n", #lVal, lVal, &lVal)
#define CUG_PtVal(ptVal) CUG_Debug("pt %s @ %p\n", #ptVal, ptVal)

#define CTRL_UART_GETCOMMAND_COMMANDCNTMAX 32
#define CTRL_UART_GETCOMMAND_COMMANDMAX 128
#define CTRL_UART_GETCOMMAND_FEEDBACKMAX 128
#define CTRL_UART_GETCOMMAND_PAYLOADMAX (CTRL_UART_GETCOMMAND_COMMANDMAX * CTRL_UART_GETCOMMAND_COMMANDCNTMAX)

/* uart parameters */
#define CTRL_UART_GETCOMMAND_UART_SPEED COMM_DD_UART_Speed9600
#define CTRL_UART_GETCOMMAND_UART_FLOWCTL COMM_DD_UART_XON_XOFF
#define CTRL_UART_GETCOMMAND_UART_DATABITS COMM_DD_UART_Databit8
#define CTRL_UART_GETCOMMAND_UART_STOPBITS COMM_DD_UART_Stopbit1
#define CTRL_UART_GETCOMMAND_UART_PARITY 'N'
#define CTRL_UART_GETCOMMAND_UART_DEV "/dev/ttyAMA1"

/* debug the input command */
#define showcmd(phead, len, msg) do { \
	LONG lReadCnt = 0; \
	printf("[ command dump ] "msg"\n%-32s : < %p >\n%-32s : < %ld >\n", #phead, phead, #len, len); \
	while (len != lReadCnt) { \
		printf("[ %02ld ] : %-4d (0x%02x) @ %p\n", lReadCnt, phead[lReadCnt], phead[lReadCnt], phead + lReadCnt); \
		lReadCnt ++; \
	} \
} while (0)


/***************************************************
	variable
***************************************************/
static CHAR cFeedbackError[] = "#BBADCODE";
static CHAR cLed_RedBlink_GreenBlink[] = "#LLEDR1G1\n";
static LONG lRetryTimeMax = 2;
static CHAR cFeedbackErrorNoPrefix[] = "#BBADCODE99\n";
static CHAR cFeedbackErrorNoPostfix[] = "#BBADCODE98\n";


/***************************************************
	prototype
***************************************************/
void* pvCTRL_UART_getCommand_Thread(void* pvArg);
void vCTRL_UART_getCommand_Thread_Cleanup(void* pvArg);
void vCTRL_UART_getCommand_Thread_ClearThreadArg(sCTRL_UART_GETCOMMAND_ThreadArg* psArg);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckCommand(UCHAR* pucCommand, LONG lCmdLen);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckCode(UCHAR* pucCommand, LONG lCommandLen, LONG lAddCheck, LONG lXorCheck);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_GetFeedBack(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, UCHAR* pucFeedbackHeader, LONG lFeedbackLen);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_welcomeMessage(sCOMM_DD_UART_Info* psUart);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_welcomeMessage_getMsg(CHAR* pcMsg, LONG *plMsgLen);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_getMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG lRemainLen, LONG* plWholeLen);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_handleMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG* plRemainLen, LONG lWholeLen, UCHAR* pucRemainder);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_RegularCommandLen(UCHAR* pucCommand, LONG* plCommandLen);
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckFeedback(sCTRL_UART_GETCOMMAND_ThreadArg* psArg);

/***************************************************
	function
***************************************************/

/*********************************************
* func : eCTRL_UART_getCommand_Init(sCTRL_UART_GETCOMMAND_Info* psInfo, sCOMM_QUEUE* psCommandQueue, sCOMM_QUEUE* psFeedbackQueue)
* arg : sCTRL_UART_GETCOMMAND_Info* psInfo, sCOMM_QUEUE* psCommandQueue, sCOMM_QUEUE* psFeedbackQueue
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : init the parameter
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_Init(sCTRL_UART_GETCOMMAND_Info* psInfo, sCOMM_QUEUE* psCommandQueue, sCOMM_QUEUE* psFeedbackQueue) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;

	CUG_FuncIn();

	{
		/* init the parameter */
		psInfo->psUart = NULL;
		psInfo->pucPayLoad = NULL;
		psInfo->pucCommand = NULL;
		psInfo->pucFeedBack = NULL;
		psInfo->psThreadArg = NULL;
		
		psInfo->eStatus = CTRL_UART_GETCOMMAND_Status_NotEnd;
		psInfo->psCommandQueue = psCommandQueue;
		psInfo->psFeedbackQueue = psFeedbackQueue;
		MALLOC(sizeof(sCOMM_DD_UART_Info));
		psInfo->psUart = malloc(sizeof(sCOMM_DD_UART_Info));
		if(!psInfo->psUart) {
			CUG_Debug("malloc error\n");
			return CTRL_UART_GETCOMMAND_MALLOC;
		}

		MALLOC(CTRL_UART_GETCOMMAND_PAYLOADMAX * sizeof(UCHAR));
		psInfo->pucPayLoad = malloc(CTRL_UART_GETCOMMAND_PAYLOADMAX * sizeof(UCHAR));
		if(!psInfo->pucPayLoad) {
			CUG_Debug("malloc error\n");
			return CTRL_UART_GETCOMMAND_MALLOC;
		}
		/* clear the payload */
		memset(psInfo->pucPayLoad, 0, CTRL_UART_GETCOMMAND_PAYLOADMAX);

		MALLOC(CTRL_UART_GETCOMMAND_COMMANDMAX * sizeof(UCHAR));
		psInfo->pucCommand = malloc(CTRL_UART_GETCOMMAND_COMMANDMAX * sizeof(UCHAR));
		if(!psInfo->pucCommand) {
			CUG_Debug("malloc error\n");
			return CTRL_UART_GETCOMMAND_MALLOC;
		}
		/* clear the payload */
		memset(psInfo->pucPayLoad, 0, CTRL_UART_GETCOMMAND_COMMANDMAX);

		MALLOC(CTRL_UART_GETCOMMAND_FEEDBACKMAX * sizeof(UCHAR));
		psInfo->pucFeedBack = malloc(CTRL_UART_GETCOMMAND_FEEDBACKMAX * sizeof(UCHAR));
		if(!psInfo->pucFeedBack) {
			CUG_Debug("malloc error\n");
			return CTRL_UART_GETCOMMAND_MALLOC;
		}
		/* clear the payload */
		memset(psInfo->pucFeedBack, 0, CTRL_UART_GETCOMMAND_FEEDBACKMAX);

		MALLOC(sizeof(sCTRL_UART_GETCOMMAND_ThreadArg));
		psInfo->psThreadArg = malloc(sizeof(sCTRL_UART_GETCOMMAND_ThreadArg));
		if(!psInfo->psThreadArg) {
			CUG_Debug("malloc error\n");
			return CTRL_UART_GETCOMMAND_MALLOC;
		}

		/* init tid */
		psInfo->tid = -1;

		/* init uart */
		eCOMM_DD_UART_Init(psInfo->psUart, CTRL_UART_GETCOMMAND_UART_SPEED, CTRL_UART_GETCOMMAND_UART_FLOWCTL,
			CTRL_UART_GETCOMMAND_UART_DATABITS, CTRL_UART_GETCOMMAND_UART_STOPBITS,
			CTRL_UART_GETCOMMAND_UART_PARITY, CTRL_UART_GETCOMMAND_UART_DEV);
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_Deinit(sCTRL_UART_GETCOMMAND_Info* psInfo)
* arg : sCTRL_UART_GETCOMMAND_Info* psInfo
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : deinit the parameter
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_Deinit(sCTRL_UART_GETCOMMAND_Info* psInfo) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;

	CUG_FuncIn();

	{
		/* deinit Uart */
		if(psInfo->psUart) {
			eCOMM_DD_UART_Deinit(psInfo->psUart);
			free(psInfo->psUart);
			psInfo->psUart = NULL;
		}

		/* deinit payload */
		if(psInfo->pucPayLoad) {
			free(psInfo->pucPayLoad);
			psInfo->pucPayLoad = NULL;
		}

		/* deinit command */
		if(psInfo->pucCommand) {
			free(psInfo->pucCommand);
			psInfo->pucCommand = NULL;
		}

		/* deinit feedback */
		if(psInfo->pucFeedBack) {
			free(psInfo->pucFeedBack);
			psInfo->pucFeedBack = NULL;
		}

		/* deinit thread argument */
		if(psInfo->psThreadArg) {
			free(psInfo->psThreadArg);
			psInfo->psThreadArg = NULL;
		}

		psInfo->psCommandQueue = NULL;
		psInfo->psFeedbackQueue = NULL;
		psInfo->tid = -1;

	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_StartThread(sCTRL_UART_GETCOMMAND_Info* psInfo)
* arg : sCTRL_UART_GETCOMMAND_Info* psInfo
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : start the main thread
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_StartThread(sCTRL_UART_GETCOMMAND_Info* psInfo) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	sCTRL_UART_GETCOMMAND_ThreadArg* psArg = NULL;
	LONG lRet;

	CUG_FuncIn();

	{
		psArg = psInfo->psThreadArg;
		psArg->peStatus = &psInfo->eStatus;
		psArg->psCommandQueue = psInfo->psCommandQueue;
		psArg->psFeedbackQueue = psInfo->psFeedbackQueue;
		psArg->psUart = psInfo->psUart;
		psArg->pucPayLoad = psInfo->pucPayLoad;
		psArg->pucCommand = psInfo->pucCommand;
		psArg->pucFeedBack = psInfo->pucFeedBack;

		/* toggle the status */
		psInfo->eStatus = CTRL_UART_GETCOMMAND_Status_NotEnd;
		
		lRet = pthread_create(&psInfo->tid, NULL, pvCTRL_UART_getCommand_Thread, (void*)psArg);
		if(lRet != 0) {
			CUG_Debug("create thread error\n");
			return CTRL_UART_GETCOMMAND_THREAD;
		}

	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_StopThread(sCTRL_UART_GETCOMMAND_Info* psInfo)
* arg : sCTRL_UART_GETCOMMAND_Info* psInfo
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : stop thread
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_StopThread(sCTRL_UART_GETCOMMAND_Info* psInfo) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	void* pvThreadRet;

	CUG_FuncIn();

	{
		psInfo->eStatus = CTRL_UART_GETCOMMAND_Status_End;
		pthread_cancel(psInfo->tid);
		pthread_join(psInfo->tid, &pvThreadRet);
		if(pvThreadRet == PTHREAD_CANCELED) {
			CUG_Debug("thread have been canceled\n");
		}
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : pvCTRL_UART_getCommand_Thread(void* pvArg)
* arg : void* pvArg
* ret : void*
* note : main thread of get command 
*********************************************/
void* pvCTRL_UART_getCommand_Thread(void* pvArg) {
	sCTRL_UART_GETCOMMAND_ThreadArg* psThreadArg = (sCTRL_UART_GETCOMMAND_ThreadArg*) pvArg;
	eCOMM_DD_UART_Ret eUartRet = COMM_DD_UART_SUCCESS;
	sCOMM_DD_UART_Info* psUart = psThreadArg->psUart;
	
	/* to save the payload tail */
	UCHAR ucRemainDer[CTRL_UART_GETCOMMAND_COMMANDMAX];
	LONG lRemainDerLen;

	/* whole length */
	LONG lWholeLen = 0;

	/* return */
	eCTRL_UART_GETCOMMAND_Ret eCommandRet = CTRL_UART_GETCOMMAND_SUCCESS;

	CUG_FuncIn();

	{

		/* register the cleanup function */
		pthread_cleanup_push(vCTRL_UART_getCommand_Thread_Cleanup, pvArg);

		/* open the uart */
		eUartRet = eCOMM_DD_UART_Open(psUart);
		if(eUartRet != COMM_DD_UART_SUCCESS) {
			CUG_Debug("open uart error with <%d>\n", eUartRet);
			eCOMM_DD_UART_Close(psUart);
			return NULL;
		}

		/* clear the remainder buffer */
		memset(ucRemainDer, 0, sizeof(ucRemainDer));

		/* init remainder length */
		lRemainDerLen = 0;

		/* wait for base process ready */
		// sleep(4);
		
		while(*psThreadArg->peStatus != CTRL_UART_GETCOMMAND_Status_End) {
			
			/* prepare and read from uart */
			vCTRL_UART_getCommand_Thread_ClearThreadArg(psThreadArg);

			/* check if have remainder */
			if(lRemainDerLen != 0) {
				memcpy((CHAR*)psThreadArg->pucPayLoad, (CHAR*)ucRemainDer, lRemainDerLen);
			}

			/* clear the remainder buffer */
			memset(ucRemainDer, 0, sizeof(ucRemainDer));

			/* read */
			eCommandRet = eCTRL_UART_getCommand_getMessage(psThreadArg, lRemainDerLen, &lWholeLen);
			if (eCommandRet != CTRL_UART_GETCOMMAND_SUCCESS) {
				/* get config message error */
				CUG_Debug("get config message error\n");
			}

			/* handle message */
			eCTRL_UART_getCommand_handleMessage(psThreadArg, &lRemainDerLen, lWholeLen, ucRemainDer);

		}

		/* cleanup */
		pthread_cleanup_pop(1);

	}

	CUG_FuncOut();
	
	return NULL;
}

/*********************************************
* func : vCTRL_UART_getCommand_Thread_Cleanup(void* pvArg)
* arg : void* pvArg
* ret : void
* note : thread clean up function
*********************************************/
void vCTRL_UART_getCommand_Thread_Cleanup(void* pvArg) {
	sCTRL_UART_GETCOMMAND_ThreadArg* psThreadArg = (sCTRL_UART_GETCOMMAND_ThreadArg*) pvArg;
	sCOMM_DD_UART_Info* psUart = psThreadArg->psUart;

	CUG_FuncIn();

	{
		/* close the uart */
		eCOMM_DD_UART_Close(psUart);
	}

	CUG_FuncOut();

}

/*********************************************
* func : vCTRL_UART_getCommand_Thread_ClearThreadArg(sCTRL_UART_GETCOMMAND_ThreadArg* psArg)
* arg : sCTRL_UART_GETCOMMAND_ThreadArg* psArg
* ret : void
* note : clear thread argument
*********************************************/
void vCTRL_UART_getCommand_Thread_ClearThreadArg(sCTRL_UART_GETCOMMAND_ThreadArg* psArg) {

	CUG_FuncIn();

	{
		memset(psArg->pucCommand, 0, CTRL_UART_GETCOMMAND_COMMANDMAX);
		memset(psArg->pucFeedBack, 0, CTRL_UART_GETCOMMAND_FEEDBACKMAX);
		memset(psArg->pucPayLoad, 0, CTRL_UART_GETCOMMAND_PAYLOADMAX);
	}

	CUG_FuncOut();

}

/*********************************************
* func : eCTRL_UART_getCommand_CheckCommand(UCHAR* pucCommand, LONG lCmdLen)
* arg : UCHAR* pucCommand, LONG lCmdLen
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : check the valid of command
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckCommand(UCHAR* pucCommand, LONG lCmdLen) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	UCHAR* pucTmp = NULL;
	UCHAR* pucCheckCommand = pucCommand;
	LONG lCommandLength;
	LONG lWholeLenTmp = lCmdLen;
	UCHAR* pucFirstCmd = NULL;
	LONG lAddCheck;
	LONG lXorCheck;

	CUG_FuncIn();

	{
		/* check current command pointer */
		if(!pucCheckCommand) {
			CUG_Debug("current command is NULL\n");
			return CTRL_UART_GETCOMMAND_NULL;
		}

		/* lenght must greater than 2 'cmd' + 'length' */
		if(lWholeLenTmp <= 2) {
			CUG_Debug("current command length abnormal\n");
			return CTRL_UART_GETCOMMAND_SHORTCOMMAND;
		}

		/* 1. parse the first byte, not need, the first '#' already removed */
		pucTmp = pucCheckCommand;
		#if 0
		if(*pucTmp++ != '#') {
			CUG_Debug("first charactor should be #\n");
			return CTRL_UART_GETCOMMAND_SYNTAX;
		}
		#endif

		/* 2. skip the command */
		pucTmp ++;
		lWholeLenTmp --;

		/* 3. length */
		lCommandLength = (LONG)(*pucTmp++);
		lWholeLenTmp --;

		/* 4. check length */
		if(lWholeLenTmp < (lCommandLength + 2)) {
			CUG_Debug("the length is too small, the length is <%ld> (and we have two more byte for valid check), but remain only <%ld>\n", 
				lCommandLength, lWholeLenTmp);
			return CTRL_UART_GETCOMMAND_LENGTH;
		}

		/* 5. commands */
		pucFirstCmd = pucTmp;
		pucTmp += lCommandLength;

		/* 6. check code ?? */
		lAddCheck = (LONG)(*pucTmp++);
		lXorCheck = (LONG)(*pucTmp++);
		eRet = eCTRL_UART_getCommand_CheckCode(pucFirstCmd, lCommandLength, lAddCheck, lXorCheck);
		if(eRet != CTRL_UART_GETCOMMAND_SUCCESS) {
			CUG_Debug("wrong code(sum or xor)\n");
			return eRet;
		}
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_GETCOMMAND_CHECKVALID_CheckCode(UCHAR* pucCommand, LONG lCommandLen, LONG lAddCheck, LONG lXorCheck)
* arg : UCHAR* pucCommand, LONG lCommandLen, LONG lAddCheck, LONG lXorCheck
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : check the valid of command code, include add and xor
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckCode(UCHAR* pucCommand, LONG lCommandLen, LONG lAddCheck, LONG lXorCheck) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	LONG lAddResult = 0;
	LONG lXorResult = 0;
	LONG lEntry = 0;

	CUG_FuncIn();

	{
		/* calculate the add and xor result */
		for (; lEntry != lCommandLen; lEntry ++) {
			lAddResult += pucCommand[lEntry];
			lXorResult ^= pucCommand[lEntry];
		}

		/* correct the add result */
		lAddResult &= 0xff;

		if (lAddResult != lAddCheck ) {
			CUG_Debug("error add check < %ld, %ld >\n", lAddCheck, lAddResult);
			return CTRL_UART_GETCOMMAND_ADDCHECK;
		}

		if (lXorResult != lXorCheck ) {
			CUG_Debug("error xor check < %ld, %ld >\n", lXorCheck, lXorResult);
			return CTRL_UART_GETCOMMAND_XORCHECK;
		}

	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_GetFeedBack(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, UCHAR* pucFeedbackHeader, LONG lFeedbackLen)
* arg : sCTRL_UART_GETCOMMAND_ThreadArg* psArg, UCHAR* pucFeedbackHeader, LONG lFeedbackLen
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : get feedback according to the command
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_GetFeedBack(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, UCHAR* pucFeedbackHeader, LONG lFeedbackLen) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	UCHAR* pucFeedback = psArg->pucFeedBack;

	CUG_FuncIn();

	{
		/* the first is '#' */
		*pucFeedback = '#';

		/* memcpy the remain */
		pucFeedback ++;
		memcpy(pucFeedback, pucFeedbackHeader, lFeedbackLen);

		/* tail */
		pucFeedback[lFeedbackLen] = '\n';
		pucFeedback[lFeedbackLen + 1] = '\0';
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_welcomeMessage(sCOMM_DD_UART_Info* psUart)
* arg : sCOMM_DD_UART_Info* psUart
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : first message to the device 1
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_welcomeMessage(sCOMM_DD_UART_Info* psUart) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	CHAR cWelcomeMsg[16];
	LONG lMsgLen;

	CUG_FuncIn();

	{
		eCTRL_UART_getCommand_welcomeMessage_getMsg(cWelcomeMsg, &lMsgLen);
		eCOMM_DD_UART_Write(psUart, (UCHAR*)cWelcomeMsg, lMsgLen);
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_welcomeMessage_getMsg(CHAR* pcMsg, LONG *plMsgLen)
* arg : CHAR* pcMsg, LONG *plMsgLen
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : construct first message
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_welcomeMessage_getMsg(CHAR* pcMsg, LONG *plMsgLen) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;

	CUG_FuncIn();

	{
		pcMsg[ 0 ] = '#';
		pcMsg[ 1 ] = 'R';
		pcMsg[ 2 ] = 'R';
		pcMsg[ 3 ] = 'E';
		pcMsg[ 4 ] = 'A';
		pcMsg[ 5 ] = 'D';
		pcMsg[ 6 ] = 'Y';
		pcMsg[ 7 ] = '\n';

		*plMsgLen = 8;
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_getMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG lRemainLen, LONG* plWholeLen)
* arg : sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG lRemainLen, LONG* plWholeLen
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : get massage from device 1
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_getMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG lRemainLen, LONG* plWholeLen) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	eCOMM_DD_UART_Ret eUartRet = COMM_DD_UART_SUCCESS;
	LONG lTimeOutSec = 60;
	static LONG lRetryTime = 0;

	CUG_FuncIn();

	{

		if (*psArg->peStatus & CTRL_UART_GETCOMMAND_Status_Configed) {
			/* read from uart device 1 */
			eCOMM_DD_UART_Read(psArg->psUart, psArg->pucPayLoad + lRemainLen, plWholeLen);
			*plWholeLen += lRemainLen;
		} else {
			while (1) {
				/* send welcome message */
				eCTRL_UART_getCommand_welcomeMessage(psArg->psUart);

				/* try to get config message */
				eUartRet = eCOMM_DD_UART_Read_Timeout(psArg->psUart, psArg->pucPayLoad + lRemainLen, plWholeLen, lTimeOutSec);
				if (eUartRet == COMM_DD_UART_TIMEOUT) {
					/* time out */
					if (++lRetryTime >= lRetryTimeMax) {
						CUG_Debug("warning : timeout to get config message, already reach the max time <%ld>\n", lRetryTimeMax);
						strcpy((CHAR*)(psArg->pucPayLoad) + lRemainLen, cLed_RedBlink_GreenBlink);
						*plWholeLen = strlen(cLed_RedBlink_GreenBlink);
#ifdef RECONFIGTIME
						time_t sTime;
						if(!access(SAVEFILE_TIMESTAMP, F_OK)){
							eCOMM_UTIL_LoadTimeCalendarfile(SAVEFILE_TIMESTAMP, NULL, &sTime);
							/* set time */
							sTime += CONFIGTIMEDELTA;
							stime(&sTime); 
						}
#endif
						break;
					}
					CUG_Debug("warning : timeout to get config message, retry again <%ld>\n", lRetryTime);
				} else {
					eCOMM_UTIL_SaveCurTimeVal2file(BOOT_TIMESTAMP);
					/* get the message */
					sleep(4);
					break;
				}
			}
		}
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_handleMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG* plRemainLen, LONG lWholeLen, UCHAR* pucRemainder)
* arg : sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG* plRemainLen, LONG lWholeLen, UCHAR* pucRemainder
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : handle message
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_handleMessage(sCTRL_UART_GETCOMMAND_ThreadArg* psArg, LONG* plRemainLen, LONG lWholeLen, UCHAR* pucRemainder) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	LONG lCommandEntry;
	sCOMM_DD_UART_Info* psUart = psArg->psUart;
	LONG lCommandsLen;
	LONG lCommandEntryLen;
	UCHAR* pucCommandEntry;

	/* for split the payload */
	UCHAR* pucCommand[CTRL_UART_GETCOMMAND_COMMANDCNTMAX];
	UCHAR** ppucCommand = pucCommand;

	LONG lFeedbackLen = 0;

	/* for precheck */
	UCHAR* pucPrefixHeader;

	CUG_FuncIn();

	{
		/* precheck the prefix "#" */
		eCOMM_STREAM_FindByte(psArg->pucPayLoad, "#", lWholeLen, &pucPrefixHeader);
		if (!pucPrefixHeader) {
			CUG_Debug("no prefix\n");
			eCOMM_DD_UART_Write(psUart, (UCHAR*)cFeedbackErrorNoPrefix, strlen(cFeedbackErrorNoPrefix));
			return CTRL_UART_GETCOMMAND_NOPREFIX;
		}
		
		/* split from the prefix */
		eCOMM_STREAM_Split(pucPrefixHeader, "#", lWholeLen, ppucCommand, &lCommandsLen);
		ppucCommand[lCommandsLen] = psArg->pucPayLoad + lWholeLen + 1;

		/* while loop handle the command */
		for(lCommandEntry = 0; lCommandEntry != lCommandsLen; lCommandEntry++) {
			pucCommandEntry = pucCommand[lCommandEntry];

			/* update the command entry length : the last 1 is '#' */
			lCommandEntryLen = pucCommand[lCommandEntry + 1] - pucCommand[lCommandEntry] - 1;

			/* regular the command entry length */
			eRet = eCTRL_UART_getCommand_RegularCommandLen(pucCommandEntry, &lCommandEntryLen);
			if (eRet != CTRL_UART_GETCOMMAND_SUCCESS) {
				CUG_Debug("regular command len error\n");
				eCOMM_DD_UART_Write(psUart, (UCHAR*)cFeedbackErrorNoPostfix, strlen(cFeedbackErrorNoPostfix));
				return eRet;
			}

			/* success, should reset the reminder */
			*plRemainLen = 0;

			/* put the payload to queue */
			vCOMM_LQ_Write(psArg->psCommandQueue, (CHAR*)pucCommandEntry, lCommandEntryLen);

			/* wait for feedback */
			vCOMM_LQ_Read(psArg->psFeedbackQueue, (CHAR**)&psArg->pucFeedBack);
			CUG_Debug("get feedback <%s>\n", psArg->pucFeedBack);

			/* feedback */
			lFeedbackLen = strlen((CHAR*)psArg->pucFeedBack);
			eCOMM_DD_UART_Write(psUart, psArg->pucFeedBack, lFeedbackLen);

			/* check command valid */
			eRet = eCTRL_UART_getCommand_CheckFeedback(psArg);
			if(eRet != CTRL_UART_GETCOMMAND_SUCCESS) {
				/* indicate something wrong with this command */
				if(lCommandEntry == (lCommandsLen - 1)) {
					/* indicate this is the last one, should backup for next parse */
					#ifdef CTRL_UART_GETCOMMAND_BACKUP
						memcpy((CHAR*)pucRemainder, (CHAR*)pucCommandEntry, lCommandEntryLen);
						*plRemainLen = lCommandEntryLen;
						CUG_Debug("this is the last one and error, will backup to next payload\n");
					#else
						*plRemainLen = 0;
						CUG_Debug("this is not backup mode\n");
					#endif
				}
			} else {
				/* the feedback is alright, turn the config status */
				if (!(*psArg->peStatus & CTRL_UART_GETCOMMAND_Status_Configed)) {
					/* in this case, must be C or L */
					assert(*pucCommandEntry == 'C'
						|| *pucCommandEntry == 'L'
						|| *pucCommandEntry == 'Q');
					*psArg->peStatus |= CTRL_UART_GETCOMMAND_Status_Configed;
				}
			}
		}
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_RegularCommandLen(UCHAR* pucCommand, LONG* plCommandLen)
* arg : UCHAR* pucCommand, LONG* plCommandLen
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : regular the command length
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_RegularCommandLen(UCHAR* pucCommand, LONG* plCommandLen) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	UCHAR* pucTail;
	LONG lCommandLen = *plCommandLen;

	CUG_FuncIn();

	{
		/* find the postfix */
		eCOMM_STREAM_FindByte(pucCommand, "\n", lCommandLen, &pucTail);
		if (!pucTail) {
			CUG_Debug("no postfix\n");
			return CTRL_UART_GETCOMMAND_NOPOSTFIX;
		}
		
		/* trim the tail */
		*pucTail = '\0';

		/* update the length */
		*plCommandLen = pucTail - pucCommand;
	}

	CUG_FuncOut();

	return eRet;
}

/*********************************************
* func : eCTRL_UART_getCommand_CheckFeedback(sCTRL_UART_GETCOMMAND_ThreadArg* psArg)
* arg : sCTRL_UART_GETCOMMAND_ThreadArg* psArg
* ret : eCTRL_UART_GETCOMMAND_Ret
* note : check the valid of feedback
*********************************************/
eCTRL_UART_GETCOMMAND_Ret eCTRL_UART_getCommand_CheckFeedback(sCTRL_UART_GETCOMMAND_ThreadArg* psArg) {
	eCTRL_UART_GETCOMMAND_Ret eRet = CTRL_UART_GETCOMMAND_SUCCESS;
	CHAR* pcFeedback = (CHAR*)psArg->pucFeedBack;
	LONG lFeedbackErrorLen = strlen(cFeedbackError);

	CUG_FuncIn();

	{
		if (!strncmp(pcFeedback, cFeedbackError, lFeedbackErrorLen)) {
			CUG_Debug("this is a error feedback\n");
			return CTRL_UART_GETCOMMAND_FEEDBACKERROR;
		}
	}

	CUG_FuncOut();

	return eRet;
}

#ifdef __cplusplus
}
#endif
