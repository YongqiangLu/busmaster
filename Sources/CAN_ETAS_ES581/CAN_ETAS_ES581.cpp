/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      CAN_ETAS_ES581.cpp
 * \brief     Exports API functions for ETAS ES581 CAN Hardware interface
 * \author    Pradeep Kadoor
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 *
 * Exports API functions for ETAS ES581 CAN Hardware interface
 */
#include "CAN_ETAS_ES581_stdafx.h"
#include "CAN_ETAS_ES581.h"
#include "include/Error.h"
#include "include/basedefs.h"
#include "DataTypes/Base_WrapperErrorLogger.h"
#include "DataTypes/MsgBufAll_DataTypes.h"
#include "DataTypes/DIL_Datatypes.h"
#include "Include/CAN_Error_Defs.h"
#include "Include/CanUsbDefs.h"
#include "Include/Struct_CAN.h"
#include "CAN_ETAS_ES581_Channel.h"
#include "CAN_ETAS_ES581_Network.h"
#include "Utility/Utility_Thread.h"
#include "Include/DIL_CommonDefs.h"
#include "ConfigDialogsDIL/API_dialog.h"
#include "icsnVC40.h"


#define USAGE_EXPORT
#include "CAN_ETAS_ES581_Extern.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//
//	Note!
//
//		If this DLL is dynamically linked against the MFC
//		DLLs, any functions exported from this DLL which
//		call into MFC must have the AFX_MANAGE_STATE macro
//		added at the very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the 
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//

// CCAN_ETAS_ES581App

BEGIN_MESSAGE_MAP(CCAN_ETAS_ES581App, CWinApp)
END_MESSAGE_MAP()


// CCAN_ETAS_ES581App construction

CCAN_ETAS_ES581App::CCAN_ETAS_ES581App()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}


// The one and only CCAN_ETAS_ES581App object

CCAN_ETAS_ES581App theApp;


// CCAN_ETAS_ES581App initialization

BOOL CCAN_ETAS_ES581App::InitInstance()
{
	CWinApp::InitInstance();

	return TRUE;
}

#define MAX_BUFF_ALLOWED 16
#define defSTR_MISSING_HARDWARE _T("Required number of hardware are not present\nRequired: %2d    Available: %2d")

const int CONFIGBYTES_TOTAL = 1024;
const BYTE CREATE_MAP_TIMESTAMP = 0x1;
const BYTE CALC_TIMESTAMP_READY = 0x2;
static BYTE sg_byCurrState = CREATE_MAP_TIMESTAMP; // Current state machine

/* Client and Client Buffer map */
struct tagClientBufMap
{
    DWORD dwClientID;
    BYTE hClientHandle;
    CBaseCANBufFSE* pClientBuf[MAX_BUFF_ALLOWED];
    TCHAR pacClientName[MAX_PATH];
    UINT unBufCount;
    tagClientBufMap()
    {
        dwClientID = 0;
        hClientHandle = NULL;
        unBufCount = 0;
        memset(pacClientName, 0, sizeof (TCHAR) * MAX_PATH);
        for (int i = 0; i < MAX_BUFF_ALLOWED; i++)
        {
            pClientBuf[i] = NULL;
        }

    }
};
typedef tagClientBufMap sClientBufMap;
typedef sClientBufMap SCLIENTBUFMAP;
typedef sClientBufMap* PSCLIENTBUFMAP;

typedef struct tagAckMap
{
    UINT m_MsgID;
    UINT m_ClientID;
    UINT m_Channel;

    BOOL operator == (const tagAckMap& RefObj)const
    {        
        return ((m_MsgID == RefObj.m_MsgID) && (m_Channel == RefObj.m_Channel)); 
    }
}SACK_MAP;

typedef std::list<SACK_MAP> CACK_MAP_LIST;
static CACK_MAP_LIST sg_asAckMapBuf;

/* Starts code for the state machine */

enum
{
    STATE_DRIVER_SELECTED    = 0x0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};
BYTE sg_bCurrState = STATE_DRIVER_SELECTED;

//#define VALIDATE_VALUE_RETURN_VAL(Val1, Val2, RetVal)   if (Val1 != Val2) {return RetVal;}

static SYSTEMTIME sg_CurrSysTime;
static UINT64 sg_TimeStamp = 0;
//Query Tick Count
static LARGE_INTEGER sg_QueryTickCount;
static HWND sg_hOwnerWnd = NULL;

//static CBaseCANBufFSE* sg_pCanBufObj[MAX_BUFF_ALLOWED];
static Base_WrapperErrorLogger* sg_pIlog   = NULL;

static CPARAM_THREADPROC sg_sParmRThread;
static s_STATUSMSG sg_sCurrStatus;
static CNetwork* sg_podActiveNetwork;
static CNetwork sg_odSimulationNetwork;
#define MAX_CANHW 16
//static BYTE sg_hHardware[ MAX_CANHW ];
// The message buffer
const int ENTRIES_IN_GBUF       = 2000;
static int sg_nFRAMES = 128;
static STCANDATA sg_asMsgBuffer[ENTRIES_IN_GBUF];
static SCONTROLER_DETAILS sg_ControllerDetails[defNO_OF_CHANNELS];
static INTERFACE_HW sg_HardwareIntr[defNO_OF_CHANNELS];
// Network for actual hardware
static CNetwork sg_odHardwareNetwork;
// Create time struct. Use 0 to transmit the message with out any delay
typedef struct
{
    DWORD millis;          // base-value: milliseconds: 0.. 2^32-1
    WORD  millis_overflow; // roll-arounds of millis
    WORD  micros;          // microseconds: 0..999
} TCANTimestamp;

static TCANTimestamp sg_sTime;
// static global variables ends

const int SIZE_WORD     = sizeof(WORD);
const int SIZE_CHAR     = sizeof(TCHAR);

// TZM specific Global variables
#define CAN_MAX_ERRSTR 256
#define MAX_CLIENT_ALLOWED 16
static char sg_acErrStr[CAN_MAX_ERRSTR] = {'\0'};
static UINT sg_unClientCnt = 0;
static SCLIENTBUFMAP sg_asClientToBufMap[MAX_CLIENT_ALLOWED];
static UINT sg_unCntrlrInitialised = 0;
static HMODULE sg_hDll = NULL;
static HANDLE m_hDataEvent = NULL;
static HANDLE sg_hCntrlStateChangeEvent = NULL;
static DWORD  sg_dwClientID = 0;

// Current buffer size
//static UINT sg_unMsgBufCount = 0;

// state variables
static BOOL sg_bIsConnected = FALSE;
static BOOL sg_bIsDriverRunning = FALSE;
static UCHAR sg_ucControllerMode = defUSB_MODE_ACTIVE;

// Count variables
static UCHAR sg_ucNoOfHardware = 0;

/*Please recheck and retain only necessary variables*/


#define NEW_LINE                _T("\n")
#define TOTAL_ERROR             600
#define MAX_BUFFER_VALUECAN     20000
#define WAITTIME_NEOVI          100

const int NEOVI_OK = 1;
const long VALUECAN_ERROR_BITS = SPY_STATUS_GLOBAL_ERR | SPY_STATUS_CRC_ERROR |
                       SPY_STATUS_INCOMPLETE_FRAME | SPY_STATUS_UNDEFINED_ERROR
                       | SPY_STATUS_BAD_MESSAGE_BIT_TIME_ERROR;

#define defSTR_ERROR_REPORT_FORMAT          _T("Channel %-2d : %s")
#define defSTR_WARNING_LIMIT_SET_FAILED     _T("Setting warning limit failed")
#define defSTR_FILTER_SET_FAILED            _T("Setting hardware filter failed")
#define defSTR_CONNECT_FAILED               _T("Connect failed")
#define defSTR_RESET_FAILED                 _T("Hardware reset failed")
#define defSTR_CLIENT_RESET_FAILED          _T("Software reset failed")
#define defSTR_CONTROLLER_MODE_SET_FAILED   _T("Setting hardware mode failed")

static UCHAR m_unWarningLimit[defNO_OF_CHANNELS] = {0};
static UCHAR m_unWarningCount[defNO_OF_CHANNELS] = {0};
static UCHAR m_unRxError = 0;
static UCHAR m_unTxError = 0;
const int MAX_DEVICES  = 255;
static int m_anDeviceTypes[MAX_DEVICES] = {0};
static int m_anComPorts[MAX_DEVICES] = {0};
static int m_anSerialNumbers[MAX_DEVICES] = {0};
static unsigned char m_ucNetworkID[16] = {0};
static int m_anhObject[MAX_DEVICES] = {0};
static TCHAR m_omErrStr[MAX_STRING] = {0};
static BOOL m_bInSimMode = FALSE;
//static CWinThread* m_pomDatInd = NULL;
static int s_anErrorCodes[TOTAL_ERROR] = {0};
/* Recheck ends */
/* Error Definitions */
#define CAN_USB_OK 0
#define CAN_QRCV_EMPTY 0x20
// First the error codes
const UINT PROC1Err = 0x1;
const UINT PROC2Err = 0x2;
const UINT PROC3Err = 0x4;
const UINT PROC4Err = 0x8;
const UINT PROC5Err = 0x10;
const UINT PROC6Err = 0x20;
const UINT PROC7Err = 0x40;
const UINT PROC8Err = 0x80;
const UINT PROC9Err = 0x100;
const UINT PROC10Err = 0x200;
const UINT PROC11Err = 0x400;
const UINT PROC12Err = 0x800;
const UINT PROC13Err = 0x1000;
const UINT PROC14Err = 0x2000;
const UINT PROC15Err = 0x4000;
const UINT PROC16Err = 0x8000;
const UINT ERR_LOADDRIVER = 0x10000;

// Function pointer definitions
typedef int (_stdcall *PMYPROC1)(int,int,int,int,int,int,unsigned char *,int *);
typedef int (_stdcall *PMYPROC2)(int,int);
typedef int (_stdcall *PMYPROC3)(int,int *);
typedef int (_stdcall *PMYPROC4)(int);
typedef int (_stdcall *PMYPROC5)(int,icsSpyMessage * ,int *,int *);
typedef int (_stdcall *PMYPROC6)(int,icsSpyMessage * ,int, int);
typedef int (_stdcall *PMYPROC7)(int, int *,int *);
typedef int (_stdcall *PMYPROC8)(int,TCHAR * ,TCHAR * ,int *,int *,int *,int *);
typedef int (_stdcall *PMYPROC9)(int,unsigned char * ,int);
typedef int (_stdcall *PMYPROC10)(int,unsigned char * ,int *);
typedef int (_stdcall *PMYPROC11)();
typedef int (_stdcall *PMYPROC12)(int,int, int, int, int * ,int *, int *, int *);
typedef int (_stdcall *PMYPROC13)(int, int *, int *, int *, int *, int *, int * , int *, int *);
typedef int (_stdcall *PMYPROC14)(int, unsigned int);
typedef int (_stdcall *PMYPROC15)(int, int, int);
typedef int (_stdcall *PMYPROC16)(int, icsSpyMessage*,double*);

// Function pointers
static PMYPROC1 pFuncPtrOpenPort = NULL;
static PMYPROC2 pFuncPtrEnableNet = NULL;
static PMYPROC3 pFuncPtrClosePort = NULL;
static PMYPROC4 pFuncPtrFreeObj = NULL;
static PMYPROC5 pFuncPtrGetMsgs = NULL;
static PMYPROC6 pFuncPtrTxMsgs = NULL;
static PMYPROC7 pFuncPtrGetErrMsgs = NULL;
static PMYPROC8 pFuncPtrGetErrInfo = NULL;
static PMYPROC9 pFuncPtrSendConfig = NULL;
static PMYPROC10 pFuncPtrGetConfig = NULL;
static PMYPROC11 pFuncPtrGetDllVer = NULL;
static PMYPROC12 pFuncPtrFindAllComDev = NULL;
static PMYPROC13 pFuncPtrGetPerPar = NULL;
static PMYPROC14 pFuncPtrWaitForRxMsg = NULL;
static PMYPROC15 pFuncPtrSetBitRate = NULL;
static PMYPROC16 pFuncPtrGetTimeStampForMsg = NULL;

