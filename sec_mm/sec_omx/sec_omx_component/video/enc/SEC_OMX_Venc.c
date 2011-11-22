/*
 *
 * Copyright 2010 Samsung Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @file        SEC_OMX_Venc.c
 * @brief
 * @author      SeungBeom Kim (sbcrux.kim@samsung.com)
 *              Yunji Kim (yunji.kim@samsung.com)
 * @version     1.0
 * @history
 *   2010.7.15 : Create
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SEC_OMX_Macros.h"
#include "SEC_OSAL_Event.h"
#include "SEC_OMX_Venc.h"
#include "SEC_OMX_Basecomponent.h"
#include "SEC_OSAL_Thread.h"

#undef  SEC_LOG_TAG
#define SEC_LOG_TAG    "SEC_VIDEO_ENC"
#define SEC_LOG_OFF
#include "SEC_OSAL_Log.h"

#define ONE_FRAME_OUTPUT  /* only one frame output for Android */
#define S5PC110_ENCODE_IN_DATA_BUFFER /* for Android s5pc110 0copy*/


inline void SEC_UpdateFrameSize(OMX_COMPONENTTYPE *pOMXComponent)
{
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_BASEPORT      *secInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    SEC_OMX_BASEPORT      *secOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];

    if ((secOutputPort->portDefinition.format.video.nFrameWidth !=
            secInputPort->portDefinition.format.video.nFrameWidth) ||
        (secOutputPort->portDefinition.format.video.nFrameHeight !=
            secInputPort->portDefinition.format.video.nFrameHeight)) {
        OMX_U32 width = 0, height = 0;

        secOutputPort->portDefinition.format.video.nFrameWidth =
            secInputPort->portDefinition.format.video.nFrameWidth;
        secOutputPort->portDefinition.format.video.nFrameHeight =
            secInputPort->portDefinition.format.video.nFrameHeight;
        width = secOutputPort->portDefinition.format.video.nStride =
            secInputPort->portDefinition.format.video.nStride;
        height = secOutputPort->portDefinition.format.video.nSliceHeight =
            secInputPort->portDefinition.format.video.nSliceHeight;

        switch(secOutputPort->portDefinition.format.video.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420SemiPlanar:
            if (width && height)
                secOutputPort->portDefinition.nBufferSize = (width * height * 3) / 2;
            break;
        default:
            if (width && height)
                secOutputPort->portDefinition.nBufferSize = width * height * 2;
            break;
        }
    }

  return ;
}

