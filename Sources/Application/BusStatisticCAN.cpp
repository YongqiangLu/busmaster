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
 * \file      BusStatisticCAN.cpp
 * \author    venkatanarayana Makam
 * \copyright Copyright (c) 2011, Robert Bosch Engineering and Business Solutions. All rights reserved.
 */
#include "StdAfx.h"
//To Include BusStatisticCAN Definitions.

#include "BusStatisticCAN.h"
#include "Include/Can_Error_Defs.h"
#include "DIL_Interface/DIL_Interface_Extern.h"
#include "TimeManager.h"

//Can interface to Bus Statistics
void* CBusStatisticCAN:: sm_pouBSCan;
/******************************************************************************
Function Name   : ReadBSDataBuffer
Input(s)        : CBusStatisticCAN* - contain the CBusStatisticCAN object
Output          : int - Function return Status
Functionality   : Updates the busstatistics parameters using 
                  vUpdateBusStatistics Function
Member of       : -
Friend of       : -
Author(s)       : Venkatanarayana Makam
Date Created    : 07/09/2010
Modifications   : 
******************************************************************************/
int ReadBSDataBuffer(CBusStatisticCAN* pBSCan)
{

    ASSERT(pBSCan != NULL);
    while (pBSCan->m_ouCanBufFSE.GetMsgCount() > 0)
    {
        static STCANDATA sCanData;
        sCanData.m_lTickCount.QuadPart;
        int Result = pBSCan->m_ouCanBufFSE.ReadFromBuffer(&sCanData);
        if (Result == ERR_READ_MEMORY_SHORT)
        {
            TRACE(_T("ERR_READ_MEMORY_SHORT"));
        }
        else if (Result == EMPTY_APP_BUFFER)
        {
            TRACE(_T("EMPTY_APP_BUFFER"));
        }
        else
        {
            pBSCan->vUpdateBusStatistics(sCanData);
        }
        
    }
    return 0;
}
/******************************************************************************
Function Name  :  BSDataReadThreadProc
Input(s)       :  pVoid - contains CBusStatisticCAN pointer
Output         :  DWORD
Functionality  :  Thread Function that is used to read the driver data and
                  updates the Bus Statistics. 
Member of      :  -
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
DWORD WINAPI BSDataReadThreadProc(LPVOID pVoid)
{    
    CPARAM_THREADPROC* pThreadParam = (CPARAM_THREADPROC *) pVoid;
    if (pThreadParam == NULL)
    {
        return ((DWORD)-1);
    }
    CBusStatisticCAN* pBusStatistics = (CBusStatisticCAN*)pThreadParam->m_pBuffer;
    if (pBusStatistics == NULL)
    {
        return ((DWORD)-1);
    }
    bool bLoopON = true;
    pThreadParam->m_unActionCode = INVOKE_FUNCTION;
    while (bLoopON)
    {
        WaitForSingleObject(pThreadParam->m_hActionEvent, INFINITE);
        switch (pThreadParam->m_unActionCode)
        {
            case INVOKE_FUNCTION:
            {
                ReadBSDataBuffer(pBusStatistics); // Retrieve message from the driver
            }
            break;
            case EXIT_THREAD:
            {
                bLoopON = false;
            }
            break;
            case CREATE_TIME_MAP:
            {
                //Nothing at this moment
            }
            break;
            case INACTION:
            {
                // nothing right at this moment
            }
            break;
            default:
            {

            }
            break;
        }
    }
    SetEvent(pThreadParam->hGetExitNotifyEvent());
    return 0;
}

/******************************************************************************
Function Name  :  CBusStatisticCAN
Input(s)       :  -
Output         :  -
Functionality  :  Constructor for CBusStatisticCAN which initialises
                  critical section and event mechanism.
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
CBusStatisticCAN::CBusStatisticCAN(void)
{
    InitializeCriticalSection(&m_omCritSecBS);
    m_ouReadThread.m_hActionEvent = m_ouCanBufFSE.hGetNotifyingEvent();
    m_nTimerHandle = NULL;
}
/******************************************************************************
Function Name  :  ~CBusStatisticCAN
Input(s)       :  -
Output         :  -
Functionality  :   Destructor of CBusStatisticCAN and which deletes critical 
                   section and terminates the thread.
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
CBusStatisticCAN::~CBusStatisticCAN(void)
{

    m_ouReadThread.bTerminateThread();      // Terminate read thread
    m_ouCanBufFSE.vClearMessageBuffer();    // clear can buffer
    DeleteCriticalSection(&m_omCritSecBS);  // delete critical section
    if( m_nTimerHandle != NULL )
        KillTimer(NULL, m_nTimerHandle);
}

/******************************************************************************
Function Name  :  BSC_DoInitialization
Input(s)       :  -
Output         :  HRESULT, contain Error information
Functionality  :  Does the client registration with driver and starts the Read
                  Tread.
                    Initialises 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_DoInitialization(void)
{
    if (DIL_GetInterface(CAN, (void**)&m_pouDIL_CAN) == S_OK)
    {
        DWORD dwClientId = 0;        
        m_pouDIL_CAN->DILC_RegisterClient(TRUE, dwClientId, CAN_MONITOR_NODE);
        m_pouDIL_CAN->DILC_ManageMsgBuf(MSGBUF_ADD, dwClientId, &m_ouCanBufFSE);
    }
    vInitialiseBSData();
    
    sm_pouBSCan = this;
    
    //Start the read thread
    
    return bStartBSReadThread()?S_OK:S_FALSE;
}
/******************************************************************************
Function Name  :  BSC_bStartUpdation
Input(s)       :  bStart, if ture starts the TIMER otherwise kill the thread
Output         :  BOOL
Functionality  :  Starts or kills the timer. 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  13/09/2010
Modifications  :  
******************************************************************************/
BOOL CBusStatisticCAN::BSC_bStartUpdation(BOOL bStart)
{
    if(bStart == TRUE)
    {
        if(m_nTimerHandle == NULL)
        {
            m_nTimerHandle = ::SetTimer(NULL, 0, 1000, OnTimeWrapper);
        }
    }
    else
    {
        KillTimer(NULL, m_nTimerHandle);
        m_nTimerHandle = NULL;
    }
    return 0;
}
/******************************************************************************
Function Name  :  BSC_ResetBusStatistic
Input(s)       :  -
Output         :  HRESULT, contain Error information
Functionality  :  This function resets the bus statistics structure using
                  vInitialiseBSData() member function.
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_ResetBusStatistic(void)
{
    EnterCriticalSection(&m_omCritSecBS);

    vInitialiseBSData();
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}

/******************************************************************************
Function Name  :  BSC_GetTotalMsgCount
Input(s)       :  1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                    CAN_CHANNEL_ALL
                  2.eDir can be DIR_RX, DIR_TX, DIR_ALL
                  3.byIdType can be TYPE_ID_CAN_STANDARD, TYPE_ID_CAN_EXTENDED,
                    TYPE_ID_CAN_ALL
                  4.byMsgType can be TYPE_MSG_CAN_RTR, TYPE_MSG_CAN_NON_RTR,
                    TYPE_MSG_CAN_ALL
                  5.nMsgCount Referance variable contains the resulted Total 
                    message count
Output         :  HRESULT, contain Error information
Functionality  :  Returns the total number of valid messages transmitted to or 
                  received from the bus.
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetTotalMsgCount(UINT unChannelIndex, eDirection eDir, 
                                BYTE byIdType, BYTE byMsgType, UINT& nMsgCount)
{
/*-----------------------------------------------------------------------------
    S.no         eDir          byIdType                    byMsgType
    ---------------------------------------------------------------------------
    1.          DIR_RX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_RTR
    2.          DIR_RX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_NON_RTR
    3.          DIR_RX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_ALL
    4.          DIR_RX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_RTR
    5.          DIR_RX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_NON_RTR
    6.          DIR_RX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_ALL
    7.          DIR_RX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_RTR
    8.          DIR_RX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_NON_RTR
    9.          DIR_RX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_ALL

    10.         DIR_TX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_RTR
    11.         DIR_TX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_NON_RTR
    12.         DIR_TX      TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_ALL
    13.         DIR_TX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_RTR
    14.         DIR_TX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_NON_RTR
    15.         DIR_TX      TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_ALL
    16.         DIR_TX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_RTR
    17.         DIR_TX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_NON_RTR
    18.         DIR_TX      TYPE_ID_CAN_ALL             TYPE_MSG_CAN_ALL

    19          DIR_ALL     TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_RTR
    20          DIR_ALL     TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_NON_RTR
    21          DIR_ALL     TYPE_ID_CAN_STANDARD        TYPE_MSG_CAN_ALL
    22          DIR_ALL     TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_RTR
    23          DIR_ALL     TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_NON_RTR
    24          DIR_ALL     TYPE_ID_CAN_EXTENDED        TYPE_MSG_CAN_ALL
    25          DIR_ALL     TYPE_ID_CAN_ALL             TYPE_MSG_CAN_RTR
    26          DIR_ALL     TYPE_ID_CAN_ALL             TYPE_MSG_CAN_NON_RTR
    27          DIR_ALL     TYPE_ID_CAN_ALL             TYPE_MSG_CAN_ALL
-----------------------------------------------------------------------------*/
    EnterCriticalSection(&m_omCritSecBS);

    if( eDir == DIR_RX )
    {
        if( byIdType == TYPE_ID_CAN_STANDARD )
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case1:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case2:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount;
            }
            else 
            {
            //Case3:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_NON_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                                    m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount;
            }
        }
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case4:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case5:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount;
            }
            else 
            {
            //Case6:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                                    m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount;
            }
        }
        else
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case7:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case8:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount;
            }
            else 
            {
            //Case9:(eDir=DIR_RX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount;
            }
        }
    }

    else if( eDir == DIR_TX )
    {
         if( byIdType == TYPE_ID_CAN_STANDARD )
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case10:eDir=DIR_TX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case11:eDir=DIR_TX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount;
            }
            else 
            {
            //Case12:eDir=DIR_TX; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount +
                                    m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount;
            }
        }
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case13:eDir=DIR_TX; byIdType=TYPE_ID_CAN_EXTEND;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case14:eDir=DIR_TX; byIdType=TYPE_ID_CAN_EXTEND;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
            else 
            {
            //Case15:eDir=DIR_TX; byIdType=TYPE_ID_CAN_EXTEND;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount +
                                    m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
        }
        else
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case16:eDir=DIR_TX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case17:eDir=DIR_TX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
            else 
            {
            //Case18:eDir=DIR_TX; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unTotalTxMsgCount;
            }
        }
    }


    else
    {
        if( byIdType == TYPE_ID_CAN_STANDARD )
        {
            if( byMsgType == TYPE_MSG_CAN_NON_RTR )
            {
            //Case19:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount+
                            m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount;
                
            }
            else if( byMsgType == TYPE_MSG_CAN_RTR)
            {
            //Case20:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount+
                            m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount;
            }
            else 
            {
            //Case21:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_STANDARD;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount;
            }
        }
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case22:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case23:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
            else 
            {
            //Case24:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_EXTENDED;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
        }
        else
        {
            if( byMsgType == TYPE_MSG_CAN_RTR )
            {
            //Case25:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount;
            }
            else if( byMsgType == TYPE_MSG_CAN_NON_RTR)
            {
            //Case26:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_NON_RTR
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
            else 
            {
            //Case27:eDir=DIR_ALL; byIdType=TYPE_ID_CAN_ALL;byMsgType=TYPE_MSG_CAN_ALL
                nMsgCount = m_sBusStatistics[unChannelIndex].m_unRxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unRxEXTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxSTDMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTD_RTRMsgCount +
                            m_sBusStatistics[unChannelIndex].m_unTxEXTDMsgCount;
            }
        }
    }
    LeaveCriticalSection(&m_omCritSecBS);
    return 0;
}