#define MAX_CHAR_SHORT 128
#define MAX_CHAR_LONG  512
#define CAN_USBMSGTYPE_DATA 2

/* Function pointers ends*/
typedef struct tagDATINDSTR
{
    BOOL    m_bIsConnected;
    HANDLE  m_hHandle;
    BOOL    m_bToContinue;
    UINT    m_unChannels;
} sDatIndStr;

static sDatIndStr s_DatIndThread;


/******************************************************************************
Function Name  : ThreadDataInd
Input(s)       : LPVOID pParam - Function specific data
Output         : 0
Description    : Thread function 
Member of      : None
Functionality  : Indicates presence of data in the bus.
Author(s)      : Ratnadip Choudhury
Date Created   : 10.04.2008
Modifications  : Ratnadip Choudhury, 12.05.2008
                 Before calling 'icsneoWaitForRxMessagesWithTimeOut', we are 
                 now checking if the application is connected to the bus. At
                 present open port procedure is called only when the node /
                 application is connected to the bus.
******************************************************************************/
unsigned int ThreadDataInd(LPVOID pParam)
{
    sDatIndStr& sCurrStr = *((sDatIndStr *) pParam);
    DWORD dwResult = 0;

    while (sCurrStr.m_bToContinue)
    {
        if (sCurrStr.m_bIsConnected)
        {
            for (UINT i = 0; i < sCurrStr.m_unChannels; i++)
            {
                dwResult = (*pFuncPtrWaitForRxMsg)(m_anhObject[i], WAITTIME_NEOVI);
                if (dwResult > 0) /* At present a timeout of 100ms is applied */
                {
                    SetEvent(sCurrStr.m_hHandle);
                }
            }
        }
        else
        {
            WaitForSingleObject(sCurrStr.m_hHandle, 500);
        }
    }
    return 0;
}

HRESULT CAN_ES581_Usb_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj);

/* HELPER FUNCTIONS START */

/* Function to initialize all global data variables */
static void vInitialiseAllData(void)
{
    // Initialise both the time parameters
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0x0;
    //INITIALISE_DATA(sg_sCurrStatus);
    memset(&sg_sCurrStatus, 0, sizeof(sg_sCurrStatus));
    //Query Tick Count
    sg_QueryTickCount.QuadPart = 0;
    //INITIALISE_ARRAY(sg_acErrStr);
    memset(sg_acErrStr, 0, sizeof(sg_acErrStr));
    CAN_ES581_Usb_ManageMsgBuf(MSGBUF_CLEAR, NULL, NULL);
}
// Function create and set read indication event
static int nCreateAndSetReadIndicationEvent(HANDLE& hReadEvent)
{
    int nReturn = -1;

    // Create a event
    m_hDataEvent = CreateEvent( NULL,           // lpEventAttributes 
                                FALSE,          // bManualReset
                                FALSE,          // bInitialState
                                STR_EMPTY);     // Name
    if (m_hDataEvent != NULL)
    {
        s_DatIndThread.m_hHandle = m_hDataEvent;
        hReadEvent = m_hDataEvent;
        nReturn = defERR_OK;
    }
    return nReturn;
}
/* Function to retreive error occurred and log it */
static void vRetrieveAndLog(DWORD /*dwErrorCode*/, char* File, int Line)
{
    USES_CONVERSION;

    char acErrText[MAX_PATH] = {'\0'};
    // Get the error text for the corresponding error code
    //if ((*pfCAN_GetErrText)(dwErrorCode, acErrText) == CAN_USB_OK)
    {
        sg_pIlog->vLogAMessage(A2T(File), Line, A2T(acErrText));

        size_t nStrLen = strlen(acErrText);
        if (nStrLen > CAN_MAX_ERRSTR)
        {
            nStrLen = CAN_MAX_ERRSTR;
        }
        strncpy(sg_acErrStr, acErrText, nStrLen);
    }
}
static int nSetHardwareMode(UCHAR ucDeviceMode)
{
// Make sure to set the network to Hardware
    sg_podActiveNetwork = &sg_odHardwareNetwork;
    sg_ucControllerMode = ucDeviceMode;
    return 0;
}

/*******************************************************************************
 Function Name  : USB_vHandleErrorCounter
 Input(s)       : ucRxErr - Rx Error Counter Value
                  ucTxErr - Tx Error Counter Valie
 Output         : UCHAR - Type of the error message
                  Error Bus, Error Warning Limit and Error Interrupt
 Functionality  : Posts message as per the error counter. This function will
                  update local state variable as per error codes.
 Member of      : CHardwareInterface
 Author(s)      : Raja N
 Date Created   : 15.09.2004
 Modifications  : 
*******************************************************************************/
static UCHAR USB_ucHandleErrorCounter( UCHAR ucChannel,
                                                    UCHAR ucRxErr,
                                                    UCHAR ucTxErr )
{
    UCHAR ucRetVal = ERROR_BUS;

    CChannel& odChannel = sg_podActiveNetwork->m_aodChannels[ ucChannel ];

    // Check for Error Handler Execution
    // Warning Limit Execution
    if( ucRxErr == odChannel.m_ucWarningLimit &&
        odChannel.m_bRxErrorExecuted == FALSE )
    {
        // Set Error type as warning limit and set the handler execution
        // Flag
        ucRetVal = ERROR_WARNING_LIMIT_REACHED;
        odChannel.m_bRxErrorExecuted = TRUE;
    }
    // Tx Error Value
    else if( ucTxErr == odChannel.m_ucWarningLimit &&
             odChannel.m_bTxErrorExecuted == FALSE )
    {
        // Set Error type as warning limit and set the handler execution
        // Flag
        ucRetVal = ERROR_WARNING_LIMIT_REACHED;
        odChannel.m_bTxErrorExecuted = TRUE;
    }
    //  If the error counter value is 95 then execute the warning limit
    // handler if we are in warning limit state
    if( ucRxErr == odChannel.m_ucWarningLimit - 1 &&
        odChannel.m_bRxErrorExecuted == TRUE )
    {
        // Change the type. This is not real error message
        ucRetVal = ERROR_WARNING_LIMIT_REACHED;
        //ucRetVal = ERROR_UNKNOWN;
        odChannel.m_bRxErrorExecuted = FALSE;
    }
    if( ucTxErr == odChannel.m_ucWarningLimit - 1 &&
        odChannel.m_bTxErrorExecuted == TRUE )
    {
        // Change the type. This is not real error message
        ucRetVal = ERROR_WARNING_LIMIT_REACHED;
        //ucRetVal = ERROR_UNKNOWN;
        odChannel.m_bTxErrorExecuted = FALSE;
    }

    // Reset Event handlers state
    if( ucRxErr < odChannel.m_ucWarningLimit - 1)
    {
        odChannel.m_bRxErrorExecuted = FALSE;
    }
    if( ucTxErr < odChannel.m_ucWarningLimit - 1)
    {
        odChannel.m_bTxErrorExecuted = FALSE;
    }

    // Supress State Change Interrupt messages
    // Active -> Passive
    if( ucRxErr == 127 &&
            odChannel.m_ucControllerState == defCONTROLLER_PASSIVE )
    {
        // Not an error. This is interrupt message
        ucRetVal = ERROR_INTERRUPT;
    }

    // Active -> Passive
    if( ucTxErr == 127 &&
        odChannel.m_ucControllerState == defCONTROLLER_PASSIVE )
    {
        // Not an error. This is interrupt message
        ucRetVal = ERROR_INTERRUPT;
    }
    return ucRetVal;
}
/*******************************************************************************
 Function Name  : USB_ucGetErrorCode
 Input(s)       : lError - Error code in Peak USB driver format
                  byDir  - Error direction Tx/Rx
 Output         : UCHAR  - Error code in BUSMASTER application format
 Functionality  : This will convert the error code from Perk USB driver format
                  to the format that is used by BUSMASTER.
 Member of      : CHardwareInterface
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : 
*******************************************************************************/
static UCHAR USB_ucGetErrorCode(LONG lError, BYTE byDir)
{
    UCHAR ucReturn = 0;
    
    // Tx Errors
    if( byDir == 1)
    {
        if (lError & SPY_STATUS_CRC_ERROR)
        {
            ucReturn = BIT_ERROR_TX;
        }
        if (lError & SPY_STATUS_INCOMPLETE_FRAME )
        {
            ucReturn = FORM_ERROR_TX;
        }
        else 
        {
            ucReturn = OTHER_ERROR_TX;
        }
    }
    // Rx Errors
    else
    {
        if (lError & SPY_STATUS_CRC_ERROR)
        {
            ucReturn = BIT_ERROR_RX;
        }
        if (lError & SPY_STATUS_INCOMPLETE_FRAME)
        {
            ucReturn = FORM_ERROR_RX;
        }
        else 
        {
            ucReturn = OTHER_ERROR_RX;
        }
    }
    // Return the error code
    return ucReturn;
}

/* Function to create time mode mapping */
static void vCreateTimeModeMapping(HANDLE hDataEvent)
{   
   WaitForSingleObject(hDataEvent, INFINITE);
   //MessageBox(0, L"TIME", L"", 0);
   GetLocalTime(&sg_CurrSysTime);
   //Query Tick Count
   QueryPerformanceCounter(&sg_QueryTickCount);
    
}
static BOOL bLoadDataFromContr(PSCONTROLER_DETAILS pControllerDetails)
{
    BOOL bReturn = FALSE;    
    // If successful
    if (pControllerDetails != NULL)
    {
        for( int nIndex = 0; nIndex < defNO_OF_CHANNELS; nIndex++ )
        {
            TCHAR* pcStopStr = NULL;
            CChannel& odChannel = sg_odHardwareNetwork.m_aodChannels[ nIndex ];
            
            // Get Warning Limit
            odChannel.m_ucWarningLimit = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrWarningLimit,
                    &pcStopStr, defBASE_DEC ));
            // Get Acceptance Filter
            odChannel.m_sFilter.m_ucACC_Code0 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccCodeByte1,
                    &pcStopStr, defBASE_HEX ));
            odChannel.m_sFilter.m_ucACC_Code1 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccCodeByte2,
                    &pcStopStr, defBASE_HEX ));
            odChannel.m_sFilter.m_ucACC_Code2 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccCodeByte3,
                    &pcStopStr, defBASE_HEX ));
            odChannel.m_sFilter.m_ucACC_Code3 = static_cast <UCHAR>(
                    _tcstol(pControllerDetails[ nIndex ].m_omStrAccCodeByte4,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_sFilter.m_ucACC_Mask0 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccMaskByte1,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_sFilter.m_ucACC_Mask1 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccMaskByte2,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_sFilter.m_ucACC_Mask2 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccMaskByte3,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_sFilter.m_ucACC_Mask3 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrAccMaskByte4,
                    &pcStopStr, defBASE_HEX));        
            odChannel.m_sFilter.m_ucACC_Filter_Type = static_cast <UCHAR>(
                    pControllerDetails[ nIndex ].m_bAccFilterMode );
            odChannel.m_bCNF1 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrCNF1,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_bCNF2 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrCNF2,
                    &pcStopStr, defBASE_HEX));
            odChannel.m_bCNF3 = static_cast <UCHAR>(
                    _tcstol( pControllerDetails[ nIndex ].m_omStrCNF3,
                    &pcStopStr, defBASE_HEX));

            // Get Baud Rate
            odChannel.m_usBaudRate = static_cast <USHORT>(
                    pControllerDetails[ nIndex ].m_nBTR0BTR1 );

            odChannel.m_unBaudrate = _tstoi( pControllerDetails[ nIndex ].m_omStrBaudrate );

            odChannel.m_ucControllerState = pControllerDetails[ nIndex ].m_ucControllerMode;
        }
        // Get Controller Mode
        // Consider only the first channel mode as controller mode
        //sg_ucControllerMode = pControllerDetails[ 0 ].m_ucControllerMode;
        
        bReturn = TRUE;
    }
    return bReturn;
}