OMX_ERRORTYPE SEC_OMX_UseBuffer(
    OMX_IN OMX_HANDLETYPE            hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBufferHdr,
    OMX_IN OMX_U32                   nPortIndex,
    OMX_IN OMX_PTR                   pAppPrivate,
    OMX_IN OMX_U32                   nSizeBytes,
    OMX_IN OMX_U8                   *pBuffer)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;
    OMX_BUFFERHEADERTYPE  *temp_bufferHeader = NULL;
    int                    i = 0;

    FunctionIn();

    if (hComponent == NULL)    {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pSECPort = &pSECComponent->pSECPort[nPortIndex];
    if (nPortIndex >= pSECComponent->portParam.nPorts) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }
    if (pSECPort->portState != OMX_StateIdle) {
        ret = OMX_ErrorIncorrectStateOperation;
        goto EXIT;
    }

    if (CHECK_PORT_TUNNELED(pSECPort) && CHECK_PORT_BUFFER_SUPPLIER(pSECPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    temp_bufferHeader = (OMX_BUFFERHEADERTYPE *)SEC_OSAL_Malloc(sizeof(OMX_BUFFERHEADERTYPE));
    if (temp_bufferHeader == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    SEC_OSAL_Memset(temp_bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));

    for (i = 0; i < pSECPort->portDefinition.nBufferCountActual; i++) {
        if (pSECPort->bufferStateAllocate[i] == BUFFER_STATE_FREE) {
            pSECPort->bufferHeader[i] = temp_bufferHeader;
            pSECPort->bufferStateAllocate[i] = (BUFFER_STATE_ASSIGNED | HEADER_STATE_ALLOCATED);
            INIT_SET_SIZE_VERSION(temp_bufferHeader, OMX_BUFFERHEADERTYPE);
            temp_bufferHeader->pBuffer        = pBuffer;
            temp_bufferHeader->nAllocLen        = nSizeBytes;
            temp_bufferHeader->pAppPrivate        = pAppPrivate;
            if (nPortIndex == INPUT_PORT_INDEX)
                temp_bufferHeader->nInputPortIndex = INPUT_PORT_INDEX;
            else
                temp_bufferHeader->nOutputPortIndex = OUTPUT_PORT_INDEX;

            pSECPort->assignedBufferNum++;
            if (pSECPort->assignedBufferNum == pSECPort->portDefinition.nBufferCountActual) {
                pSECPort->portDefinition.bPopulated = OMX_TRUE;
                /* SEC_OSAL_MutexLock(pSECComponent->compMutex); */
                SEC_OSAL_SemaphorePost(pSECPort->loadedResource);
                /* SEC_OSAL_MutexUnlock(pSECComponent->compMutex); */
            }
            *ppBufferHdr = temp_bufferHeader;
            ret = OMX_ErrorNone;
            goto EXIT;
        }
    }
    ret = OMX_ErrorInsufficientResources;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_AllocateBuffer(
    OMX_IN OMX_HANDLETYPE            hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBuffer,
    OMX_IN OMX_U32                   nPortIndex,
    OMX_IN OMX_PTR                   pAppPrivate,
    OMX_IN OMX_U32                   nSizeBytes)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;
    OMX_BUFFERHEADERTYPE  *temp_bufferHeader = NULL;
    OMX_U8                *temp_buffer = NULL;
    int                    i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pSECPort = &pSECComponent->pSECPort[nPortIndex];
    if (nPortIndex >= pSECComponent->portParam.nPorts) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }
/*
    if (pSECPort->portState != OMX_StateIdle ) {
        ret = OMX_ErrorIncorrectStateOperation;
        goto EXIT;
    }
*/
    if (CHECK_PORT_TUNNELED(pSECPort) && CHECK_PORT_BUFFER_SUPPLIER(pSECPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    temp_buffer = SEC_OSAL_Malloc(sizeof(OMX_U8) * nSizeBytes);
    if (temp_buffer == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    temp_bufferHeader = (OMX_BUFFERHEADERTYPE *)SEC_OSAL_Malloc(sizeof(OMX_BUFFERHEADERTYPE));
    if (temp_bufferHeader == NULL) {
        SEC_OSAL_Free(temp_buffer);
        temp_buffer = NULL;
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    SEC_OSAL_Memset(temp_bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));

    for (i = 0; i < pSECPort->portDefinition.nBufferCountActual; i++) {
        if (pSECPort->bufferStateAllocate[i] == BUFFER_STATE_FREE) {
            pSECPort->bufferHeader[i] = temp_bufferHeader;
            pSECPort->bufferStateAllocate[i] = (BUFFER_STATE_ALLOCATED | HEADER_STATE_ALLOCATED);
            INIT_SET_SIZE_VERSION(temp_bufferHeader, OMX_BUFFERHEADERTYPE);
            temp_bufferHeader->pBuffer        = temp_buffer;
            temp_bufferHeader->nAllocLen        = nSizeBytes;
            temp_bufferHeader->pAppPrivate        = pAppPrivate;
            if (nPortIndex == INPUT_PORT_INDEX)
                temp_bufferHeader->nInputPortIndex = INPUT_PORT_INDEX;
            else
                temp_bufferHeader->nOutputPortIndex = OUTPUT_PORT_INDEX;
            pSECPort->assignedBufferNum++;
            if (pSECPort->assignedBufferNum == pSECPort->portDefinition.nBufferCountActual) {
                pSECPort->portDefinition.bPopulated = OMX_TRUE;
                /* SEC_OSAL_MutexLock(pSECComponent->compMutex); */
                SEC_OSAL_SemaphorePost(pSECPort->loadedResource);
                /* SEC_OSAL_MutexUnlock(pSECComponent->compMutex); */
            }
            *ppBuffer = temp_bufferHeader;
            ret = OMX_ErrorNone;
            goto EXIT;
        }
    }
    ret = OMX_ErrorInsufficientResources;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_FreeBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_U32        nPortIndex,
    OMX_IN OMX_BUFFERHEADERTYPE *pBufferHdr)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;
    OMX_BUFFERHEADERTYPE  *temp_bufferHeader = NULL;
    OMX_U8                *temp_buffer = NULL;
    int                    i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    pSECPort = &pSECComponent->pSECPort[nPortIndex];

    if (CHECK_PORT_TUNNELED(pSECPort) && CHECK_PORT_BUFFER_SUPPLIER(pSECPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    if ((pSECPort->portState != OMX_StateLoaded) && (pSECPort->portState != OMX_StateInvalid)) {
        (*(pSECComponent->pCallbacks->EventHandler)) (pOMXComponent,
                        pSECComponent->callbackData,
                        (OMX_U32)OMX_EventError,
                        (OMX_U32)OMX_ErrorPortUnpopulated,
                        nPortIndex, NULL);
    }

    for (i = 0; i < pSECPort->portDefinition.nBufferCountActual; i++) {
        if (((pSECPort->bufferStateAllocate[i] | BUFFER_STATE_FREE) != 0) && (pSECPort->bufferHeader[i] != NULL)) {
            if (pSECPort->bufferHeader[i]->pBuffer == pBufferHdr->pBuffer) {
                if (pSECPort->bufferStateAllocate[i] & BUFFER_STATE_ALLOCATED) {
                    SEC_OSAL_Free(pSECPort->bufferHeader[i]->pBuffer);
                    pSECPort->bufferHeader[i]->pBuffer = NULL;
                    pBufferHdr->pBuffer = NULL;
                } else if (pSECPort->bufferStateAllocate[i] & BUFFER_STATE_ASSIGNED) {
                    ; /* None*/
                }
                pSECPort->assignedBufferNum--;
                if (pSECPort->bufferStateAllocate[i] & HEADER_STATE_ALLOCATED) {
                    SEC_OSAL_Free(pSECPort->bufferHeader[i]);
                    pSECPort->bufferHeader[i] = NULL;
                    pBufferHdr = NULL;
                }
                pSECPort->bufferStateAllocate[i] = BUFFER_STATE_FREE;
                ret = OMX_ErrorNone;
                goto EXIT;
            }
        }
    }

EXIT:
    if (ret == OMX_ErrorNone) {
        if (pSECPort->assignedBufferNum == 0 ) {
            SEC_OSAL_Log(SEC_LOG_TRACE, "pSECPort->unloadedResource signal set");
            /* SEC_OSAL_MutexLock(pSECComponent->compMutex); */
            SEC_OSAL_SemaphorePost(pSECPort->unloadedResource);
            /* SEC_OSAL_MutexUnlock(pSECComponent->compMutex); */
            pSECPort->portDefinition.bPopulated = OMX_FALSE;
        }
    }

    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_AllocateTunnelBuffer(SEC_OMX_BASEPORT *pOMXBasePort, OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE                 ret = OMX_ErrorNone;
    SEC_OMX_BASEPORT             *pSECPort = NULL;
    OMX_BUFFERHEADERTYPE         *temp_bufferHeader = NULL;
    OMX_U8                       *temp_buffer = NULL;
    OMX_U32                       bufferSize = 0;
    OMX_PARAM_PORTDEFINITIONTYPE  portDefinition;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_ERRORTYPE SEC_OMX_FreeTunnelBuffer(SEC_OMX_BASEPORT *pOMXBasePort, OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    SEC_OMX_BASEPORT* pSECPort = NULL;
    OMX_BUFFERHEADERTYPE* temp_bufferHeader = NULL;
    OMX_U8 *temp_buffer = NULL;
    OMX_U32 bufferSize = 0;
    OMX_PARAM_PORTDEFINITIONTYPE portDefinition;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_ERRORTYPE SEC_OMX_ComponentTunnelRequest(
    OMX_IN OMX_HANDLETYPE hComp,
    OMX_IN OMX_U32        nPort,
    OMX_IN OMX_HANDLETYPE hTunneledComp,
    OMX_IN OMX_U32        nTunneledPort,
    OMX_INOUT OMX_TUNNELSETUPTYPE *pTunnelSetup)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_BOOL SEC_Check_BufferProcess_State(SEC_OMX_BASECOMPONENT *pSECComponent)
{
    if ((pSECComponent->currentState == OMX_StateExecuting) &&
        (pSECComponent->pSECPort[INPUT_PORT_INDEX].portState == OMX_StateIdle) &&
        (pSECComponent->pSECPort[OUTPUT_PORT_INDEX].portState == OMX_StateIdle) &&
        (pSECComponent->transientState != SEC_OMX_TransStateExecutingToIdle) &&
        (pSECComponent->transientState != SEC_OMX_TransStateIdleToExecuting))
        return OMX_TRUE;
    else
        return OMX_FALSE;
}

static OMX_ERRORTYPE SEC_InputBufferReturn(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_BASEPORT      *secOMXInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    SEC_OMX_BASEPORT      *secOMXOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    SEC_OMX_DATABUFFER    *dataBuffer = &pSECComponent->secDataBuffer[INPUT_PORT_INDEX];
    OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader;

    FunctionIn();

    if (bufferHeader != NULL) {
        if (secOMXInputPort->markType.hMarkTargetComponent != NULL ) {
            bufferHeader->hMarkTargetComponent      = secOMXInputPort->markType.hMarkTargetComponent;
            bufferHeader->pMarkData                 = secOMXInputPort->markType.pMarkData;
            secOMXInputPort->markType.hMarkTargetComponent = NULL;
            secOMXInputPort->markType.pMarkData = NULL;
        }

        if (bufferHeader->hMarkTargetComponent != NULL) {
            if (bufferHeader->hMarkTargetComponent == pOMXComponent) {
                pSECComponent->pCallbacks->EventHandler(pOMXComponent,
                                pSECComponent->callbackData,
                                OMX_EventMark,
                                0, 0, bufferHeader->pMarkData);
            } else {
                pSECComponent->propagateMarkType.hMarkTargetComponent = bufferHeader->hMarkTargetComponent;
                pSECComponent->propagateMarkType.pMarkData = bufferHeader->pMarkData;
            }
        }

        if (CHECK_PORT_TUNNELED(secOMXInputPort)) {
            OMX_FillThisBuffer(secOMXInputPort->tunneledComponent, bufferHeader);
        } else {
            bufferHeader->nFilledLen = 0;
            pSECComponent->pCallbacks->EmptyBufferDone(pOMXComponent, pSECComponent->callbackData, bufferHeader);
        }
    }

    if ((pSECComponent->currentState == OMX_StatePause) &&
        ((!CHECK_PORT_BEING_FLUSHED(secOMXInputPort) && !CHECK_PORT_BEING_FLUSHED(secOMXOutputPort)))) {
        SEC_OSAL_SignalReset(pSECComponent->pauseEvent);
        SEC_OSAL_SignalWait(pSECComponent->pauseEvent, DEF_MAX_WAIT_TIME);
    }

    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_InputBufferGetQueue(SEC_OMX_BASECOMPONENT *pSECComponent)
{
    OMX_ERRORTYPE       ret = OMX_ErrorNone;
    SEC_OMX_BASEPORT   *pSECPort = NULL;
    SEC_OMX_DATABUFFER *dataBuffer = NULL;
    SEC_OMX_MESSAGE*    message = NULL;
    SEC_OMX_DATABUFFER *inputUseBuffer = &pSECComponent->secDataBuffer[INPUT_PORT_INDEX];

    FunctionIn();

    pSECPort= &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    dataBuffer = &pSECComponent->secDataBuffer[INPUT_PORT_INDEX];

    if (pSECComponent->currentState != OMX_StateExecuting) {
        ret = OMX_ErrorUndefined;
        goto EXIT;
    } else {
        SEC_OSAL_SemaphoreWait(pSECPort->bufferSemID);
        SEC_OSAL_MutexLock(inputUseBuffer->bufferMutex);
        if (dataBuffer->dataValid != OMX_TRUE) {
            message = (SEC_OMX_MESSAGE *)SEC_OSAL_Dequeue(&pSECPort->bufferQ);
            if (message == NULL) {
                ret = OMX_ErrorUndefined;
                SEC_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                goto EXIT;
            }

            dataBuffer->bufferHeader = (OMX_BUFFERHEADERTYPE *)(message->pCmdData);
            dataBuffer->allocSize = dataBuffer->bufferHeader->nAllocLen;
            dataBuffer->dataLen = dataBuffer->bufferHeader->nFilledLen;
            dataBuffer->remainDataLen = dataBuffer->dataLen;
            dataBuffer->usedDataLen = 0; //dataBuffer->bufferHeader->nOffset;
            dataBuffer->dataValid = OMX_TRUE;
            dataBuffer->nFlags = dataBuffer->bufferHeader->nFlags;
            dataBuffer->timeStamp = dataBuffer->bufferHeader->nTimeStamp;
#ifdef S5PC110_ENCODE_IN_DATA_BUFFER
            pSECComponent->processData[INPUT_PORT_INDEX].dataBuffer = dataBuffer->bufferHeader->pBuffer;
            pSECComponent->processData[INPUT_PORT_INDEX].allocSize = dataBuffer->bufferHeader->nAllocLen;
#endif
            SEC_OSAL_Free(message);
        }
        SEC_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
        ret = OMX_ErrorNone;
    }
EXIT:
    FunctionOut();

    return ret;
}

static OMX_ERRORTYPE SEC_OutputBufferReturn(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_BASEPORT      *secOMXInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    SEC_OMX_BASEPORT      *secOMXOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    SEC_OMX_DATABUFFER    *dataBuffer = &pSECComponent->secDataBuffer[OUTPUT_PORT_INDEX];
    OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader;

    FunctionIn();

    if (bufferHeader != NULL) {
        bufferHeader->nFilledLen = dataBuffer->remainDataLen;
        bufferHeader->nOffset    = 0;
        bufferHeader->nFlags     = dataBuffer->nFlags;
        bufferHeader->nTimeStamp = dataBuffer->timeStamp;

        if (pSECComponent->propagateMarkType.hMarkTargetComponent != NULL) {
            bufferHeader->hMarkTargetComponent = pSECComponent->propagateMarkType.hMarkTargetComponent;
            bufferHeader->pMarkData = pSECComponent->propagateMarkType.pMarkData;
            pSECComponent->propagateMarkType.hMarkTargetComponent = NULL;
            pSECComponent->propagateMarkType.pMarkData = NULL;
        }

        if (bufferHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            pSECComponent->pCallbacks->EventHandler(pOMXComponent,
                            pSECComponent->callbackData,
                            OMX_EventBufferFlag,
                            OUTPUT_PORT_INDEX,
                            bufferHeader->nFlags, NULL);
        }

        if (CHECK_PORT_TUNNELED(secOMXOutputPort)) {
            OMX_EmptyThisBuffer(secOMXOutputPort->tunneledComponent, bufferHeader);
        } else {
            pSECComponent->pCallbacks->FillBufferDone(pOMXComponent, pSECComponent->callbackData, bufferHeader);
        }
    }

    if ((pSECComponent->currentState == OMX_StatePause) &&
        ((!CHECK_PORT_BEING_FLUSHED(secOMXInputPort) && !CHECK_PORT_BEING_FLUSHED(secOMXOutputPort)))) {
        SEC_OSAL_SignalReset(pSECComponent->pauseEvent);
        SEC_OSAL_SignalWait(pSECComponent->pauseEvent, DEF_MAX_WAIT_TIME);
    }
    
    /* reset dataBuffer */
    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OutputBufferGetQueue(SEC_OMX_BASECOMPONENT *pSECComponent)
{
    OMX_ERRORTYPE       ret = OMX_ErrorNone;
    SEC_OMX_BASEPORT   *pSECPort = NULL;
    SEC_OMX_DATABUFFER *dataBuffer = NULL;
    SEC_OMX_MESSAGE    *message = NULL;
    SEC_OMX_DATABUFFER *outputUseBuffer = &pSECComponent->secDataBuffer[OUTPUT_PORT_INDEX];

    FunctionIn();

    pSECPort= &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    dataBuffer = &pSECComponent->secDataBuffer[OUTPUT_PORT_INDEX];

    if (pSECComponent->currentState != OMX_StateExecuting) {
        ret = OMX_ErrorUndefined;
        goto EXIT;
    } else {
        SEC_OSAL_SemaphoreWait(pSECPort->bufferSemID);
        SEC_OSAL_MutexLock(outputUseBuffer->bufferMutex);
        if (dataBuffer->dataValid != OMX_TRUE) {
            message = (SEC_OMX_MESSAGE *)SEC_OSAL_Dequeue(&pSECPort->bufferQ);
            if (message == NULL) {
                ret = OMX_ErrorUndefined;
                SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                goto EXIT;
            }

            dataBuffer->bufferHeader = (OMX_BUFFERHEADERTYPE *)(message->pCmdData);
            dataBuffer->allocSize = dataBuffer->bufferHeader->nAllocLen;
            dataBuffer->dataLen = 0; //dataBuffer->bufferHeader->nFilledLen;
            dataBuffer->remainDataLen = dataBuffer->dataLen;
            dataBuffer->usedDataLen = 0; //dataBuffer->bufferHeader->nOffset;
            dataBuffer->dataValid =OMX_TRUE;
            /* dataBuffer->nFlags = dataBuffer->bufferHeader->nFlags; */
            /* dataBuffer->nTimeStamp = dataBuffer->bufferHeader->nTimeStamp; */
            SEC_OSAL_Free(message);
        }
        SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
        ret = OMX_ErrorNone;
    }
EXIT:
    FunctionOut();

    return ret;

}

static OMX_ERRORTYPE SEC_BufferReset(OMX_COMPONENTTYPE *pOMXComponent, OMX_U32 portIndex)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    /* SEC_OMX_BASEPORT      *pSECPort = &pSECComponent->pSECPort[portIndex]; */
    SEC_OMX_DATABUFFER    *dataBuffer = &pSECComponent->secDataBuffer[portIndex];
    /* OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader; */

    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

    return ret;
}

static OMX_ERRORTYPE SEC_DataReset(OMX_COMPONENTTYPE *pOMXComponent, OMX_U32 portIndex)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    /* SEC_OMX_BASEPORT      *pSECPort = &pSECComponent->pSECPort[portIndex]; */
    /* SEC_OMX_DATABUFFER    *dataBuffer = &pSECComponent->secDataBuffer[portIndex]; */
    /* OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader; */
    SEC_OMX_DATA          *processData = &pSECComponent->processData[portIndex];

    processData->dataLen       = 0;
    processData->remainDataLen = 0;
    processData->usedDataLen   = 0;
    processData->nFlags        = 0;
    processData->timeStamp     = 0;

    return ret;
}

OMX_BOOL SEC_Preprocessor_InputData(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_BOOL               ret = OMX_FALSE;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_DATABUFFER    *inputUseBuffer = &pSECComponent->secDataBuffer[INPUT_PORT_INDEX];
    SEC_OMX_DATA          *inputData = &pSECComponent->processData[INPUT_PORT_INDEX];
    OMX_U32                copySize = 0;
    OMX_BYTE               checkInputStream = NULL;
    OMX_U32                checkInputStreamLen = 0;
    OMX_U32                checkedSize = 0;
    OMX_BOOL               flagEOS = OMX_FALSE;
    OMX_BOOL               flagEOF = OMX_FALSE;
    OMX_BOOL               previousFrameEOF = OMX_FALSE;

    if (inputUseBuffer->dataValid == OMX_TRUE) {
        checkInputStream = inputUseBuffer->bufferHeader->pBuffer + inputUseBuffer->usedDataLen;
        checkInputStreamLen = inputUseBuffer->remainDataLen;

        if ((inputUseBuffer->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) &&
            (pSECComponent->bUseFlagEOF == OMX_FALSE)) {
            pSECComponent->bUseFlagEOF = OMX_TRUE;
        }

        if (inputData->dataLen == 0) {
            previousFrameEOF = OMX_TRUE;
        } else {
            previousFrameEOF = OMX_FALSE;
        }
        if (pSECComponent->bUseFlagEOF == OMX_TRUE) {
            flagEOF = OMX_TRUE;
            checkedSize = checkInputStreamLen;
            if (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
                flagEOS = OMX_TRUE;
            }
        } else {
            SEC_OMX_BASEPORT *pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
            int width = pSECPort->portDefinition.format.video.nFrameWidth;
            int height = pSECPort->portDefinition.format.video.nFrameHeight;
            int oneFrameSize = 0;

            if (pSECPort->portDefinition.format.video.eColorFormat == OMX_COLOR_FormatYUV420SemiPlanar)
                oneFrameSize = (width * height * 3) / 2;
            else if (pSECPort->portDefinition.format.video.eColorFormat == OMX_COLOR_FormatYUV420Planar)
                oneFrameSize = (width * height * 3) / 2;
            else if (pSECPort->portDefinition.format.video.eColorFormat == OMX_COLOR_FormatYUV422Planar)
                oneFrameSize = width * height * 2;

            if (previousFrameEOF == OMX_TRUE) {
                if (checkInputStreamLen >= oneFrameSize) {
                    checkedSize = oneFrameSize;
                    flagEOF = OMX_TRUE;
                } else {
                    flagEOF = OMX_FALSE;
                }
            } else {
                if (checkInputStreamLen >= (oneFrameSize - inputData->dataLen)) {
                    checkedSize = oneFrameSize - inputData->dataLen;
                    flagEOF = OMX_TRUE;
                } else {
                    flagEOF = OMX_FALSE;
                }
            }

            if ((flagEOF == OMX_FALSE) && (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS)) {
                flagEOF = OMX_TRUE;
                flagEOS = OMX_TRUE;
            }
        }

        if (flagEOF == OMX_TRUE) {
            copySize = checkedSize;
            SEC_OSAL_Log(SEC_LOG_TRACE, "sec_checkInputFrame : OMX_TRUE");
        } else {
            copySize = checkInputStreamLen;
            SEC_OSAL_Log(SEC_LOG_TRACE, "sec_checkInputFrame : OMX_FALSE");
        }

        if (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS)
            pSECComponent->bSaveFlagEOS = OMX_TRUE;

        if (((inputData->allocSize) - (inputData->dataLen)) >= copySize) {
            SEC_OMX_BASEPORT *pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];

#ifndef S5PC110_ENCODE_IN_DATA_BUFFER
            if (copySize > 0) {
                SEC_OSAL_Memcpy(inputData->dataBuffer + inputData->dataLen, checkInputStream, copySize);
            }
#else
            if (pSECPort->portDefinition.format.video.eColorFormat == OMX_COLOR_FormatYUV420Planar) {
                if (flagEOF == OMX_TRUE) {
                    OMX_U32 width, height;

                    width = pSECPort->portDefinition.format.video.nFrameWidth;
                    height = pSECPort->portDefinition.format.video.nFrameHeight;

                    SEC_OSAL_Log(SEC_LOG_TRACE, "inputData->specificBufferHeader.YVirAddr : 0x%x", inputData->specificBufferHeader.YVirAddr);
                    SEC_OSAL_Log(SEC_LOG_TRACE, "inputData->specificBufferHeader.CVirAddr : 0x%x", inputData->specificBufferHeader.CVirAddr);

                    SEC_OSAL_Log(SEC_LOG_TRACE, "width:%d, height:%d, Ysize:%d", width, height, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
                    SEC_OSAL_Log(SEC_LOG_TRACE, "width:%d, height:%d, Csize:%d", width, height, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2)));

                    SEC_OSAL_Memcpy(inputData->specificBufferHeader.YVirAddr, checkInputStream, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
                    SEC_OSAL_Memcpy(inputData->specificBufferHeader.CVirAddr, checkInputStream + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)), ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2)));
                }
            }
#endif
            inputUseBuffer->dataLen -= copySize;
            inputUseBuffer->remainDataLen -= copySize;
            inputUseBuffer->usedDataLen += copySize;

            inputData->dataLen += copySize;
            inputData->remainDataLen += copySize;

            if (previousFrameEOF == OMX_TRUE) {
                inputData->timeStamp = inputUseBuffer->timeStamp;
                inputData->nFlags = inputUseBuffer->nFlags;
            }

            if (pSECComponent->bUseFlagEOF == OMX_TRUE) {
                if (pSECComponent->bSaveFlagEOS == OMX_TRUE) {
                    inputData->nFlags |= OMX_BUFFERFLAG_EOS;
                    flagEOF = OMX_TRUE;
                    pSECComponent->bSaveFlagEOS = OMX_FALSE;
                }
            } else {
                if ((checkedSize == checkInputStreamLen) && (pSECComponent->bSaveFlagEOS == OMX_TRUE)) {
                    inputData->nFlags |= OMX_BUFFERFLAG_EOS;
                    flagEOF = OMX_TRUE;
                    pSECComponent->bSaveFlagEOS = OMX_FALSE;
                } else {
                    inputData->nFlags = (inputUseBuffer->nFlags & (~OMX_BUFFERFLAG_EOS));
                }
            }
        } else {
            /*????????????????????????????????? Error ?????????????????????????????????*/
            SEC_DataReset(pOMXComponent, INPUT_PORT_INDEX);
            flagEOF = OMX_FALSE;
        }

        if (inputUseBuffer->remainDataLen == 0) {
#ifdef S5PC110_ENCODE_IN_DATA_BUFFER
            if(flagEOF == OMX_FALSE)
#endif
            SEC_InputBufferReturn(pOMXComponent);
        } else {
            inputUseBuffer->dataValid = OMX_TRUE;
        }
    }

    if (flagEOF == OMX_TRUE) {
        if (pSECComponent->checkTimeStamp.needSetStartTimeStamp == OMX_TRUE) {
            pSECComponent->checkTimeStamp.needCheckStartTimeStamp = OMX_TRUE;
            pSECComponent->checkTimeStamp.startTimeStamp = inputData->timeStamp;
            pSECComponent->checkTimeStamp.nStartFlags = inputData->nFlags;
            pSECComponent->checkTimeStamp.needSetStartTimeStamp = OMX_FALSE;
        }

        ret = OMX_TRUE;
    } else {
        ret = OMX_FALSE;
    }
    return ret;
}

OMX_BOOL SEC_Postprocess_OutputData(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_BOOL               ret = OMX_FALSE;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_DATABUFFER    *outputUseBuffer = &pSECComponent->secDataBuffer[OUTPUT_PORT_INDEX];
    SEC_OMX_DATA          *outputData = &pSECComponent->processData[OUTPUT_PORT_INDEX];
    OMX_U32                copySize = 0;

    if (outputUseBuffer->dataValid == OMX_TRUE) {
        if (pSECComponent->checkTimeStamp.needCheckStartTimeStamp == OMX_TRUE) {
            if (pSECComponent->checkTimeStamp.startTimeStamp == outputData->timeStamp){
                pSECComponent->checkTimeStamp.startTimeStamp = -19761123;
                pSECComponent->checkTimeStamp.nStartFlags = 0x0;
                pSECComponent->checkTimeStamp.needSetStartTimeStamp = OMX_FALSE;
                pSECComponent->checkTimeStamp.needCheckStartTimeStamp = OMX_FALSE;
            } else {
                SEC_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

                ret = OMX_TRUE;
                goto EXIT;
            }
        } else if (pSECComponent->checkTimeStamp.needSetStartTimeStamp == OMX_TRUE) {
            SEC_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

            ret = OMX_TRUE;
            goto EXIT;
        }

        if (outputData->remainDataLen <= (outputUseBuffer->allocSize - outputUseBuffer->dataLen)) {
            copySize = outputData->remainDataLen;
            if (copySize > 0)
                SEC_OSAL_Memcpy((outputUseBuffer->bufferHeader->pBuffer + outputUseBuffer->dataLen),
                    (outputData->dataBuffer + outputData->usedDataLen),
                     copySize);

            outputUseBuffer->dataLen += copySize;
            outputUseBuffer->remainDataLen += copySize;
            outputUseBuffer->nFlags = outputData->nFlags;
            outputUseBuffer->timeStamp = outputData->timeStamp;

            ret = OMX_TRUE;

            /* reset outputData */
            SEC_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

#ifdef ONE_FRAME_OUTPUT  /* only one frame output for Android */
            if ((outputUseBuffer->remainDataLen > 0) ||
                (outputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS))
                SEC_OutputBufferReturn(pOMXComponent);
#else
            if ((outputUseBuffer->remainDataLen > 0) ||
                ((outputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS) == OMX_BUFFERFLAG_EOS)) {
                SEC_OutputBufferReturn(pOMXComponent);
            } else {
                outputUseBuffer->dataValid = OMX_TRUE;
            }
#endif
        } else {
            SEC_OSAL_Log(SEC_LOG_ERROR, "output buffer is smaller than encoded data size Out Length");

            copySize = outputUseBuffer->allocSize - outputUseBuffer->dataLen;

            SEC_OSAL_Memcpy((outputUseBuffer->bufferHeader->pBuffer + outputUseBuffer->dataLen),
                    (outputData->dataBuffer + outputData->usedDataLen),
                     copySize);

            outputUseBuffer->dataLen += copySize;
            outputUseBuffer->remainDataLen += copySize;
            outputUseBuffer->nFlags = 0;
            outputUseBuffer->timeStamp = outputData->timeStamp;

            ret = OMX_FALSE;

            outputData->remainDataLen -= copySize;
            outputData->usedDataLen += copySize;

            SEC_OutputBufferReturn(pOMXComponent);
        }
    } else {
        ret = OMX_FALSE;
    }

EXIT:
    return ret;
}

OMX_ERRORTYPE SEC_OMX_BufferProcess(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_OMX_BASEPORT      *secInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    SEC_OMX_BASEPORT      *secOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    SEC_OMX_DATABUFFER    *inputUseBuffer = &pSECComponent->secDataBuffer[INPUT_PORT_INDEX];
    SEC_OMX_DATABUFFER    *outputUseBuffer = &pSECComponent->secDataBuffer[OUTPUT_PORT_INDEX];
    SEC_OMX_DATA          *inputData = &pSECComponent->processData[INPUT_PORT_INDEX];
    SEC_OMX_DATA          *outputData = &pSECComponent->processData[OUTPUT_PORT_INDEX];
    OMX_U32                copySize = 0;

    pSECComponent->remainOutputData = OMX_FALSE;
    pSECComponent->reInputData = OMX_FALSE;

    FunctionIn();

    while (!pSECComponent->bExitBufferProcessThread) {
        SEC_OSAL_SleepMillisec(0);

        if (((pSECComponent->currentState == OMX_StatePause) ||
            (pSECComponent->currentState == OMX_StateIdle) ||
            (pSECComponent->transientState == SEC_OMX_TransStateLoadedToIdle) ||
            (pSECComponent->transientState == SEC_OMX_TransStateExecutingToIdle)) &&
            (pSECComponent->transientState != SEC_OMX_TransStateIdleToLoaded)&&
            ((!CHECK_PORT_BEING_FLUSHED(secInputPort) && !CHECK_PORT_BEING_FLUSHED(secOutputPort)))) {
            SEC_OSAL_SignalReset(pSECComponent->pauseEvent);
            SEC_OSAL_SignalWait(pSECComponent->pauseEvent, DEF_MAX_WAIT_TIME);
        }

        while (SEC_Check_BufferProcess_State(pSECComponent) && !pSECComponent->bExitBufferProcessThread) {
            SEC_OSAL_SleepMillisec(0);

            SEC_OSAL_MutexLock(outputUseBuffer->bufferMutex);
            if ((outputUseBuffer->dataValid != OMX_TRUE) &&
                (!CHECK_PORT_BEING_FLUSHED(secOutputPort))) {
                SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                ret = SEC_OutputBufferGetQueue(pSECComponent);
                if ((ret == OMX_ErrorUndefined) ||
                    (secInputPort->portState != OMX_StateIdle) ||
                    (secOutputPort->portState != OMX_StateIdle)) {
                    break;
                }
            } else {
                SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
            }

            if (pSECComponent->remainOutputData == OMX_FALSE) {
                if (pSECComponent->reInputData == OMX_FALSE) {
                    SEC_OSAL_MutexLock(inputUseBuffer->bufferMutex);
                    if ((SEC_Preprocessor_InputData(pOMXComponent) == OMX_FALSE) &&
                        (!CHECK_PORT_BEING_FLUSHED(secInputPort))) {
                            SEC_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                            ret = SEC_InputBufferGetQueue(pSECComponent);
                            break;
                    }

                    SEC_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                }

                SEC_OSAL_MutexLock(inputUseBuffer->bufferMutex);
                SEC_OSAL_MutexLock(outputUseBuffer->bufferMutex);
                ret = pSECComponent->sec_mfc_bufferProcess(pOMXComponent, inputData, outputData);
#ifdef S5PC110_ENCODE_IN_DATA_BUFFER
                if (inputUseBuffer->remainDataLen == 0)
                    SEC_InputBufferReturn(pOMXComponent);
                else
                    inputUseBuffer->dataValid = OMX_TRUE;
#endif
                SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                SEC_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);

                if (ret == OMX_ErrorInputDataEncodeYet)
                    pSECComponent->reInputData = OMX_TRUE;
                else
                    pSECComponent->reInputData = OMX_FALSE;
            }

            SEC_OSAL_MutexLock(outputUseBuffer->bufferMutex);

            if (SEC_Postprocess_OutputData(pOMXComponent) == OMX_FALSE)
                pSECComponent->remainOutputData = OMX_TRUE;
            else
                pSECComponent->remainOutputData = OMX_FALSE;

            SEC_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
        }
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_VideoEncodeGetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nParamIndex,
    OMX_INOUT OMX_PTR     ComponentParameterStructure)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    if (pSECComponent->currentState == OMX_StateInvalid ) {
        ret = OMX_StateInvalid;
        goto EXIT;
    }

    if (ComponentParameterStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    switch (nParamIndex) {
    case OMX_IndexParamVideoInit:
    {
        OMX_PORT_PARAM_TYPE *portParam = (OMX_PORT_PARAM_TYPE *)ComponentParameterStructure;
        ret = SEC_OMX_Check_SizeVersion(portParam, sizeof(OMX_PORT_PARAM_TYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        portParam->nPorts           = pSECComponent->portParam.nPorts;
        portParam->nStartPortNumber = pSECComponent->portParam.nStartPortNumber;
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *portFormat = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)ComponentParameterStructure;
        OMX_U32                         portIndex = portFormat->nPortIndex;
        OMX_U32                         index    = portFormat->nIndex;
        SEC_OMX_BASEPORT               *pSECPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE   *portDefinition = NULL;
        OMX_U32                         supportFormatNum = 0;

        ret = SEC_OMX_Check_SizeVersion(portFormat, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if ((portIndex >= pSECComponent->portParam.nPorts)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }


        if (portIndex == INPUT_PORT_INDEX) {
            supportFormatNum = INPUT_PORT_SUPPORTFORMAT_NUM_MAX - 1;
            if (index > supportFormatNum) {
                ret = OMX_ErrorNoMore;
                goto EXIT;
            }

            pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
            portDefinition = &pSECPort->portDefinition;

            switch (index) {
            case supportFormat_1:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_COLOR_FormatYUV420Planar;
                portFormat->xFramerate           = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_2:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_COLOR_FormatYUV420SemiPlanar;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_3:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = SEC_OMX_COLOR_FormatNV12PhysicalAddress;
                portFormat->xFramerate           = portDefinition->format.video.xFramerate;
                break;
            }
        } else if (portIndex == OUTPUT_PORT_INDEX) {
            supportFormatNum = OUTPUT_PORT_SUPPORTFORMAT_NUM_MAX - 1;
            if (index > supportFormatNum) {
                ret = OMX_ErrorNoMore;
                goto EXIT;
            }

            pSECPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
            portDefinition = &pSECPort->portDefinition;

            portFormat->eCompressionFormat = portDefinition->format.video.eCompressionFormat;
            portFormat->eColorFormat       = portDefinition->format.video.eColorFormat;
            portFormat->xFramerate           = portDefinition->format.video.xFramerate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE  *videoRateControl = (OMX_VIDEO_PARAM_BITRATETYPE *)ComponentParameterStructure;
        OMX_U32                       portIndex = videoRateControl->nPortIndex;
        SEC_OMX_BASEPORT         *pSECPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pSECPort = &pSECComponent->pSECPort[portIndex];
            portDefinition = &pSECPort->portDefinition;

            videoRateControl->eControlRate = pSECPort->eControlRate;
            videoRateControl->nTargetBitrate = portDefinition->format.video.nBitrate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    default:
    {
        ret = SEC_OMX_GetParameter(hComponent, nParamIndex, ComponentParameterStructure);
    }
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}
OMX_ERRORTYPE SEC_OMX_VideoEncodeSetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nIndex,
    OMX_IN OMX_PTR        ComponentParameterStructure)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    if (pSECComponent->currentState == OMX_StateInvalid ) {
        ret = OMX_StateInvalid;
        goto EXIT;
    }

    if (ComponentParameterStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    switch (nIndex) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *portFormat = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)ComponentParameterStructure;
        OMX_U32                         portIndex = portFormat->nPortIndex;
        OMX_U32                         index    = portFormat->nIndex;
        SEC_OMX_BASEPORT               *pSECPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE   *portDefinition = NULL;
        OMX_U32                         supportFormatNum = 0;

        ret = SEC_OMX_Check_SizeVersion(portFormat, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if ((portIndex >= pSECComponent->portParam.nPorts)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pSECPort = &pSECComponent->pSECPort[portIndex];
            portDefinition = &pSECPort->portDefinition;

            portDefinition->format.video.eColorFormat       = portFormat->eColorFormat;
            portDefinition->format.video.eCompressionFormat = portFormat->eCompressionFormat;
            portDefinition->format.video.xFramerate         = portFormat->xFramerate;
        }
    }
        break;
    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE  *videoRateControl = (OMX_VIDEO_PARAM_BITRATETYPE *)ComponentParameterStructure;
        OMX_U32                       portIndex = videoRateControl->nPortIndex;
        SEC_OMX_BASEPORT             *pSECPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pSECPort = &pSECComponent->pSECPort[portIndex];
            portDefinition = &pSECPort->portDefinition;

            pSECPort->eControlRate = videoRateControl->eControlRate;
            portDefinition->format.video.nBitrate = videoRateControl->nTargetBitrate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *pPortDefinition =
                (OMX_PARAM_PORTDEFINITIONTYPE *)ComponentParameterStructure;
        ret = SEC_OMX_SetParameter(hComponent, nIndex, ComponentParameterStructure);
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pPortDefinition->nPortIndex == INPUT_PORT_INDEX) {
            SEC_OMX_BASEPORT *pSECInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
            OMX_U32 width = pSECInputPort->portDefinition.format.video.nFrameWidth;
            OMX_U32 height = pSECInputPort->portDefinition.format.video.nFrameHeight;
            SEC_OMX_BASEPORT *pSECOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
            pSECOutputPort->portDefinition.format.video.nFrameWidth = width;
            pSECOutputPort->portDefinition.format.video.nFrameHeight = height;
            pSECOutputPort->portDefinition.nBufferSize = (width * height * 3) / 2;
            SEC_OSAL_Log(SEC_LOG_TRACE, "pSECOutputPort->portDefinition.nBufferSize: %d",
                            pSECOutputPort->portDefinition.nBufferSize);
        }
    }
        break;
    default:
    {
        ret = SEC_OMX_SetParameter(hComponent, nIndex, ComponentParameterStructure);
    }
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_VideoEncodeComponentInit(OMX_IN OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    ret = SEC_OMX_BaseComponent_Constructor(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    ret = SEC_OMX_Port_Constructor(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        SEC_OMX_BaseComponent_Destructor(pOMXComponent);
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    pSECComponent->bSaveFlagEOS = OMX_FALSE;

    /* Input port */
    pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    pSECPort->portDefinition.nBufferCountActual = MAX_VIDEO_INPUTBUFFER_NUM;
    pSECPort->portDefinition.nBufferCountMin = MAX_VIDEO_INPUTBUFFER_NUM;
    pSECPort->portDefinition.nBufferSize = 0;
    pSECPort->portDefinition.eDomain = OMX_PortDomainVideo;

    pSECPort->portDefinition.format.video.cMIMEType = SEC_OSAL_Malloc(MAX_OMX_MIMETYPE_SIZE);
    SEC_OSAL_Strcpy(pSECPort->portDefinition.format.video.cMIMEType, "raw/video");
    pSECPort->portDefinition.format.video.pNativeRender = 0;
    pSECPort->portDefinition.format.video.bFlagErrorConcealment = OMX_FALSE;
    pSECPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;

    pSECPort->portDefinition.format.video.nFrameWidth = 0;
    pSECPort->portDefinition.format.video.nFrameHeight= 0;
    pSECPort->portDefinition.format.video.nStride = 0;
    pSECPort->portDefinition.format.video.nSliceHeight = 0;
    pSECPort->portDefinition.format.video.nBitrate = 64000;
    pSECPort->portDefinition.format.video.xFramerate = (15 << 16);
    pSECPort->portDefinition.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    pSECPort->portDefinition.format.video.pNativeWindow = NULL;

    /* Output port */
    pSECPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    pSECPort->portDefinition.nBufferCountActual = MAX_VIDEO_OUTPUTBUFFER_NUM;
    pSECPort->portDefinition.nBufferCountMin = MAX_VIDEO_OUTPUTBUFFER_NUM;
    pSECPort->portDefinition.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUFFER_SIZE;
    pSECPort->portDefinition.eDomain = OMX_PortDomainVideo;

    pSECPort->portDefinition.format.video.cMIMEType = SEC_OSAL_Malloc(MAX_OMX_MIMETYPE_SIZE);
    SEC_OSAL_Strcpy(pSECPort->portDefinition.format.video.cMIMEType, "raw/video");
    pSECPort->portDefinition.format.video.pNativeRender = 0;
    pSECPort->portDefinition.format.video.bFlagErrorConcealment = OMX_FALSE;
    pSECPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;

    pSECPort->portDefinition.format.video.nFrameWidth = 0;
    pSECPort->portDefinition.format.video.nFrameHeight= 0;
    pSECPort->portDefinition.format.video.nStride = 0;
    pSECPort->portDefinition.format.video.nSliceHeight = 0;
    pSECPort->portDefinition.format.video.nBitrate = 64000;
    pSECPort->portDefinition.format.video.xFramerate = (15 << 16);
    pSECPort->portDefinition.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    pSECPort->portDefinition.format.video.pNativeWindow = NULL;

    pOMXComponent->UseBuffer              = &SEC_OMX_UseBuffer;
    pOMXComponent->AllocateBuffer         = &SEC_OMX_AllocateBuffer;
    pOMXComponent->FreeBuffer             = &SEC_OMX_FreeBuffer;
    pOMXComponent->ComponentTunnelRequest = &SEC_OMX_ComponentTunnelRequest;

    pSECComponent->sec_AllocateTunnelBuffer = &SEC_OMX_AllocateTunnelBuffer;
    pSECComponent->sec_FreeTunnelBuffer     = &SEC_OMX_FreeTunnelBuffer;
    pSECComponent->sec_BufferProcess        = &SEC_OMX_BufferProcess;
    pSECComponent->sec_BufferReset          = &SEC_BufferReset;
    pSECComponent->sec_InputBufferReturn    = &SEC_InputBufferReturn;
    pSECComponent->sec_OutputBufferReturn   = &SEC_OutputBufferReturn;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_VideoEncodeComponentDeinit(OMX_IN OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;
    SEC_OMX_BASEPORT      *pSECPort = NULL;
    int                    i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    for(i = 0; i < ALL_PORT_NUM; i++) {
        pSECPort = &pSECComponent->pSECPort[i];
        SEC_OSAL_Free(pSECPort->portDefinition.format.video.cMIMEType);
        pSECPort->portDefinition.format.video.cMIMEType = NULL;
    }

    ret = SEC_OMX_Port_Destructor(pOMXComponent);

    ret = SEC_OMX_BaseComponent_Destructor(hComponent);

EXIT:
    FunctionOut();

    return ret;
}