/******************************************************************************
Function Name  :  BSC_GetTotalErrCount
Input(s)       :  1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                    CAN_CHANNEL_ALL
                  2.eDir can be DIR_RX, DIR_TX, DIR_ALL
                  3.nErrCount contain the Total error count on function return
Output         :  HRESULT, contain Error information
Functionality  :  Returns the total number of error messages occurred while 
                  receiving or transmitting  
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetTotalErrCount(UINT unChannelIndex, eDirection eDir, 
                                               UINT& nErrCount)
{
    EnterCriticalSection(&m_omCritSecBS);

    //eDir == DIR_RX
    if( DIR_RX == eDir )
    {
        nErrCount = m_sBusStatistics[ unChannelIndex ].m_unErrorRxCount;
    }
    //eDir == DIR_TX
    else if( DIR_TX == eDir )
    {
        nErrCount = m_sBusStatistics[ unChannelIndex ].m_unErrorTxCount;
    }
    //eDir == DIR_ALL
    else
    {
        nErrCount = m_sBusStatistics[ unChannelIndex ].m_unErrorTotalCount;
    }
    LeaveCriticalSection(&m_omCritSecBS);
    return 0;
}

/******************************************************************************
Function Name  :  BSC_GetAvgMsgCountPerSec
Input(s)       :  1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                    CAN_CHANNEL_ALL
                  2.eDir can be DIR_RX, DIR_TX, DIR_ALL
                  3.byIdType can be TYPE_ID_CAN_STANDARD, TYPE_ID_CAN_EXTENDED,
                    TYPE_ID_CAN_ALL
                  4.fMsgCount, on return it will contain the Average message 
                    count per second.
Output         :  HRESULT, contain Error information
Functionality  :  Returns average number of msgs per second(Msg/s) 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetAvgMsgCountPerSec(UINT unChannelIndex, 
                              eDirection eDir, BYTE byIdType, double& dMsgRate)
{
    
/*-----------------------------------------------------------------------------
    S.no         eDir          byIdType         
------------------------------------------------
    1.         DIR_RX       TYPE_ID_CAN_STANDARD
    2.         DIR_RX       TYPE_ID_CAN_EXTENDED
    3.         DIR_RX       TYPE_ID_CAN_ALL
    4.         DIR_TX       TYPE_ID_CAN_STANDARD
    5.         DIR_TX       TYPE_ID_CAN_EXTENDED
    6.         DIR_TX       TYPE_ID_CAN_ALL
    7.         DIR_ALL      TYPE_ID_CAN_STANDARD
    8.         DIR_ALL      TYPE_ID_CAN_EXTENDED
    9.         DIR_ALL      TYPE_ID_CAN_ALL
-----------------------------------------------------------------------------*/

    EnterCriticalSection(&m_omCritSecBS);

    if( eDir == DIR_RX )
    {
        if( byIdType == TYPE_ID_CAN_STANDARD)
        {
        //eDir == DIR_RX, byIdType = TYPE_ID_CAN_STANDARD
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dRxSTDMsgRate;
        }
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
        //eDir == DIR_RX, byIdType = TYPE_ID_CAN_EXTENDED
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dRxEXTMsgRate;
        }
        else
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dTotalRxMsgRate;
        }
    }
    else if( eDir == DIR_TX )
    {
        //eDir == DIR_TX, byIdType = TYPE_ID_CAN_STANDARD
        if( byIdType == TYPE_ID_CAN_STANDARD )
        {
           dMsgRate = m_sBusStatistics[unChannelIndex].m_dTxSTDMsgRate;
        }
        //eDir == DIR_TX, byIdType = TYPE_ID_CAN_EXTENDED
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dTxEXTMsgRate;
        }
        //eDir == DIR_TX, byIdType = TYPE_ID_CAN_ALL
        else
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dTotalTxMsgRate;
        }
    }
    else
    {
        //eDir == DIR_ALL, byIdType = TYPE_ID_CAN_STANDARD
        if( byIdType == TYPE_ID_CAN_STANDARD )
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dTxSTDMsgRate +
                       m_sBusStatistics[unChannelIndex].m_dRxSTDMsgRate;
        }
        //eDir == DIR_ALL, byIdType = TYPE_ID_CAN_EXTENDED
        else if( byIdType == TYPE_ID_CAN_EXTENDED )
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_dTxEXTMsgRate +
                       m_sBusStatistics[unChannelIndex].m_dRxEXTMsgRate;
        }
        //eDir == DIR_ALL, byIdType = TYPE_ID_CAN_ALL
        else
        {
            dMsgRate = m_sBusStatistics[unChannelIndex].m_unMsgPerSecond;
        }
    }
   // dMsgRate = (double)(dMsgRate / m_dDiffTime);
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}