/*******************************************************************************
 Function Name  : vProcessError
 Input(s)       : icsSpyMessage& CurrSpyMsg - The current channel (in parameter)
                  STCANDATA& sCanData - Application specific data format 
                  (out parameter), USHORT ushRxErr - Number of Rx error, 
                  USHORT ushTxErr - Number of Tx error
 Output         : void
 Functionality  : Based on the number of Rx and Tx error, this will take the 
                  action necessary.
 Member of      : 
 Author(s)      : Ratnadip Choudhury
 Date Created   : 21.04.2008
*******************************************************************************/
static void vProcessError(STCANDATA& sCanData, CChannel& odChannel,
                                       USHORT ushRxErr, USHORT ushTxErr)
{
    sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType = 
        USB_ucHandleErrorCounter(0, (UCHAR)ushRxErr, (UCHAR)ushTxErr);

    // Update Error Type
    if (sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType == ERROR_INTERRUPT)
    {
        // This is interrupt message. So change the type
        sCanData.m_uDataInfo.m_sErrInfo.m_ucReg_ErrCap = ERROR_UNKNOWN;
    }

    // Update Rx Error Counter Value & Tx Error Counter Value
    odChannel.vUpdateErrorCounter((UCHAR)ushTxErr, (UCHAR)ushRxErr);
}
/*******************************************************************************
 Function Name  : bClassifyMsgType
 Input(s)       : icsSpyMessage& CurrSpyMsg - Message polled from the bus in 
                  neoVI format (in parameter), STCANDATA& sCanData - Application
                  specific data format (out parameter), UINT unChannel - channel
 Output         : TRUE (always)
 Functionality  : This will classify the messages, which can be one of Rx, Tx or
                  Error messages. In case of Err messages this identifies under
                  what broader category (Rx / Tx) does this occur.
 Member of      : 
 Author(s)      : Ratnadip Choudhury
 Date Created   : 21.04.2008
 Modifications  : Pradeep Kadoor on 12.05.2009.
                  RTR messages are handled.
*******************************************************************************/
static BYTE bClassifyMsgType(icsSpyMessage& CurrSpyMsg, 
                      STCANDATA& sCanData, UINT unChannel)
{
    if (CurrSpyMsg.StatusBitField & VALUECAN_ERROR_BITS)
    {
        sCanData.m_ucDataType = ERR_FLAG;
        // Set bus error as default error. This will be 
        // Modified by the function USB_ucHandleErrorCounter
        sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType = ERROR_BUS;
        // Assign the channel number
        sCanData.m_uDataInfo.m_sErrInfo.m_ucChannel = (UCHAR)unChannel;
        // Assign error type in the Error Capture register
        // and the direction of the error
        BOOL bIsTxMsg = FALSE;
        if (CurrSpyMsg.StatusBitField & SPY_STATUS_TX_MSG)
        {
            bIsTxMsg = TRUE;
        }
        sCanData.m_uDataInfo.m_sErrInfo.m_ucReg_ErrCap =
                USB_ucGetErrorCode(CurrSpyMsg.StatusBitField, (BYTE) bIsTxMsg);
        //explaination of error bit
        sCanData.m_uDataInfo.m_sErrInfo.m_nSubError= 0;
        // Update error counter values
        if (bIsTxMsg)
        {
            m_unTxError++;
        }
        else
        {
            m_unRxError++;
        }
        sCanData.m_uDataInfo.m_sErrInfo.m_ucRxErrCount = m_unRxError;
        sCanData.m_uDataInfo.m_sErrInfo.m_ucTxErrCount = m_unTxError;
    }
    else if (CurrSpyMsg.StatusBitField & SPY_STATUS_CAN_BUS_OFF)
    {
        sCanData.m_ucDataType = ERR_FLAG;

        // Set bus error as default error. This will be 
        // Modified by the function USB_ucHandleErrorCounter
        sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType = ERROR_BUS;
        // Assign the channel number
        sCanData.m_uDataInfo.m_sErrInfo.m_ucChannel = (UCHAR)unChannel;

        for (USHORT j = 0x01; j <= 0x80; j <<= 1)
        {
            if (CurrSpyMsg.Data[0] & j)
            {
                UCHAR ucRxCnt = sCanData.m_uDataInfo.m_sErrInfo.m_ucRxErrCount;
                UCHAR ucTxCnt = sCanData.m_uDataInfo.m_sErrInfo.m_ucTxErrCount;
                switch (j)
                {
                    case 0x01:
                    {
                        ucRxCnt = (ucRxCnt < 96) ? 96 : ucRxCnt;
                        ucTxCnt = (ucTxCnt < 96) ? 96 : ucTxCnt;
                    }
                    break;
                    case 0x02: ucRxCnt = (ucRxCnt < 96) ? 96 : ucRxCnt; break;
                    case 0x04: ucTxCnt = (ucTxCnt < 96) ? 96 : ucTxCnt; break;
                    case 0x08: ucTxCnt = (ucTxCnt < 128) ? 128 : ucTxCnt; break;
                    case 0x10: ucRxCnt = (ucRxCnt < 128) ? 128 : ucRxCnt; break;
                    case 0x20: ucTxCnt = (ucTxCnt < 255) ? 255 : ucTxCnt; break;
                    case 0x40:
                    case 0x80:
                    default: break;
                }
                sCanData.m_uDataInfo.m_sErrInfo.m_ucRxErrCount = ucRxCnt;
                sCanData.m_uDataInfo.m_sErrInfo.m_ucTxErrCount = ucTxCnt;
            }
        }
    }
    else
    {   
        //Check for RTR Message
        if (CurrSpyMsg.StatusBitField & SPY_STATUS_REMOTE_FRAME)
        {
            sCanData.m_ucDataType = RX_FLAG;
            sCanData.m_uDataInfo.m_sCANMsg.m_ucRTR = TRUE;
        }
        else
        {
            sCanData.m_uDataInfo.m_sCANMsg.m_ucRTR = FALSE;
        }
        if (CurrSpyMsg.StatusBitField & SPY_STATUS_TX_MSG)
        {
            sCanData.m_ucDataType = TX_FLAG;
        }
        else if (CurrSpyMsg.StatusBitField & SPY_STATUS_NETWORK_MESSAGE_TYPE)
        {
            sCanData.m_ucDataType = RX_FLAG;
        }        
        /*else
        {
            //ASSERT(FALSE);
        }*/
        // Copy data length
        sCanData.m_uDataInfo.m_sCANMsg.m_ucDataLen = CurrSpyMsg.NumberBytesData;

        // Copy the message data
        memcpy(sCanData.m_uDataInfo.m_sCANMsg.m_ucData, 
               CurrSpyMsg.Data, CurrSpyMsg.NumberBytesData);

        // Copy the message ID
        memcpy(&(sCanData.m_uDataInfo.m_sCANMsg.m_unMsgID),
               &(CurrSpyMsg.ArbIDOrHeader), sizeof(UINT));

        // Check for extended message indication
        sCanData.m_uDataInfo.m_sCANMsg.m_ucEXTENDED = 
             (CurrSpyMsg.StatusBitField & SPY_STATUS_XTD_FRAME) ? TRUE : FALSE;

        
    }
    return TRUE;
}
/*******************************************************************************
 Function Name  : nReadMultiMessage
 Input(s)       : psCanDataArray - Pointer to CAN Message Array of Structures
                  nMessage - Maximun number of message to read or size of the
                  CAN Message Array
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
                  nMessage - Actual Messages Read
 Functionality  : This function will read multiple CAN messages from the driver.
                  The other fuctionality is same as single message read. This
                  will update the variable nMessage with the actual messages
                  read.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Ratnadip Choudhury on 10.04.2008
                  Added neoVI specific codes.
 Modifications  : Ratnadip Choudhury on 13.05.2008
                  Solved system crash and application buffer overflow problems.
*******************************************************************************/
static int nReadMultiMessage(PSTCANDATA psCanDataArray,
                                          int &nMessage, int nChannelIndex)
{
    int i = 0;
    int nReturn = 0;
    static int errorCount = 0;
    // Now the data messages
    CChannel& odChannel = sg_odHardwareNetwork.m_aodChannels[nChannelIndex];
    static int s_CurrIndex = 0, s_Messages = 0;
    static icsSpyMessage s_asSpyMsg[MAX_BUFFER_VALUECAN] = {0};
    int nErrMsg = 0;
    if (s_CurrIndex == 0)
    {
        nReturn = (*pFuncPtrGetMsgs)(m_anhObject[nChannelIndex], s_asSpyMsg,
                                        &s_Messages, &nErrMsg);
        //TRACE("s_Messages: %d  nErrMsg: %d nMessage: %d\n", s_Messages, 
        //    nErrMsg, nMessage);
    }

    // Start of first level of error message processing
    USHORT ushRxErr = 0, ushTxErr = 0;
    if (nErrMsg > 0)
    {
        int nErrors = 0;
        nReturn = (*pFuncPtrGetErrMsgs)(m_anhObject[nChannelIndex], s_anErrorCodes, &nErrors);
        if ((nReturn == NEOVI_OK) && (nErrors > 0))
        {
            errorCount += nErrors;
            for (int j = 0; j < nErrors; j++)
            {
                switch (s_anErrorCodes[j])
                {
                    case NEOVI_ERROR_DLL_USB_SEND_DATA_ERROR:
                    {
                        ++ushTxErr;
                    }
                    break;
                    case NEOVI_ERROR_DLL_RX_MSG_FRAME_ERR:
                    case NEOVI_ERROR_DLL_RX_MSG_FIFO_OVER:
                    case NEOVI_ERROR_DLL_RX_MSG_CHK_SUM_ERR:
                    {
                        ++ushRxErr;
                    }
                    break;
                    default:
                    {
                        // Do nothing until further clarification is received
                    }
                    break;
                }
            }
        }
    }
    // End of first level of error message processing

    // START
    /* Create the time stamp map. This means getting the local time and assigning 
    offset value to the QuadPart.
    */
    static LONGLONG QuadPartRef = 0;

    if (CREATE_MAP_TIMESTAMP == sg_byCurrState)
    {
        //ASSERT(s_CurrIndex < s_Messages);

        //CTimeManager::bReinitOffsetTimeValForES581();
        icsSpyMessage& CurrSpyMsg = s_asSpyMsg[s_CurrIndex];
        DOUBLE dTimestamp = 0;
        nReturn = (*pFuncPtrGetTimeStampForMsg)(m_anhObject[nChannelIndex], &CurrSpyMsg, &dTimestamp);
        if (nReturn == NEOVI_OK)
        {
            QuadPartRef = (LONGLONG)(dTimestamp * 10000);//CurrSpyMsg.TimeHardware2 * 655.36 + CurrSpyMsg.TimeHardware * 0.01;
            sg_byCurrState = CALC_TIMESTAMP_READY;
            nReturn = defERR_OK;
        }
        else
        {
            nReturn = -1;
        }
    }
    
    // END

    int nLimForAppBuf = nMessage;//MIN(nMessage, s_Messages);
    double dCurrTimeStamp;
    for (/*int i = 0*/; (i < nLimForAppBuf) && (s_CurrIndex < s_Messages); i++)
    {
        STCANDATA& sCanData = psCanDataArray[i];
        icsSpyMessage& CurrSpyMsg = s_asSpyMsg[s_CurrIndex];

        sCanData.m_uDataInfo.m_sCANMsg.m_ucChannel = (UCHAR)(nChannelIndex + 1);
        nReturn = (*pFuncPtrGetTimeStampForMsg)(m_anhObject[nChannelIndex], &CurrSpyMsg, &dCurrTimeStamp);
        /*sCanData.m_lTickCount.QuadPart = (CurrSpyMsg.TimeHardware2 * 655.36 
                                        + CurrSpyMsg.TimeHardware * 0.01);*/
        sCanData.m_lTickCount.QuadPart = (LONGLONG)(dCurrTimeStamp * 10000);
        sg_TimeStamp = sCanData.m_lTickCount.QuadPart = 
                                (sCanData.m_lTickCount.QuadPart - QuadPartRef);

        bClassifyMsgType(CurrSpyMsg, sCanData, sCanData.m_uDataInfo.m_sCANMsg.m_ucChannel);

        if (sCanData.m_ucDataType == ERR_FLAG)
        {
            vProcessError(sCanData, odChannel, ushRxErr + 1, ushTxErr + 1);
            // Reset to zero
            ushRxErr = 0; ushTxErr = 0;
        }
        s_CurrIndex++;
    }
    //TRACE("s_CurrIndex: %d i: %d\n", s_CurrIndex, i);

    //ASSERT(!(s_CurrIndex > MAX_BUFFER_VALUECAN));
    //ASSERT(!(s_CurrIndex > s_Messages));

    if ((s_CurrIndex == MAX_BUFFER_VALUECAN) || (s_CurrIndex == s_Messages))
    {
        s_CurrIndex = 0;
        s_Messages = 0;
    }

    // This code is needed when error messages don't occur in the list of 
    // the regular message
    if ((ushRxErr != 0) || (ushTxErr != 0))
    {
        STCANDATA sCanData;
        vProcessError(sCanData, odChannel, ushRxErr, ushTxErr);
    }

        
    nMessage = i;

    return defERR_OK; // Multiple return statements had to be added because
    // neoVI specific codes and simulation related codes need to coexist. 
    // Code for the later still employs Peak API interface.
}
static BOOL bGetClientObj(DWORD dwClientID, UINT& unClientIndex)
{
    BOOL bResult = FALSE;
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].dwClientID == dwClientID)
        {
            unClientIndex = i;
            i = sg_unClientCnt; //break the loop
            bResult = TRUE;
        }
    }
    return bResult;
}

static BOOL bRemoveClient(DWORD dwClientId)
{
    BOOL bResult = FALSE;
    if (sg_unClientCnt > 0)
    {
        UINT unClientIndex = (UINT)-1;
        if (bGetClientObj(dwClientId, unClientIndex))
        {
            //clear the client first            
            if (sg_asClientToBufMap[unClientIndex].hClientHandle != NULL)
            {
                HRESULT hResult = S_OK;//(*pfCAN_RemoveClient)(sg_asClientToBufMap[unClientIndex].hClientHandle);
                if (hResult == S_OK)
                {
                    sg_asClientToBufMap[unClientIndex].dwClientID = 0;
                    sg_asClientToBufMap[unClientIndex].hClientHandle = NULL;
                    memset (sg_asClientToBufMap[unClientIndex].pacClientName, 0, sizeof (TCHAR) * MAX_PATH);
                    for (int i = 0; i < MAX_BUFF_ALLOWED; i++)
                    {
                        sg_asClientToBufMap[unClientIndex].pClientBuf[i] = NULL;
                    }
                    sg_asClientToBufMap[unClientIndex].unBufCount = 0;
                    bResult = TRUE;                    
                }
                else
                {
                    vRetrieveAndLog(hResult, __FILE__, __LINE__);
                }
            }
            else
            {
                sg_asClientToBufMap[unClientIndex].dwClientID = 0;
                memset (sg_asClientToBufMap[unClientIndex].pacClientName, 0, sizeof (TCHAR) * MAX_PATH);
                for (int i = 0; i < MAX_BUFF_ALLOWED; i++)
                {
                    sg_asClientToBufMap[unClientIndex].pClientBuf[i] = NULL;
                }
                sg_asClientToBufMap[unClientIndex].unBufCount = 0;
                bResult = TRUE;

            }
            if (bResult == TRUE)
            {
                if ((unClientIndex + 1) < sg_unClientCnt)
                {
                    sg_asClientToBufMap[unClientIndex] = sg_asClientToBufMap[sg_unClientCnt - 1];
                }
                sg_unClientCnt--;
            }
        }
    }
    return bResult;
}
static BOOL bRemoveClientBuffer(CBaseCANBufFSE* RootBufferArray[MAX_BUFF_ALLOWED], UINT& unCount, CBaseCANBufFSE* BufferToRemove)
{
    BOOL bReturn = TRUE;
    for (UINT i = 0; i < unCount; i++)
    {
        if (RootBufferArray[i] == BufferToRemove)
        {
            if (i < (unCount - 1)) //If not the last bufffer
            {
                RootBufferArray[i] = RootBufferArray[unCount - 1];
            }
            unCount--;
        }
    }
    return bReturn;
}
void vMarkEntryIntoMap(const SACK_MAP& RefObj)
{
    sg_asAckMapBuf.push_back(RefObj);
}

BOOL bRemoveMapEntry(const SACK_MAP& RefObj, UINT& ClientID)
{   
    BOOL bResult = FALSE;
    CACK_MAP_LIST::iterator  iResult = 
        std::find( sg_asAckMapBuf.begin(), sg_asAckMapBuf.end(), RefObj );  
 
    if ((*iResult).m_ClientID > 0)
    {
        bResult = TRUE;
        ClientID = (*iResult).m_ClientID;
        sg_asAckMapBuf.erase(iResult);
    }
    return bResult;
}

/* This function writes the message to the corresponding clients buffer */
static void vWriteIntoClientsBuffer(STCANDATA& sCanData)
{
     //Write into the client's buffer and Increment message Count
    if (sCanData.m_ucDataType == TX_FLAG)
    {
        static SACK_MAP sAckMap;
        UINT ClientId = 0;
        static UINT Index = (UINT)-1;
        sAckMap.m_Channel = sCanData.m_uDataInfo.m_sCANMsg.m_ucChannel;
        sAckMap.m_MsgID = sCanData.m_uDataInfo.m_sCANMsg.m_unMsgID;

        if (bRemoveMapEntry(sAckMap, ClientId))
        {
            BOOL bClientExists = bGetClientObj(ClientId, Index);
            for (UINT i = 0; i < sg_unClientCnt; i++)
            {
                //Tx for monitor nodes and sender node
                if ((i == CAN_MONITOR_NODE_INDEX)  || (bClientExists && (i == Index)))
                {
                    for (UINT j = 0; j < sg_asClientToBufMap[i].unBufCount; j++)
                    {
                        sg_asClientToBufMap[i].pClientBuf[j]->WriteIntoBuffer(&sCanData);
                    }
                }
                else
                {
                    //Send the other nodes as Rx.
                    for (UINT j = 0; j < sg_asClientToBufMap[i].unBufCount; j++)
                    {
                        static STCANDATA sTempCanData;
                        sTempCanData = sCanData;
                        sTempCanData.m_ucDataType = RX_FLAG;
                        sg_asClientToBufMap[i].pClientBuf[j]->WriteIntoBuffer(&sTempCanData);
                    }
                }
            }
        }
    }
    else // provide it to everybody
    {
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            for (UINT j = 0; j < sg_asClientToBufMap[i].unBufCount; j++)
            {
                sg_asClientToBufMap[i].pClientBuf[j]->WriteIntoBuffer(&sCanData);
            }
        }
    }
}

/* Processing of the received packets from bus */
static void ProcessCANMsg(int nChannelIndex)
{
    int nSize = sg_nFRAMES;
    if (nReadMultiMessage(sg_asMsgBuffer, nSize, nChannelIndex) == defERR_OK)
    {
        for (INT unCount = 0; unCount < nSize; unCount++)
        {
            vWriteIntoClientsBuffer(sg_asMsgBuffer[unCount]);
        }
    }
}

/* Read thread procedure */

DWORD WINAPI CanMsgReadThreadProc_CAN_Usb(LPVOID pVoid)
{
    USES_CONVERSION;

    CPARAM_THREADPROC* pThreadParam = (CPARAM_THREADPROC *) pVoid;

    // Validate certain required pointers
    VALIDATE_POINTER_RETURN_VALUE_LOG(pThreadParam, (DWORD)-1);
    // Assign thread action to CREATE_TIME_MAP
    pThreadParam->m_unActionCode = CREATE_TIME_MAP;
    // Set the event to CAN_USB driver for wakeup and frame arrival notification
    nCreateAndSetReadIndicationEvent(pThreadParam->m_hActionEvent);

    // Get the handle to the controller and validate it
    VALIDATE_POINTER_RETURN_VALUE_LOG(pThreadParam->m_hActionEvent, (DWORD)-1);

    DWORD dwResult = 0;
    while (s_DatIndThread.m_bToContinue)
    {
        if (s_DatIndThread.m_bIsConnected)
        {
            for (int i = 0; i < sg_ucNoOfHardware; i++)
            {
                dwResult = (*pFuncPtrWaitForRxMsg)(m_anhObject[i], WAITTIME_NEOVI);
                //kadoor WaitForSingleObject(pThreadParam->m_hActionEvent, INFINITE);
                if (dwResult > 0)
                {
                    switch (pThreadParam->m_unActionCode)
                    {
                        case INVOKE_FUNCTION:
                        {
                            ProcessCANMsg(i); // Retrieve message from the driver
                        }
                        break;
                        case CREATE_TIME_MAP:
                        {
                            sg_byCurrState = CREATE_MAP_TIMESTAMP;
                            SetEvent(pThreadParam->m_hActionEvent);
                            vCreateTimeModeMapping(pThreadParam->m_hActionEvent);
                            ProcessCANMsg(i);
                            pThreadParam->m_unActionCode = INVOKE_FUNCTION;
                        }
                        break;
                        default:
                        case INACTION:
                        {
                            // nothing right at this moment
                        }
                        break;
                    }
                }
            }
        }
        else
        {
            WaitForSingleObject(pThreadParam->m_hActionEvent, 500);
            switch (pThreadParam->m_unActionCode)
            {
                case EXIT_THREAD:
                {
                    s_DatIndThread.m_bToContinue = FALSE;
                }
                break;
            }
        }
    }
    SetEvent(pThreadParam->hGetExitNotifyEvent());

    // Close the notification event for startup
    CloseHandle(pThreadParam->m_hActionEvent);
    pThreadParam->m_hActionEvent = NULL;

    return 0;
}