/******************************************************************************
Function Name  :  BSC_GetAvgErrCountPerSec
Input(s)       :  1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                    CAN_CHANNEL_ALL
                  2.eDir can be DIR_RX, DIR_TX, DIR_ALL
                  3.fErrCount, on return it contain the Average Error count per
                    Second.
Output         :  HRESULT, contain Error information
Functionality  :  Returns average number of errors per second(Err/s) 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetAvgErrCountPerSec(UINT unChannelIndex, 
                                eDirection eDir, double& dErrCount)
{
    EnterCriticalSection(&m_omCritSecBS);

    //eDir == DIR_RX
    if( eDir == DIR_RX )
    {
        dErrCount = m_sBusStatistics[unChannelIndex].m_dErrorRxRate;
    }
    //eDir == DIR_TX
    else if( eDir == DIR_TX )
    {
        dErrCount = m_sBusStatistics[unChannelIndex].m_dErrorTxRate;
    }
    //eDir == DIR_ALL
    else
    {
        dErrCount = m_sBusStatistics[unChannelIndex].m_dErrorRate;
    }
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}

/******************************************************************************
Function Name  :  BSC_GetBusLoad
Input(s)       :  1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                    CAN_CHANNEL_ALL
                  2.eLoad can be CURRENT, AVERAGE, PEAK
                  3.dBusLoad, on function return it contain Bus Load
Output         :  HRESULT, contain Error information
Functionality  :  Returns the bus load  
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetBusLoad(UINT unChannelIndex, eLOAD eLoad, double &dBusLoad)
{
    EnterCriticalSection(&m_omCritSecBS);

    //eLoad = AVERAGE
    if( eLoad == AVERAGE )
    {
        dBusLoad = m_sBusStatistics[unChannelIndex].m_dAvarageBusLoad;
    }
    //eLoad = PEAK
    else if( eLoad == PEAK )
    {
        dBusLoad = m_sBusStatistics[unChannelIndex].m_dPeakBusLoad;
    }
    //eLoad = CURRENT
    else
    {
        dBusLoad = m_sBusStatistics[unChannelIndex].m_dBusLoad;
    }
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}

/******************************************************************************
Function Name  : BSC_GetErrorCounter 
Input(s)       : 1.unChannelIndex can be CAN_CHANNEL_1, CAN_CHANNEL_2,
                   CAN_CHANNEL_ALL 
                 2.eDir can be DIR_RX, DIR_TX, DIR_ALL
                 3.eLoad can be CURRENT, AVERAGE, PEAK
                 4.ucErrCounter, on return contains the error counter.
Output         :  HRESULT, contain Error information
Functionality  :  Returns controller status  
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetErrorCounter(UINT unChannelIndex, eDirection eDir, 
                                              eLOAD eLoad, UCHAR &ucErrCounter)
{
    EnterCriticalSection(&m_omCritSecBS);

    if( eDir == DIR_RX )
    {
    //eDir == DIR_RX; eLoad == CURRENT
        if( eLoad == CURRENT )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucRxErrorCounter;    
        }
    //eDir == DIR_RX; eLoad == PEAK
        else if( eLoad == PEAK )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucRxPeakErrorCount;
        }
    }
    else if( eDir == DIR_TX )
    {
    //eDir == DIR_TX; eLoad == CURRENT
        if( eLoad == CURRENT )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucTxErrorCounter;
        }
    //eDir == DIR_TX; eLoad == PEAK
        else if( eLoad == PEAK )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucTxPeakErrorCount;
        }
    }
    else
    {
        //eDir == DIR_ALL; eLoad == CURRENT
        if( eLoad == CURRENT )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucRxErrorCounter +
                           m_sBusStatistics[unChannelIndex].m_ucTxErrorCounter; 
        }
        //eDir == DIR_ALL; eLoad == PEAK
        else if( eLoad == PEAK )
        {
            ucErrCounter = m_sBusStatistics[unChannelIndex].m_ucRxPeakErrorCount +
                           m_sBusStatistics[unChannelIndex].m_ucTxPeakErrorCount;
        }
    }
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}
/******************************************************************************
Function Name  :  BSC_SetBaudRate
Input(s)       :  -
Output         :  BOOL, return the error state
Functionality  :  Starts the Read thread. 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_SetBaudRate(UINT unChannelIndex, double dBaudRate)
{
    EnterCriticalSection(&m_omCritSecBS);

    ASSERT(unChannelIndex < defNO_OF_CHANNELS);
    m_sBusStatistics[unChannelIndex].m_dBaudRate = dBaudRate;
    LeaveCriticalSection(&m_omCritSecBS);
    return 0L;
}
/******************************************************************************
Function Name  :  BSC_ucGetControllerStatus
Input(s)       :  1.UINT unChannelIndex
Output         :  UCHAR Controller status
Functionality  :  This Function renders controller status
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  09/09/2010
Modifications  :  
******************************************************************************/
UCHAR CBusStatisticCAN::BSC_ucGetControllerStatus(UINT unChannelIndex)
{
    return m_sBusStatistics[unChannelIndex].m_ucStatus;
}
/******************************************************************************
Function Name  :  BSC_GetBusStatistics
Input(s)       :  1.UINT unChannelIndex
                  2.SBUSSTATISTICS will have the required channel's 
                  BusStatistics
Output         :  HRESULT function return status
Functionality  :  This Function returns the required channel's BusStatistics
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  15/09/2010
Modifications  :  
******************************************************************************/
HRESULT CBusStatisticCAN::BSC_GetBusStatistics(UINT unChannelIndex, SBUSSTATISTICS& sBusStatistics)
{
    sBusStatistics = m_sBusStatistics[unChannelIndex];
    return S_OK;
}
/******************************************************************************
Function Name  :  OnTimeWrapper
Input(s)       :  1.hwnd - NULL
                  2.uMsg - 0
                  3.idEvent - 0
                  4.dwTime - Time Elapsed
Output         :  -
Functionality  :  This Function will be called by the Timer event 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  09/09/2010
Modifications  :  
******************************************************************************/
void CALLBACK CBusStatisticCAN::OnTimeWrapper(HWND, UINT, UINT_PTR, DWORD)
{

    CBusStatisticCAN *pTempClass = (CBusStatisticCAN*)sm_pouBSCan; // cast the void pointer
    pTempClass->vCalculateBusParametres(); // call non-static function
}