/*******************************************************************************
  Function Name  : nCreateMultipleHardwareNetwork
  Input(s)       : -
  Output         : -
  Functionality  : This function will get the hardware selection from the user
                   and will create essential networks.
  Member of      : 
  Author(s)      : Pradeep Kadoor
  Date Created   : 14.06.2009
  Modifications  : 
*******************************************************************************/
static int nCreateMultipleHardwareNetwork()
{
	TCHAR acTempStr[256] = {_T('\0')};
	INT anSelectedItems[defNO_OF_CHANNELS] = {0};
	int nHwCount = sg_ucNoOfHardware;
    // Get Hardware Network Map
	for (int nCount = 0; nCount < nHwCount; nCount++)
	{
		sg_HardwareIntr[nCount].m_dwIdInterface = m_anComPorts[nCount];
        sg_HardwareIntr[nCount].m_dwVendor = m_anSerialNumbers[nCount];
		_stprintf(acTempStr, _T("Serial Number: %d, Port ID: %d"), m_anSerialNumbers[nCount], m_anComPorts[nCount]);
		_tcscpy(sg_HardwareIntr[nCount].m_acDescription, acTempStr);
	}
	ListHardwareInterfaces(sg_hOwnerWnd, DRIVER_CAN_ETAS_ES581, sg_HardwareIntr, anSelectedItems, nHwCount);
    sg_ucNoOfHardware = (UCHAR)nHwCount;
	//Reorder hardware interface as per the user selection
	for (int nCount = 0; nCount < sg_ucNoOfHardware; nCount++)
	{
		m_anComPorts[nCount] = (int)sg_HardwareIntr[anSelectedItems[nCount]].m_dwIdInterface;
        m_anSerialNumbers[nCount] = (int)sg_HardwareIntr[anSelectedItems[nCount]].m_dwVendor;
	}
    for (int nIndex = 0; nIndex < sg_ucNoOfHardware; nIndex++)
    {   
        if (nIndex == 0)// AFXBeginThread should be called only once.
        {
            s_DatIndThread.m_bToContinue = TRUE;
            s_DatIndThread.m_bIsConnected = FALSE;
            s_DatIndThread.m_unChannels = sg_ucNoOfHardware;
			sg_odHardwareNetwork.m_nNoOfChannels = (int)sg_ucNoOfHardware;
        }	
			
		// Assign hardware handle
		sg_odHardwareNetwork.m_aodChannels[ nIndex ].m_hHardwareHandle = (BYTE)m_anComPorts[nIndex];
			
		// Assign Net Handle
		sg_odHardwareNetwork.m_aodChannels[ nIndex ].m_hNetworkHandle = m_ucNetworkID[nIndex];
    }
    return defERR_OK;
}
/******************************************************************************
  Function Name  : nCreateSingleHardwareNetwork
  Input(s)       : -
  Output         : -
  Functionality  : This is USB Specific Function. This will create a single
                   network with available single hardware.
  Member of      : 
  Author(s)      : Raja N
  Date Created   : 9.3.2005
  Modifications  : Raja N on 17.03.2005, Modified the function name to correct
                   spelling mistake
  Modifications  : Pradeep Kadoor on 09.05.2008, OpePort function is no more
                   called from here.
******************************************************************************/
static int nCreateSingleHardwareNetwork()
{
    // Create network here with net handle 1
    s_DatIndThread.m_bToContinue = TRUE;
    s_DatIndThread.m_bIsConnected = FALSE;
    s_DatIndThread.m_unChannels = 1;

    // Set the number of channels
    sg_odHardwareNetwork.m_nNoOfChannels = 1;
    // Assign hardware handle
    sg_odHardwareNetwork.m_aodChannels[ 0 ].m_hHardwareHandle = (BYTE)m_anComPorts[0];
        
    // Assign Net Handle
    sg_odHardwareNetwork.m_aodChannels[ 0 ].m_hNetworkHandle = m_ucNetworkID[1];        
    
    return defERR_OK;
}
/*******************************************************************************
 Function Name  : nGetNoOfConnectedHardware
 Input(s)       : nHardwareCount - Hardware Count
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
 Functionality  : Finds the number of hardware connected. This is applicable
                  only for USB device. For parallel port this is not required.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 14.03.2005, Removed assigning hardware index to
                  invalid value as architecture is changed to support multiple
                  hardware.
 Modifications  : Ratnadip Choudhury, 10.04.2008
                  Replaced Peak specific code with neoVI specific codes
*******************************************************************************/
static int nGetNoOfConnectedHardware(int& nHardwareCount)
{
    // 0, Query successful, but no device found
    // > 0, Number of devices found
    // < 0, query for devices unsucessful
    int nReturn = 0;

    for (BYTE i = 0; i < 16; i++)
    {
        m_ucNetworkID[i] = i;
    }
    // TODO: Add your command handler code here
    if ((*pFuncPtrFindAllComDev)(INTREPIDCS_DRIVER_STANDARD, 1, 0, 1,
            m_anDeviceTypes, m_anComPorts, m_anSerialNumbers, &nReturn))
    {
        if (nReturn == 0)
        {
            _tcscpy(m_omErrStr,_T("Query successful, but no device found"));
        }
        else
        {
            nHardwareCount = nReturn;
        }
    }
    else
    {
        nReturn = -1;
        _tcscpy(m_omErrStr,_T("Query for devices unsuccessful"));
    }
    // Return the operation result
    return nReturn;
}

/*******************************************************************************
 Function Name  : nInitHwNetwork
 Input(s)       : -
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
                  nMessage - Actual Messages Read
 Functionality  : This is USB Specific function.This function will create
                  find number of hardware connected. It will create network as
                  per hardware count. This will popup hardware selection dialog
                  in case there are more hardware present.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Raja N on 17.3.2005
                  Modifications as per code review comment
 Modifications  : Anish Kumar on 13.09.2006.
                  Added code for external evaluation copy 
 Modifications  : Pradeep Kadoor on 12.05.2009.
                  Added a message box to display number of hardware present.                  
                    
*******************************************************************************/
static int nInitHwNetwork()
{
    int nDevices = 0;
    int nReturn = -1;
    // Select Hardware
    // This function will be called only for USB mode
    nReturn = nGetNoOfConnectedHardware(nDevices);
    m_bInSimMode = (nDevices == 0);

    // Assign the device count
    sg_ucNoOfHardware = (UCHAR)nDevices;
    // Capture only Driver Not Running event
    // Take action based on number of Hardware Available
    TCHAR acNo_Of_Hw[MAX_STRING] = {0};
    _stprintf(acNo_Of_Hw, _T("Number of ES581s Available: %d"), nDevices);
    // No Hardware found
    
    if( nDevices == 0 )
    {
        MessageBox(NULL,m_omErrStr, NULL, MB_OK | MB_ICONERROR);
        nReturn = -1;
    }
    // Available hardware is lesser then the supported channels
    else 
    {
        // Check whether channel selection dialog is required
        if( nDevices > 1 )
        {
            // Get the selection from the user. This will also
            // create and assign the networks
            nReturn = nCreateMultipleHardwareNetwork();
        }
        else
        {
            // Use available one hardware
            nReturn = nCreateSingleHardwareNetwork();
        }
    }
    return nReturn;
}

/*******************************************************************************
 Function Name  : nConnectToDriver
 Input(s)       : -
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
 Functionality  : This function will initialise hardware handlers of USB. This
                  will establish the connection with driver. This will create or
                  connect to the network. This will create client for USB. For
                  parallel port mode this will initialise connection with the
                  driver.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel archtecture. Added
                  code to init parallel port channel.
*******************************************************************************/
static int nConnectToDriver()
{
    int nReturn = -1;
    
    if( sg_bIsDriverRunning == TRUE )
    {
        // Select Hardware or Simulation Network
        nReturn = nInitHwNetwork();
    }
    return nReturn;
}

/*******************************************************************************
 Function Name  : bGetDriverStatus
 Input(s)       : -
 Output         : BOOL - TRUE if the driver is running.
                  FALSE - IF it is not running
 Functionality  : 
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 16.09.2004, Modification to change the device name
                  if the device is not running
 Modifications  : Ratnadip Choudhury, 10.04.2008 
                  For ES581 just returning m_bIsDriverRunning
*******************************************************************************/
static BOOL bGetDriverStatus()
{
    sg_bIsDriverRunning = TRUE;
    return TRUE;
}

/*******************************************************************************
 Function Name  : nSetBaudRate
 Input(s)       : usBaud - Baud rate in BTR0BTR1 format
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
 Functionality  : This function will set the baud rate of the controller
                  Parallel Port Mode: Controller will be initialised with all
                  other parameters after baud rate change.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 16.09.2004, Added check before assignment of new
                  value to the member variable.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Ratnadip Choudhury on 21.04.2008
                  Modification to support valueCAN (ES581)
*******************************************************************************/
static int nSetBaudRate()
{
    int nReturn = 0;

    // Set baud rate to all available hardware
    for( UINT unIndex = 0;
         unIndex < sg_odHardwareNetwork.m_nNoOfChannels;
         unIndex++)
    {
        FLOAT fBaudRate = (FLOAT)_tstof(sg_ControllerDetails[unIndex].m_omStrBaudrate);
        int nBitRate = (INT)(fBaudRate * 1000);
        // disconnect the COM
        (*pFuncPtrEnableNet)(m_anhObject[unIndex], 0);

        // Set the baudrate
        nReturn = (*pFuncPtrSetBitRate)(m_anhObject[unIndex], nBitRate, NETID_HSCAN);
        // connect the COM
        (*pFuncPtrEnableNet)(m_anhObject[unIndex], 1);
        if (nReturn != NEOVI_OK)
        {
            unIndex = sg_odHardwareNetwork.m_nNoOfChannels;
        }
        else
        {
            nReturn = 0;
        }
    }
    
    return nReturn;
}

/*******************************************************************************
 Function Name  : nSetWarningLimit
 Input(s)       : ucWarninglimit - Warning Limit Value
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
 Functionality  : This function will set the warning limit of the controller. In
                  USB mode this is not supported as the warning limit is
                  internally set to 96
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 16.09.2004, Added check before assignment of new
                  value to the member variable.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Raja N on 17.3.2005
                  Implemented code review comments
*******************************************************************************/
static int nSetWarningLimit()
{
    for (UINT i = 0; i < sg_podActiveNetwork->m_nNoOfChannels; i++)
    {
        CChannel& odChannel = sg_podActiveNetwork->m_aodChannels[i];
        m_unWarningLimit[i] = odChannel.m_ucWarningLimit;
    }
    return defERR_OK; // Multiple return statements had to be added because
    // neoVI specific codes and simulation related codes need to coexist. 
    // Code for the later still employs Peak API interface.
}

/*******************************************************************************
 Function Name  : nSetFilter
 Input(s)       : sFilter - Filter Structure
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This function will set the filter information.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 16.09.2004, Added code to check return value and
                  update only on success.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel.
 Modifications  : Raja N on 17.3.2005
                  Implemented code review comments.
 Modifications  : Changed the filter code back to previous as dual filter is 
                  not working in USB mode
*******************************************************************************/
static int nSetFilter( )
{
    return 0;
}

/*******************************************************************************
 Function Name  : nSetApplyConfiguration
 Input(s)       : -
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This function will set all controller parameters. This will
                  set Baud rate, Filter, Warning Limit and Controller Mode. In
                  case of USB the warning limit call will be ignored.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 07.10.2004
                  Seprated Parallel port code to set baud rate irrespective of
                  controller mode. This is for the fix of the bug of controller
                  kept uninitialised if the loaded configuration has
                  self-reception mode.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel.
 Modifications  : Raja N on 22.3.2005
                  Modifications after testing. Check previous result before
                  setting baud rate and warning limit to avoid multiple error
                  messages.
 Modifications  : Pradeep Kadoor on 12.09.2008
                  Setting of baud rate is removed as it is called 
                  when connect button is pressed.
*******************************************************************************/
static int nSetApplyConfiguration()
{
    int nReturn = defERR_OK;

    // Set Hardware Mode
    if ((nReturn = nSetHardwareMode(sg_ucControllerMode)) == defERR_OK)
    {
        // Set Filter
        nReturn = nSetFilter();
    }
    // Set warning limit only for hardware network
    if ((nReturn == defERR_OK) && (sg_ucControllerMode != defUSB_MODE_SIMULATE))
    {
        // Set Warning Limit
        nReturn = nSetWarningLimit();
    }

    return nReturn;
}


/*******************************************************************************
 Function Name  : nTestHardwareConnection
 Input(s)       : ucaTestResult - Array that will hold test result. TRUE if
                  hardware present and false if not connected  
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This function will check all hardware connectivity by getting
                  hardware parameter. In parallel port mode this will set the
                  baud rate to test hardware presence.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel.
*******************************************************************************/
static int nTestHardwareConnection(UCHAR& ucaTestResult, UINT nChannel) //const
{
    BYTE aucConfigBytes[CONFIGBYTES_TOTAL];
    int nReturn = 0;
    int nConfigBytes = 0;
    if (nChannel < sg_odHardwareNetwork.m_nNoOfChannels)
    {
        if ((pFuncPtrGetConfig(m_anhObject[nChannel], aucConfigBytes, &nConfigBytes) == NEOVI_OK))
        {
            ucaTestResult = TRUE;
        }
        else
        {            
            sg_bIsConnected = FALSE;
            ucaTestResult = FALSE;
        }
    }
    return nReturn;
}
/*******************************************************************************
 Function Name  : nDisconnectFromDriver
 Input(s)       : -
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This will close the connection with the driver. This will be
                  called before deleting HI layer. This will be called during
                  application close.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Ratnadip Choudhury on 10.04.2008
                  Added neoVI specific codes.
*******************************************************************************/
static int nDisconnectFromDriver()
{
    int nReturn = 0;
    sg_bIsDriverRunning = FALSE;

    int nErrors = 0;
    for (BYTE i = 0; i < sg_ucNoOfHardware; i++)
    {
        if (m_anhObject[i] != 0)
        {
            // First disconnect the COM
            //(*pFuncPtrFreeObj)(m_anhObject[i]);

            if ((*pFuncPtrClosePort)(m_anhObject[i], &nErrors) == 1)
            {
                m_anhObject[i] = 0;
            }
            else
            {
                nReturn = -1;
            }
        }
    }

    return nReturn;
}
/*******************************************************************************
 Function Name  : nConnect
 Input(s)       : bConnect - TRUE to Connect, FALSE to Disconnect
 Output         : int - Returns defERR_OK if successful otherwise corresponding
                  Error code.
 Functionality  : This function will connect the tool with hardware. This will
                  establish the data link between the application and hardware.
                  Parallel Port Mode: Controller will be disconnected.
                  USB: Client will be disconnected from the network
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Anish on 27.11.2006
                  CAN_PARAM_SELF_RECEIVE parameter setting code is removed
                  as setting CAN_PARAM_MARK_SELFRECEIVED_MSG_WITH_MSGTYPE flag
                  fulfil the requirement
 Modifications  : Ratnadip Choudhury on 10.04.2008
                  Added neoVI specific codes.
 Modifications  : Pradeep Kadoor on 09.05.2008
                  Calling functions OpenPort and ClosePort respectively when 
                  values of bConnect are TRUE and FALSE.
*******************************************************************************/
static int nConnect(BOOL bConnect, BYTE /*hClient*/)
{
    int nReturn = -1;
    BYTE aucConfigBytes[CONFIGBYTES_TOTAL];
    int nConfigBytes = 0;
    
	if (!sg_bIsConnected && bConnect) // Disconnected and to be connected
    {
        for (UINT i = 0; i < sg_podActiveNetwork->m_nNoOfChannels; i++)
        {
            nReturn = (*pFuncPtrOpenPort)(m_anComPorts[i],
                NEOVI_COMMTYPE_RS232, INTREPIDCS_DRIVER_STANDARD, 0, 57600,
                1, m_ucNetworkID, &(m_anhObject[i]));
            
            if (nReturn != NEOVI_OK)
            {
                // In such a case it is wiser to carry out disconnecting 
                nDisconnectFromDriver(); // operations from the driver

                // Try again
                (*pFuncPtrOpenPort)(m_anComPorts[i], NEOVI_COMMTYPE_RS232,
                                    INTREPIDCS_DRIVER_STANDARD, 0, 57600, 1,
                                    m_ucNetworkID, &(m_anhObject[i]));

                /* Checking return value of this function is not important 
                because GetConfig function (called to check for the hardware 
                presence) renders us the necessary information */
                nReturn = (*pFuncPtrGetConfig)(m_anhObject[i], aucConfigBytes,
                    &nConfigBytes);
            }
            if (nReturn == NEOVI_OK) // Hardware is present
            {
                // OpenPort and this function must be called together.
                // CTimeManager::bReinitOffsetTimeValForES581();
                // Transit into 'CREATE TIME MAP' state
                sg_byCurrState = CREATE_MAP_TIMESTAMP;
                if (i == (sg_podActiveNetwork->m_nNoOfChannels -1))
                {
                    //Only at the last it has to be called
                    nSetBaudRate();
                    sg_bIsConnected = bConnect;
                    s_DatIndThread.m_bIsConnected = sg_bIsConnected;
                }
                nReturn = defERR_OK;
            }
            else // Hardware is not present
            {
                
                // Display the error message when not called from COM interface                
            }
        }
    }
    else if (sg_bIsConnected && !bConnect) // Connected & to be disconnected
    {
        sg_bIsConnected = bConnect;
        s_DatIndThread.m_bIsConnected = sg_bIsConnected;
        Sleep(0); // Let other threads run for once
        nReturn = nDisconnectFromDriver();
    }
    else 
    {
        nReturn = defERR_OK;
    }

    return nReturn;
}

/* Function to set API function pointers */
HRESULT GetETAS_ES581_APIFuncPtrs(void)
{
    LRESULT unResult = 0;
    if (sg_hDll != NULL)
    {
        pFuncPtrOpenPort = (PMYPROC1) GetProcAddress(sg_hDll, "icsneoOpenPortEx");
        if (NULL == pFuncPtrOpenPort)
        {              
            unResult = unResult | PROC1Err;
        }
        //Check2
        pFuncPtrEnableNet = (PMYPROC2) GetProcAddress(sg_hDll, "icsneoEnableNetworkCom");
        if (NULL == pFuncPtrEnableNet)
        {
            unResult = unResult | PROC2Err;
        }
        //Check3
        pFuncPtrClosePort  = (PMYPROC3) GetProcAddress(sg_hDll, "icsneoClosePort");
        if (NULL == pFuncPtrClosePort)
        {             
            unResult = unResult | PROC3Err;
        }
        //Check4
        pFuncPtrFreeObj = (PMYPROC4) GetProcAddress(sg_hDll, "icsneoFreeObject");
        if (NULL == pFuncPtrFreeObj)
        {             
            unResult = unResult | PROC4Err;
        }
        //Check5
        pFuncPtrGetMsgs = (PMYPROC5) GetProcAddress(sg_hDll, "icsneoGetMessages");
        if (NULL == pFuncPtrGetMsgs)
        {              
            unResult = unResult | PROC5Err;
        }
        //Check6
        pFuncPtrTxMsgs = (PMYPROC6) GetProcAddress(sg_hDll, "icsneoTxMessages");
        if (NULL == pFuncPtrTxMsgs)
        {                
            unResult = unResult | PROC6Err;
        }
        //Check7
        pFuncPtrGetErrMsgs  = (PMYPROC7) GetProcAddress(sg_hDll, "icsneoGetErrorMessages");
        if (NULL == pFuncPtrGetErrMsgs)
        {            
            unResult = unResult | PROC7Err;
        }
        //Check8
        pFuncPtrGetErrInfo = (PMYPROC8) GetProcAddress(sg_hDll, "icsneoGetErrorInfo");
        if (NULL == pFuncPtrGetErrInfo)
        {            
            unResult = unResult | PROC8Err;
        }
        //Check9
        pFuncPtrSendConfig  = (PMYPROC9) GetProcAddress(sg_hDll, "icsneoSendConfiguration");
        if (NULL == pFuncPtrSendConfig)
        {           
            unResult = unResult | PROC9Err;
        }
        //Check10
        pFuncPtrGetConfig  = (PMYPROC10) GetProcAddress(sg_hDll, "icsneoGetConfiguration");
        if (NULL == pFuncPtrGetConfig)
        {            
            unResult = unResult | PROC10Err;
        }
        //Check11
        pFuncPtrGetDllVer  = (PMYPROC11) GetProcAddress(sg_hDll, "icsneoGetDLLVersion");
        if (NULL == pFuncPtrGetDllVer)
        {            
            unResult = unResult | PROC11Err;
        }
        //Check12
        pFuncPtrFindAllComDev  = (PMYPROC12) GetProcAddress(sg_hDll, "icsneoFindAllCOMDevices");
        if (NULL == pFuncPtrFindAllComDev)
        {              
            unResult = unResult | PROC12Err;
        }
        //Check13
        pFuncPtrGetPerPar  = (PMYPROC13) GetProcAddress(sg_hDll, "icsneoGetPerformanceParameters");
        if (NULL == pFuncPtrGetPerPar)
        {            
            unResult = unResult | PROC13Err;
        }
        //Check14
        pFuncPtrWaitForRxMsg = (PMYPROC14) GetProcAddress(sg_hDll, "icsneoWaitForRxMessagesWithTimeOut");
        if (NULL == pFuncPtrWaitForRxMsg)
        {            
            unResult = unResult | PROC14Err;
        }
        //Check15
        pFuncPtrSetBitRate = (PMYPROC15) GetProcAddress(sg_hDll, "icsneoSetBitRate");
        if (NULL == pFuncPtrSetBitRate)
        {
            unResult = unResult | PROC15Err;
        }
        //Check16
        pFuncPtrGetTimeStampForMsg = (PMYPROC16) GetProcAddress(sg_hDll, "icsneoGetTimeStampForMsg");
        if (NULL == pFuncPtrGetTimeStampForMsg)
        {
            unResult = unResult | PROC16Err;
        }
        //check for error
        if (unResult != 0)
        {
            FreeLibrary(sg_hDll);
        }
    }
    else
    {
        unResult = ERR_LOADDRIVER;
    }
    return (HRESULT) unResult; 
}

/* HELPER FUNCTIONS END */

/* GENERAL FUNCTION SET STARTS */
/* Perform initialization operations specific to TZM */
USAGEMODE HRESULT CAN_ES581_Usb_PerformInitOperations(void)
{
    //Register Monitor client
    CAN_ES581_Usb_RegisterClient(TRUE, sg_dwClientID, CAN_MONITOR_NODE);
    sg_podActiveNetwork = &sg_odHardwareNetwork;
    return S_OK;
}

/* Perform closure operations specific to TZM */
USAGEMODE HRESULT CAN_ES581_Usb_PerformClosureOperations(void)
{
    HRESULT hResult = S_OK;
    UINT ClientIndex = 0;
    while (sg_unClientCnt > 0)
    {
        bRemoveClient(sg_asClientToBufMap[ClientIndex].dwClientID);
    }
    hResult = CAN_ES581_Usb_DeselectHwInterface();

    if (hResult == S_OK)
    {
        sg_bCurrState = STATE_DRIVER_SELECTED;
    }
    return hResult;
}
/* Rrtreive time mode mapping */
USAGEMODE HRESULT CAN_ES581_Usb_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER* QueryTickCount)
{
    memcpy(&CurrSysTime, &sg_CurrSysTime, sizeof(SYSTEMTIME));
    TimeStamp = sg_TimeStamp;
    if(QueryTickCount != NULL)
    {
        *QueryTickCount = sg_QueryTickCount;
    }
    return S_OK;
}


/* Function to List Hardware interfaces connect to the system and requests to the 
   user to select*/
USAGEMODE HRESULT CAN_ES581_Usb_ListHwInterfaces(INTERFACE_HW_LIST& asSelHwInterface, INT& nCount)    
{    
    USES_CONVERSION;
    HRESULT hResult = S_FALSE;
    if (bGetDriverStatus())
    {
        if (nConnectToDriver() == CAN_USB_OK)
        {  
            nCount = sg_ucNoOfHardware;
            for (UINT i = 0; i < sg_ucNoOfHardware; i++)
            {
                asSelHwInterface[i].m_dwIdInterface = m_anComPorts[i];
                _stprintf(asSelHwInterface[i].m_acDescription, _T("%d"), m_anSerialNumbers[i]);
                hResult = S_OK;
                sg_bCurrState = STATE_HW_INTERFACE_LISTED;
            }
        }
        else
        {            
            hResult = NO_HW_INTERFACE;
            sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("Error connecting to driver"));
        }
    }
    else
    {   
        sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("Error in getting driver status"));
    }
    return hResult;
}