/******************************************************************************
Function Name  :  bStartBSReadThread
Input(s)       :  -
Output         :  BOOL, return the error state
Functionality  :  Starts the Read thread. 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
BOOL CBusStatisticCAN::bStartBSReadThread(void)
{   
    m_ouReadThread.m_pBuffer = this;
    m_ouReadThread.bStartThread(BSDataReadThreadProc);
    
    return TRUE;
}

/******************************************************************************
Function Name  :  vInitialiseBSData
Input(s)       :  -
Output         :  -
Functionality  :  Initialises the m_sbusstatistics structor 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
void CBusStatisticCAN::vInitialiseBSData(void)
{   
    EnterCriticalSection(&m_omCritSecBS);
  
    for( int nChannelIndex = 0; nChannelIndex < defNO_OF_CHANNELS; nChannelIndex++ )
    {
        // Reset whole statucture
        memset( &m_sBusStatistics[ nChannelIndex ] , 0, sizeof(SBUSSTATISTICS) );
        memset( &m_sPrevStatData[ nChannelIndex ] , 0, sizeof(SBUSSTATISTICS) );
        // Set Baud rate Manually
        m_sBusStatistics[ nChannelIndex ].m_dBaudRate = _tstof(defBAUDRATE);
    }


    memset( &m_unPrevStandardCount, 0, sizeof( m_unPrevStandardCount ) );
    memset( &m_unPrevExtendedCount, 0, sizeof( m_unPrevExtendedCount ) );
    memset( &m_unPrevStandardRTRCount, 0, sizeof( m_unPrevStandardRTRCount ) );
    memset( &m_unPrevExtendedRTRCount, 0, sizeof( m_unPrevExtendedRTRCount ) );
    memset( &m_unPrevErrorTotalCount, 0, sizeof( m_unPrevErrorTotalCount ) );

    m_nFactorSTDFrame     = defBITS_STD_FRAME + defBITS_INTER_FRAME;
    m_nFactorEXTDFrame    = defBITS_EXTD_FRAME + defBITS_INTER_FRAME;
    m_nFactorSTDRTRFrame  = m_nFactorSTDFrame;
    m_nFactorEXTDRTRFrame = m_nFactorEXTDFrame;
    m_nFactorErrorFrame   = defBITS_ERROR_FRAME + defBITS_INTER_FRAME;
    m_dDiffTime           = defTIME_INTERVAL;
    m_unPreviousTime = -1;
    LeaveCriticalSection(&m_omCritSecBS);
}

/******************************************************************************
Function Name  :  vCalculateDiffTime
Input(s)       :  -
Output         :  -
Functionality  :  Calculates the differential time in sec 
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
void CBusStatisticCAN::vCalculateDiffTime(void)
{

    // check if the previous time value is not stored. take 1.0s as initial
    // value for calculation.
    if(m_unPreviousTime != -1 )
    {
        m_dDiffTime         = CTimeManager::nCalculateCurrTimeStamp(FALSE) -
                            m_unPreviousTime;
        m_unPreviousTime += static_cast<UINT>(m_dDiffTime);
        m_dDiffTime         = m_dDiffTime / defDIV_FACT_FOR_SECOND;
    }
    else
    {
        m_unPreviousTime = CTimeManager::nCalculateCurrTimeStamp(FALSE);
    }
}

/******************************************************************************
Function Name  :  vUpdateBusStatistics 
Input(s)       :  sCanData 
Output         :  - 
Functionality  :  Calculate the Bus statistics in m_sBusStatistics structure
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  06/09/2010
Modifications  :  
******************************************************************************/
void CBusStatisticCAN::vUpdateBusStatistics(STCANDATA &sCanData)
{
    EnterCriticalSection(&m_omCritSecBS);
   
    m_sCurrEntry = sCanData.m_uDataInfo.m_sCANMsg;
    int nCurrentChannelIndex = sCanData.m_uDataInfo.m_sCANMsg.m_ucChannel - 1;

    if ((nCurrentChannelIndex < 0) || (nCurrentChannelIndex > (defNO_OF_CHANNELS - 1)))
    {
        nCurrentChannelIndex = 0;//take appropriate action
    }
    if(m_sCurrEntry.m_ucRTR == 1)
        m_sBusStatistics[ nCurrentChannelIndex ].m_unDLCCount+=
                            sCanData.m_uDataInfo.m_sCANMsg.m_ucDataLen;
   
    //is it Tx Message
    if(IS_TX_MESSAGE(sCanData.m_ucDataType))
    {
        if(IS_ERR_MESSAGE(sCanData.m_ucDataType))
        {
             m_sBusStatistics[nCurrentChannelIndex].m_unErrorTxCount++;
        }
        else
        {
            m_sBusStatistics[nCurrentChannelIndex].m_unTotalTxMsgCount++;
            if (m_sCurrEntry.m_ucRTR == 0) // Non RTR message
            {
                if (m_sCurrEntry.m_ucEXTENDED == 0)
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unTxSTDMsgCount++;
                }
                else
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unTxEXTDMsgCount++;
                }
            }
            else // RTR message
            {
                if (m_sCurrEntry.m_ucEXTENDED == 0)
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unTxSTD_RTRMsgCount++;
                }
                else
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unTxEXTD_RTRMsgCount++;
                }
            }
        }
    }
    //is it Rx Message
    else if(IS_RX_MESSAGE(sCanData.m_ucDataType))
    {
        if(IS_ERR_MESSAGE(sCanData.m_ucDataType))
        {
            m_sBusStatistics[nCurrentChannelIndex].m_unErrorTxCount++;
        }
        else
        {
            m_sBusStatistics[nCurrentChannelIndex].m_unTotalRxMsgCount++;
            if (m_sCurrEntry.m_ucRTR == 0) // Non RTR message
            {
                if (m_sCurrEntry.m_ucEXTENDED == 0)
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unRxSTDMsgCount++;
                }
                else
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unRxEXTDMsgCount++;
                }
            }
            else // RTR message
            {
                if (m_sCurrEntry.m_ucEXTENDED == 0)
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unRxSTD_RTRMsgCount++;
                }
                else
                {
                    m_sBusStatistics[ nCurrentChannelIndex ].m_unRxEXTD_RTRMsgCount++;
                }
            }
        }
    }
    else
    {
        //Is it is Error
        m_sBusStatistics[nCurrentChannelIndex].m_unErrorTotalCount++;
        if (sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType == ERROR_BUS)
        {
            // Update Statistics information
            m_sBusStatistics[ nCurrentChannelIndex ].m_unErrorTotalCount++;
            USHORT usErrorID = sCanData.m_uDataInfo.m_sErrInfo.m_ucReg_ErrCap & 0xE0;
            // Received message
            if (usErrorID & 0x20)
            {
                m_sBusStatistics[ nCurrentChannelIndex ].m_unErrorRxCount++;
            }
            else
            {
                m_sBusStatistics[ nCurrentChannelIndex ].m_unErrorTxCount++;
            }
        }
    }
    
   LeaveCriticalSection(&m_omCritSecBS);
}
/******************************************************************************
Function Name  :  vCalculateBusParametres
Input(s)       :  -
Output         :  -
Functionality  :  Claculates the Bus Statistics using m_sBusStatistics 
                  structure  
Member of      :  CBusStatisticCAN
Friend of      :  -
Author(s)      :  Venkatanarayana Makam
Date Created   :  09/09/2010
Modifications  :  
******************************************************************************/
void CBusStatisticCAN::vCalculateBusParametres(void)
{
    EnterCriticalSection(&m_omCritSecBS);
    vCalculateDiffTime();
    for(int nChannelIndex =0; nChannelIndex <defNO_OF_CHANNELS; nChannelIndex++)
    {
        m_sBusStatistics[nChannelIndex].m_nSamples++;
        m_sBusStatistics[ nChannelIndex ].m_unTotalMsgCount =
                m_sBusStatistics[ nChannelIndex ].m_unTotalTxMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTotalRxMsgCount;
        //***** Total Message Rate *****//
        m_sBusStatistics[ nChannelIndex ].m_unMsgPerSecond =
            m_sBusStatistics[ nChannelIndex ].m_unTotalMsgCount -
            m_sPrevStatData[ nChannelIndex ].m_unTotalMsgCount;
        
        m_sBusStatistics[ nChannelIndex ].m_unMsgPerSecond =
            static_cast<UINT>
            (m_sBusStatistics[ nChannelIndex ].m_unMsgPerSecond / m_dDiffTime );
        // Calculate Error Count & Rate
        m_sBusStatistics[ nChannelIndex ].m_unErrorTotalCount =
            m_sBusStatistics[ nChannelIndex ].m_unErrorRxCount +
            m_sBusStatistics[ nChannelIndex ].m_unErrorTxCount;

        m_sBusStatistics[ nChannelIndex ].m_dErrorRate =
            m_sBusStatistics[ nChannelIndex ].m_unErrorTotalCount -
            m_sPrevStatData[ nChannelIndex ].m_unErrorTotalCount;

        // Transmitted messages
        // Calculate Total Tx Message Rate
        m_sBusStatistics[nChannelIndex].m_dTotalTxMsgRate =
                (m_sBusStatistics[nChannelIndex].m_unTotalTxMsgCount -
                m_sPrevStatData[nChannelIndex].m_unTotalTxMsgCount);

        // Calculate STD Tx Message Rate
        m_sBusStatistics[ nChannelIndex ].m_dTxSTDMsgRate =
            (m_sBusStatistics[ nChannelIndex ].m_unTxSTDMsgCount -
            m_sPrevStatData[ nChannelIndex ].m_unTxSTDMsgCount);

        // Calculate Extended Tx Message Rate
        m_sBusStatistics[ nChannelIndex ].m_dTxEXTMsgRate =
            (m_sBusStatistics[ nChannelIndex ].m_unTxEXTDMsgCount -
            m_sPrevStatData[ nChannelIndex ].m_unTxEXTDMsgCount);

        // Calculate Tx Error Rate
        m_sBusStatistics[ nChannelIndex ].m_dErrorTxRate =
            (m_sBusStatistics[ nChannelIndex ].m_unErrorTxCount -
            m_sPrevStatData[ nChannelIndex ].m_unErrorTxCount);

        // Received messages
        // Calculate Total Rx Message Rate
        m_sBusStatistics[nChannelIndex].m_dTotalRxMsgRate =
                (m_sBusStatistics[nChannelIndex].m_unTotalRxMsgCount -
                m_sPrevStatData[nChannelIndex].m_unTotalRxMsgCount);
        // Calculate STD Rx Message Rate
        m_sBusStatistics[ nChannelIndex ].m_dRxSTDMsgRate =
            (m_sBusStatistics[ nChannelIndex ].m_unRxSTDMsgCount - 
            m_sPrevStatData[ nChannelIndex ].m_unRxSTDMsgCount);

        // Calculate Extended Rx Message Rate
        m_sBusStatistics[ nChannelIndex ].m_dRxEXTMsgRate =
            (m_sBusStatistics[ nChannelIndex ].m_unRxEXTDMsgCount -
            m_sPrevStatData[ nChannelIndex ].m_unRxEXTDMsgCount);

        // Calculate Rx Error Rate
        m_sBusStatistics[ nChannelIndex ].m_dErrorRxRate =
            (m_sBusStatistics[ nChannelIndex ].m_unErrorRxCount -
            m_sPrevStatData[ nChannelIndex ].m_unErrorRxCount);

        UINT unTotalbitPerSecond = 0;
        // Get the bits for standard data frame
        unTotalbitPerSecond =
            ( m_sBusStatistics[ nChannelIndex ].m_unRxSTDMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTxSTDMsgCount -
                m_unPrevStandardCount[ nChannelIndex ] ) *
                m_nFactorSTDFrame;
        
        // initialise the previous value to current value.
        m_unPrevStandardCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unRxSTDMsgCount +
            m_sBusStatistics[ nChannelIndex ].m_unTxSTDMsgCount;

        // Get the bits for extended data frame
        unTotalbitPerSecond +=
            ( m_sBusStatistics[ nChannelIndex ].m_unRxEXTDMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTxEXTDMsgCount -
                m_unPrevExtendedCount[ nChannelIndex ] ) *
            m_nFactorEXTDFrame;
        
        // initialise the previous value to current value.
        m_unPrevExtendedCount[ nChannelIndex ] = 
                m_sBusStatistics[ nChannelIndex ].m_unRxEXTDMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTxEXTDMsgCount;

        // Get and Update Tx Error Counter value
        SERROR_CNT sErrorCounter;
        if (m_pouDIL_CAN->DILC_GetErrorCount( sErrorCounter, nChannelIndex, ERR_CNT) == S_OK)
        {
            m_sBusStatistics[ nChannelIndex ].m_ucTxErrorCounter =
                                        sErrorCounter.m_ucTxErrCount;
            m_sBusStatistics[ nChannelIndex ].m_ucRxErrorCounter =
                                        sErrorCounter.m_ucRxErrCount;
        }
        if (m_pouDIL_CAN->DILC_GetErrorCount( sErrorCounter, nChannelIndex, PEAK_ERR_CNT) == S_OK)
        {
            m_sBusStatistics[ nChannelIndex ].m_ucTxPeakErrorCount=
                                        sErrorCounter.m_ucTxErrCount;
            m_sBusStatistics[ nChannelIndex ].m_ucRxPeakErrorCount =
                                        sErrorCounter.m_ucRxErrCount;
        }
        // Get the controller status
        LPARAM lParam = 0;
        if (m_pouDIL_CAN->DILC_GetControllerParams( lParam, nChannelIndex,
                                                        HW_MODE) == S_OK)
        {
            m_sBusStatistics[ nChannelIndex ].m_ucStatus = (UCHAR)lParam;
        }
        // Get the bits for data length
        unTotalbitPerSecond +=
            m_sBusStatistics[ nChannelIndex ].m_unDLCCount * defBITS_IN_BYTE;
        // Get the bits for RTR standard frame.
        unTotalbitPerSecond +=
            ( m_sBusStatistics[ nChannelIndex ].m_unRxSTD_RTRMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTxSTD_RTRMsgCount -
                m_unPrevStandardRTRCount[ nChannelIndex ] ) *
            m_nFactorSTDRTRFrame;

        // Get the bits for RTR standard frame.
        unTotalbitPerSecond +=
            ( m_sBusStatistics[ nChannelIndex ].m_unRxEXTD_RTRMsgCount +
                m_sBusStatistics[ nChannelIndex ].m_unTxEXTD_RTRMsgCount -
                m_unPrevExtendedRTRCount[ nChannelIndex ] ) *
            m_nFactorEXTDRTRFrame;

        //total bit for error message
        unTotalbitPerSecond +=
            ( m_sBusStatistics[ nChannelIndex ].m_unErrorTotalCount -
                m_unPrevErrorTotalCount[ nChannelIndex ] ) *
            m_nFactorErrorFrame;

        // Get the total bits per second
        DOUBLE dBusLoad  = unTotalbitPerSecond / m_dDiffTime;
        // Get the load

	    if(dBusLoad != 0)
		    dBusLoad = dBusLoad /
			    ( m_sBusStatistics[ nChannelIndex ].m_dBaudRate * defBITS_KBUAD_RATE );
	    // Get the percentage load
        dBusLoad = dBusLoad * defMAX_PERCENTAGE_BUS_LOAD;
        // check for peak load
        if( dBusLoad > m_sBusStatistics[ nChannelIndex ].m_dPeakBusLoad )
        {
            // if peak load is greater than or equal to 100% assign it 99.99%
            if( dBusLoad > defMAX_PERCENTAGE_BUS_LOAD )
            {
                dBusLoad = defMAX_PERCENTAGE_BUS_LOAD_ALLOWED;
            }
            m_sBusStatistics[ nChannelIndex ].m_dPeakBusLoad = dBusLoad ;
        }
        // assign it to global data structure.
        m_sBusStatistics[ nChannelIndex ].m_dBusLoad = dBusLoad ;
    	
        // Calculate avarage bus load
        m_sBusStatistics[ nChannelIndex ].m_dTotalBusLoad += dBusLoad;
        // Increament samples
        
        // Calculate Avarage bus load
        m_sBusStatistics[ nChannelIndex ].m_dAvarageBusLoad = 
                            m_sBusStatistics[ nChannelIndex ].m_dTotalBusLoad /
                            m_sBusStatistics[ nChannelIndex ].m_nSamples;
       
        // Initialise previous values with current values.
        m_unPrevStandardCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unRxSTDMsgCount +
            m_sBusStatistics[ nChannelIndex ].m_unTxSTDMsgCount;
        
        m_unPrevExtendedCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unRxEXTDMsgCount +
            m_sBusStatistics[ nChannelIndex ].m_unTxEXTDMsgCount;
        
        m_unPrevStandardRTRCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unRxSTD_RTRMsgCount +
            m_sBusStatistics[ nChannelIndex ].m_unTxSTD_RTRMsgCount;

        m_unPrevExtendedRTRCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unRxEXTD_RTRMsgCount +
            m_sBusStatistics[ nChannelIndex ].m_unTxEXTD_RTRMsgCount;

        m_unPrevErrorTotalCount[ nChannelIndex ] =
            m_sBusStatistics[ nChannelIndex ].m_unErrorTotalCount;
        
        m_sBusStatistics[ nChannelIndex ].m_unDLCCount = 0;
        
        m_sPrevStatData[ nChannelIndex ] = m_sBusStatistics[ nChannelIndex ];
    }
    LeaveCriticalSection(&m_omCritSecBS);
}