/*******************************************************************************
 Function Name  : nResetHardware
 Input(s)       : bHardwareReset - Reset Mode
                  TRUE - Hardware Reset, FALSE - Software Reset
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This function will do controller reset. In case of USB mode
                  Software Reset will be simulated by Client Reset.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 16.09.2004, Added message posting code in to the if
                  condition so that the message will be posted only on
                  successful reset of hardware.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel.
 Modifications  : Raja N on 9.3.2005
                  Code review points are implemented
 Modifications  : Ratnadip Choudhury on 10.04.2008
                  Added neoVI specific codes.
*******************************************************************************/
static int nResetHardware(BOOL /*bHardwareReset*/)
{
    return 0;
}
/* Function to deselect the chosen hardware interface */
USAGEMODE HRESULT CAN_ES581_Usb_DeselectHwInterface(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);

    HRESULT hResult = S_OK;
    
    CAN_ES581_Usb_ResetHardware();

    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    
    return hResult;
}

/* Function to select hardware interface chosen by the user */
USAGEMODE HRESULT CAN_ES581_Usb_SelectHwInterface(const INTERFACE_HW_LIST& asSelHwInterface, INT /*nCount*/)
{
    USES_CONVERSION;

    VALIDATE_POINTER_RETURN_VAL(sg_hDll, S_FALSE);

    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_LISTED, ERR_IMPROPER_STATE);

    HRESULT hResult = S_FALSE;
    
    if (bGetDriverStatus())
    {
        //Select the interface
        for (UINT i = 0; i < sg_ucNoOfHardware; i++)
        {
            m_anComPorts[i] = (INT)asSelHwInterface[i].m_dwIdInterface;
            m_anSerialNumbers[i] = _ttoi(asSelHwInterface[i].m_acDescription);
        }
        //Check for the success
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
        hResult = S_OK;
    }
    else
    {
        hResult = S_FALSE;
        sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("Driver is not running..."));
    }
    return hResult;
}

/* Function to set controller configuration*/
USAGEMODE HRESULT CAN_ES581_Usb_SetConfigData(PCHAR ConfigFile, int Length)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);

    USES_CONVERSION; 

    HRESULT hResult = S_FALSE;
    memcpy((void*)sg_ControllerDetails, (void*)ConfigFile, Length);
    int nReturn = nSetApplyConfiguration();
    if (nReturn == defERR_OK)
    {
        hResult = S_OK;
    }
    else
    {
        sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__,
                        _T("Controller configuration failed"));
    }
    return hResult;
}

BOOL Callback_DILTZM(BYTE /*Argument*/, PBYTE pDatStream, int /*Length*/)
{
    return (CAN_ES581_Usb_SetConfigData((CHAR *) pDatStream, 0) == S_OK);
}

/* Function to display config dialog */
USAGEMODE HRESULT CAN_ES581_Usb_DisplayConfigDlg(PCHAR& InitData, int& Length)
{
    HRESULT Result = WARN_INITDAT_NCONFIRM;
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);

    VALIDATE_POINTER_RETURN_VAL(InitData, Result);
    PSCONTROLER_DETAILS pControllerDetails = (PSCONTROLER_DETAILS)InitData;
    //First initialize with existing hw description
    for (INT i = 0; i < min(Length, sg_ucNoOfHardware); i++)
    {   
        _stprintf(pControllerDetails[i].m_omHardwareDesc, _T("Serial Number %d"), m_anSerialNumbers[i]);
    }
    if (sg_ucNoOfHardware > 0)
    {
        int nResult = DisplayConfigurationDlg(sg_hOwnerWnd, Callback_DILTZM,
            pControllerDetails, sg_ucNoOfHardware, DRIVER_CAN_ETAS_ES581);
        switch (nResult)
        {
            case WARNING_NOTCONFIRMED:
            {
                Result = WARN_INITDAT_NCONFIRM;
            }
            break;
            case INFO_INIT_DATA_CONFIRMED:
            {
                bLoadDataFromContr(pControllerDetails);
                memcpy(sg_ControllerDetails, pControllerDetails, sizeof (SCONTROLER_DETAILS) * defNO_OF_CHANNELS);
                nSetApplyConfiguration();
                memcpy(InitData, (void*)sg_ControllerDetails, sizeof (SCONTROLER_DETAILS) * defNO_OF_CHANNELS);
                Length = sizeof(SCONTROLER_DETAILS) * defNO_OF_CHANNELS;
                Result = INFO_INITDAT_CONFIRM_CONFIG;
            }
            break;
            case INFO_RETAINED_CONFDATA:
            {
                Result = INFO_INITDAT_RETAINED;
            }
            break;
            case ERR_CONFIRMED_CONFIGURED: // Not to be addressed at present
            case INFO_CONFIRMED_CONFIGURED:// Not to be addressed at present
            default:
            {
                // Do nothing... default return value is S_FALSE.
            }
            break;
        }
    }
    else
    {
        Result = S_OK;
    }

    return Result;
}

/* Function to start monitoring the bus */
USAGEMODE HRESULT CAN_ES581_Usb_StartHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);

    USES_CONVERSION;
    HRESULT hResult = S_OK;
    //Restart the read thread
    sg_sParmRThread.bTerminateThread();
    sg_sParmRThread.m_pBuffer = (LPVOID) &s_DatIndThread;
    s_DatIndThread.m_bToContinue = TRUE;
    if (sg_sParmRThread.bStartThread(CanMsgReadThreadProc_CAN_Usb))
    {
        hResult = S_OK;
    }
    else
    {
        sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("Could not start the read thread" ));
    }
    //If everything is ok connect to the network
    if (hResult == S_OK)
    {
        for (UINT ClientIndex = 0; ClientIndex < sg_unClientCnt; ClientIndex++)
        {
            BYTE hClient = sg_asClientToBufMap[ClientIndex].hClientHandle;
            hResult = nConnect(TRUE, hClient);    
            if (hResult == CAN_USB_OK)
            {
                hResult = S_OK;
                sg_bCurrState = STATE_CONNECTED;
            }
            else
            {
                //log the error for open port failure
                vRetrieveAndLog(hResult, __FILE__, __LINE__);
                hResult = ERR_LOAD_HW_INTERFACE;
            }
        }
    }
    return hResult;
}

/* Function to stop monitoring the bus */
USAGEMODE HRESULT CAN_ES581_Usb_StopHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    HRESULT hResult = S_OK;
    
    for (UINT ClientIndex = 0; ClientIndex < sg_unClientCnt; ClientIndex++)
    {
        BYTE hClient = sg_asClientToBufMap[ClientIndex].hClientHandle;        
    
        hResult = nConnect(FALSE, hClient);
        if (hResult == CAN_USB_OK)
        {
            hResult = S_OK;
            sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
        }
        else
        {
            //log the error for open port failure
            vRetrieveAndLog(hResult, __FILE__, __LINE__);
            hResult = ERR_LOAD_HW_INTERFACE;
            ClientIndex = sg_unClientCnt; //break the loop
        }
    }
    return hResult;
}
static BOOL bClientExist(TCHAR* pcClientName, INT& Index)
{    
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (!_tcscmp(pcClientName, sg_asClientToBufMap[i].pacClientName))
        {
            Index = i;
            return TRUE;
        }       
    }
    return FALSE;
}

static BOOL bClientIdExist(const DWORD& dwClientId)
{
    BOOL bReturn = FALSE;
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].dwClientID == dwClientId)
        {
            bReturn = TRUE;
            i = sg_unClientCnt; // break the loop
        }
    }
    return bReturn;
}


/*******************************************************************************
 Function Name  : nGetErrorCounter
 Input(s)       : sErrorCount - Error Counter Structure
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This function will return the error counter values. In case of
                  USB this is not supported.
 Member of      : -
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel. USB driver supports
                  indication whenever there is a change in the error counter
                  value. But parallel port mode this is done in polling. So
                  update the error counter whenever it is read from the driver.
*******************************************************************************/
static int nGetErrorCounter( UINT unChannel, SERROR_CNT& sErrorCount)
{
    int nReturn = -1;

    // Check for the valid channel index
    if( unChannel < sg_podActiveNetwork->m_nNoOfChannels )
    {
        // Get the channel reference
        CChannel& odChannel = sg_podActiveNetwork->m_aodChannels[ unChannel ];
        // Assign the error counter value
        sErrorCount.m_ucRxErrCount = odChannel.m_ucRxErrorCounter;
        sErrorCount.m_ucTxErrCount = odChannel.m_ucTxErrorCounter;
        nReturn = defERR_OK;
    }
    else
    {
        // Invalid channel ID. Help debugging
    }

    return nReturn;
}
/* Function to reset the hardware, fcClose resets all the buffer */
USAGEMODE HRESULT CAN_ES581_Usb_ResetHardware(void)
{
    HRESULT hResult = S_FALSE;
    // Stop the hardware if connected
    CAN_ES581_Usb_StopHardware(); //return value not necessary
    if (sg_sParmRThread.bTerminateThread())
    {
        if (nResetHardware(TRUE) == CAN_USB_OK)
        {
            hResult = S_OK;
        }
    }
    return hResult;
}

/* Function to get Controller status */
USAGEMODE HRESULT CAN_ES581_Usb_GetCurrStatus(s_STATUSMSG& StatusData)
{
    if (sg_ucControllerMode == defUSB_MODE_ACTIVE)
    {
        StatusData.wControllerStatus = NORMAL_ACTIVE;
    }
    else if (sg_ucControllerMode == defUSB_MODE_PASSIVE)
    {
        StatusData.wControllerStatus = NORMAL_PASSIVE;
    }
    else if (sg_ucControllerMode == defUSB_MODE_SIMULATE)
    {
        StatusData.wControllerStatus = NOT_DEFINED;
    }        
    return S_OK;
}

/* Function to get Tx Msg Buffers configured from chi file */
USAGEMODE HRESULT CAN_ES581_Usb_GetTxMsgBuffer(BYTE*& /*pouFlxTxMsgBuffer*/)
{
    return S_OK;
}
/*******************************************************************************
 Function Name  : nWriteMessage
 Input(s)       : sMessage - Message to Transmit
 Output         : int - Operation Result. 0 incase of no errors. Failure Error
                  codes otherwise.
 Functionality  : This will send a CAN message to the driver. In case of USB
                  this will write the message in to the driver buffer and will
                  return. In case if parallel port mode this will write the
                  message and will wait for the ACK event from the driver. If
                  the event fired this will return 0. Otherwise this will return
                  wait time out error. In parallel port it is a blocking call
                  and in case of failure condition this will take 2 seconds.
 Member of      : 
 Author(s)      : Raja N
 Date Created   : 02.09.2004
 Modifications  : Raja N on 13.09.2004, Added code to post a message on failure.
                  and added code to check the return value of wait and
                  incremented the Tx count only on WAIT_OBJECT_0 condition.
 Modifications  : Raja N on 9.3.2005
                  Modifications to support multi channel
 Modifications  : Ratnadip Choudhury on 21.04.2008
                  Added neoVI specific codes.
*******************************************************************************/
static int nWriteMessage(STCAN_MSG sMessage)
{
    int nReturn = -1;

    // Return when in disconnected state
    if (!sg_bIsConnected) return nReturn;
    
    if (sg_podActiveNetwork != NULL &&
    sMessage.m_ucChannel > 0 &&
    sMessage.m_ucChannel <= sg_podActiveNetwork->m_nNoOfChannels)
    {
        icsSpyMessage SpyMsg;
        memcpy(&(SpyMsg.ArbIDOrHeader), &(sMessage.m_unMsgID), sizeof(UINT));
        SpyMsg.NumberBytesData = sMessage.m_ucDataLen;
        SpyMsg.StatusBitField = 0;
        SpyMsg.StatusBitField2 = 0;
        if (sMessage.m_ucRTR == 1)
        {
            SpyMsg.StatusBitField |= SPY_STATUS_REMOTE_FRAME;
        }
        if (sMessage.m_ucEXTENDED == 1)
        {
            SpyMsg.StatusBitField |= SPY_STATUS_XTD_FRAME;
        }
        memcpy(SpyMsg.Data, sMessage.m_ucData, sMessage.m_ucDataLen);
        if ((*pFuncPtrTxMsgs)(m_anhObject[(int)(sMessage.m_ucChannel) - 1], &SpyMsg, NETID_HSCAN, 1) != 0)
        {
            nReturn = 0;
        }
    }
    return nReturn; // Multiple return statements had to be added because
    // neoVI specific codes and simulation related codes need to coexist. 
    // Code for the later still employs Peak API interface.
}

/* Function to Send CAN Message to Transmit buffer. This is called only after checking the controller in active mode */
USAGEMODE HRESULT CAN_ES581_Usb_SendMsg(DWORD dwClientID, const STCAN_MSG& sMessage)
{    
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);
    static SACK_MAP sAckMap;
    HRESULT hResult = S_FALSE;
    if (bClientIdExist(dwClientID))
    {
        if (sMessage.m_ucChannel <= sg_ucNoOfHardware)
        {
            sAckMap.m_ClientID = dwClientID;
            sAckMap.m_MsgID = sMessage.m_unMsgID;
            sAckMap.m_Channel = sMessage.m_ucChannel;
            /*Mark an entry in Map. This is helpful to idendify
              which client has been sent this message in later stage*/ 
            vMarkEntryIntoMap(sAckMap);
            if (nWriteMessage(sMessage) == defERR_OK)
            {
                hResult = S_OK;
            }
        }
        else
        {
            hResult = ERR_INVALID_CHANNEL;
        }
    }
    else
    {
        hResult = ERR_NO_CLIENT_EXIST;
    }
    return hResult;
}
/* Function get hardware, firmware, driver information*/ 
USAGEMODE HRESULT CAN_ES581_Usb_GetBoardInfo(s_BOARDINFO& /*BoardInfo*/)
{
    return S_OK;
}

USAGEMODE HRESULT CAN_ES581_Usb_GetBusConfigInfo(BYTE* /*usInfo*/)
{
    return S_OK;
}

USAGEMODE HRESULT CAN_ES581_Usb_GetVersionInfo(VERSIONINFO& /*sVerInfo*/)
{
    return S_FALSE;
}

/* Function to retreive error string of last occurred error*/
USAGEMODE HRESULT CAN_ES581_Usb_GetLastErrorString(CHAR* acErrorStr, int nLength)
{
    // TODO: Add your implementation code here
    int nCharToCopy = (int) (strlen(sg_acErrStr));
    if (nCharToCopy > nLength)
    {
        nCharToCopy = nLength;
    }
    strncpy(acErrorStr, sg_acErrStr, nCharToCopy);

    return S_OK;
}

/* Set application parameters specific to CAN_USB */
USAGEMODE HRESULT CAN_ES581_Usb_SetAppParams(HWND hWndOwner, Base_WrapperErrorLogger* pILog)
{
    sg_hOwnerWnd = hWndOwner;
    sg_pIlog = pILog;
    vInitialiseAllData();
    return S_OK;
}

static BOOL bIsBufferExists(const SCLIENTBUFMAP& sClientObj, const CBaseCANBufFSE* pBuf)
{
    BOOL bExist = FALSE;
    for (UINT i = 0; i < sClientObj.unBufCount; i++)
    {
        if (pBuf == sClientObj.pClientBuf[i])
        {
            bExist = TRUE;
            i = sClientObj.unBufCount; //break the loop
        }
    }
    return bExist;
}

static DWORD dwGetAvailableClientSlot()
{
    DWORD nClientId = 2;
    for (int i = 0; i < MAX_CLIENT_ALLOWED; i++)
    {
        if (bClientIdExist(nClientId))
        {
            nClientId += 1;
        }
        else
        {
            i = MAX_CLIENT_ALLOWED; //break the loop
        }
    }
    return nClientId;
}
/* Register Client */
USAGEMODE HRESULT CAN_ES581_Usb_RegisterClient(BOOL bRegister,DWORD& ClientID, TCHAR* pacClientName)
{
    USES_CONVERSION;    
    HRESULT hResult = S_FALSE;
    if (bRegister)
    {
        if (sg_unClientCnt < MAX_CLIENT_ALLOWED)
        {        
            INT Index = 0;
            if (!bClientExist(pacClientName, Index))
            {
                //Currently store the client information
                if (_tcscmp(pacClientName, CAN_MONITOR_NODE) == 0)
                {
                    //First slot is reserved to monitor node
                    ClientID = 1;
                    _tcscpy(sg_asClientToBufMap[0].pacClientName, pacClientName);                
                    sg_asClientToBufMap[0].dwClientID = ClientID;
                    sg_asClientToBufMap[0].unBufCount = 0;
                }
                else
                {                    
                    if (!bClientExist(CAN_MONITOR_NODE, Index))
                    {
                        Index = sg_unClientCnt + 1;
                    }
                    else
                    {
                        Index = sg_unClientCnt;
                    }
                    ClientID = dwGetAvailableClientSlot();
                    _tcscpy(sg_asClientToBufMap[Index].pacClientName, pacClientName);
            
                    sg_asClientToBufMap[Index].dwClientID = ClientID;
                    sg_asClientToBufMap[Index].unBufCount = 0;
                }
                sg_unClientCnt++;
                hResult = S_OK;
            }
            else
            {
                ClientID = sg_asClientToBufMap[Index].dwClientID;
                hResult = ERR_CLIENT_EXISTS;
            }
        }
        else
        {
            hResult = ERR_NO_MORE_CLIENT_ALLOWED;
        }
    }
    else
    {
        if (bRemoveClient(ClientID))
        {
            hResult = S_OK;
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    return hResult;
}

USAGEMODE HRESULT CAN_ES581_Usb_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    HRESULT hResult = S_FALSE;
    if (ClientID != NULL)
    {
        UINT unClientIndex;
        if (bGetClientObj(ClientID, unClientIndex))
        {
            SCLIENTBUFMAP &sClientObj = sg_asClientToBufMap[unClientIndex];
            if (bAction == MSGBUF_ADD)
            {
                //Add msg buffer
                if (pBufObj != NULL)
                {
                    if (sClientObj.unBufCount < MAX_BUFF_ALLOWED)
                    {
                        if (bIsBufferExists(sClientObj, pBufObj) == FALSE)
                        {
                            sClientObj.pClientBuf[sClientObj.unBufCount++] = pBufObj;
                            hResult = S_OK;
                        }
                        else
                        {
                            hResult = ERR_BUFFER_EXISTS;
                        }
                    }
                }
            }
            else if (bAction == MSGBUF_CLEAR)
            {
                //clear msg buffer
                if (pBufObj != NULL) //REmove only buffer mentioned
                {
                    bRemoveClientBuffer(sClientObj.pClientBuf, sClientObj.unBufCount, pBufObj);
                }
                else //Remove all
                {
                    for (UINT i = 0; i < sClientObj.unBufCount; i++)
                    {
                        sClientObj.pClientBuf[i] = NULL;
                    }
                    sClientObj.unBufCount = 0;
                }
                hResult = S_OK;
            }
            else
            {
                ////ASSERT(FALSE);
            }
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    else
    {
        if (bAction == MSGBUF_CLEAR)
        {
            //clear msg buffer
            for (UINT i = 0; i < sg_unClientCnt; i++)
            {
                CAN_ES581_Usb_ManageMsgBuf(MSGBUF_CLEAR, sg_asClientToBufMap[i].dwClientID, NULL);                        
            }
            hResult = S_OK;
        }        
    }
    
    return hResult;
}
/* Function to load driver CanApi2.dll */
USAGEMODE HRESULT CAN_ES581_Usb_LoadDriverLibrary(void)
{
    USES_CONVERSION;

    HRESULT hResult = S_OK;

    if (sg_hDll != NULL)
    {
        sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("icsneo.dll already loaded"));
        hResult = DLL_ALREADY_LOADED;
    }

    if (S_OK == hResult)
    {
        sg_hDll = LoadLibrary(_T("icsneo40.dll"));
        if (sg_hDll == NULL)
        {
            sg_pIlog->vLogAMessage(A2T(__FILE__), __LINE__, _T("CanApi2.dll loading failed"));
            hResult = ERR_LOAD_DRIVER;
        }
        else
        {
            int nResult = GetETAS_ES581_APIFuncPtrs();
            if (nResult != 0)
            {
                // Log list of the function pointers non-retrievable
                // TO DO: specific information on failure in getting function pointer
                sg_pIlog->vLogAMessage(A2T(__FILE__),
                            __LINE__, _T("Getting Process address of the APIs failed"));
                hResult = ERR_LOAD_DRIVER;
            }
        }
    }

    return hResult;
}

/* Function to Unload Driver library */
USAGEMODE HRESULT CAN_ES581_Usb_UnloadDriverLibrary(void)
{
    // Don't bother about the success & hence the result
    CAN_ES581_Usb_DeselectHwInterface();

    // Store the Boardinfo to global variable

    if (sg_hDll != NULL)
    {
        FreeLibrary(sg_hDll);
        sg_hDll = NULL;
    }
    return S_OK;
}


/* START OF FILTER IN/OUT FUNCTION */
USAGEMODE HRESULT CAN_ES581_Usb_FilterFrames(FILTER_TYPE /*FilterType*/, TYPE_CHANNEL /*Channel*/, UINT* /*punFrames*/, UINT /*nLength*/)
{
    return S_OK;
}
/* END OF FILTER IN/OUT FUNCTION */
USAGEMODE HRESULT CAN_ES581_Usb_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus)
{
    if (hEvent != NULL)
    {
        sg_hCntrlStateChangeEvent = hEvent;
    }    
    unCntrlStatus = static_cast<UINT>(sg_ucControllerMode);
    return S_OK;
}
//
USAGEMODE HRESULT CAN_ES581_Usb_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    HRESULT hResult = S_OK;
    if ((sg_bCurrState == STATE_HW_INTERFACE_SELECTED) || (sg_bCurrState == STATE_CONNECTED))
    {
        switch (eContrParam)
        {
            
            case NUMBER_HW:
            {
                lParam = sg_ucNoOfHardware;
            }
            break;
            case NUMBER_CONNECTED_HW:
            {
                int nConHwCnt = 0;
                if (nGetNoOfConnectedHardware(nConHwCnt) == NEOVI_OK)
                {
                    lParam = nConHwCnt;
                }
                else
                {
                    hResult = S_FALSE;
                }
            }
            break;
            case DRIVER_STATUS:
            {
                lParam = sg_bIsDriverRunning;
            }
            break;
            case HW_MODE:
            {
                if (nChannel < sg_ucNoOfHardware)
                {
                    lParam = sg_ucControllerMode;
                    if( sg_ucControllerMode == 0 || sg_ucControllerMode > defMODE_SIMULATE )
                    {
                        lParam = defCONTROLLER_BUSOFF;
                        hResult = S_FALSE;
                    }
                }
                else
                {
                    //Unknown
                    lParam = defCONTROLLER_BUSOFF + 1;
                }
            }
            break;
            case CON_TEST:
            {
                UCHAR ucResult;
                if (nTestHardwareConnection(ucResult, nChannel) == defERR_OK)
                {
                    lParam = (LONG)ucResult;
                }
            }
            break;
            default:
            {
                hResult = S_FALSE;
            }
            break;

        }
    }
    else
    {
        hResult = ERR_IMPROPER_STATE;
    }
    return hResult;
}


USAGEMODE HRESULT CAN_ES581_Usb_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM /*eContrParam*/)
{
    HRESULT hResult = S_FALSE;
    if ((sg_bCurrState == STATE_CONNECTED) || (sg_bCurrState == STATE_HW_INTERFACE_SELECTED))
    {
        if (nChannel <= sg_ucNoOfHardware)
        {
            if (nGetErrorCounter(nChannel, sErrorCnt) == defERR_OK)
            {
                hResult = S_OK;
            }
        }
        else
        {
            hResult = ERR_INVALID_CHANNEL;
        }
    }
    else
    {
        hResult = ERR_IMPROPER_STATE;
    }
    return hResult;
}
