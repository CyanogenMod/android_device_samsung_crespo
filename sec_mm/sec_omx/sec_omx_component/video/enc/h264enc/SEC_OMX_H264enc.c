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
 * @file        SEC_OMX_H264enc.c
 * @brief
 * @author      SeungBeom Kim (sbcrux.kim@samsung.com)
 * @version     1.0
 * @history
 *   2010.7.15 : Create
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SEC_OMX_Macros.h"
#include "SEC_OMX_Basecomponent.h"
#include "SEC_OMX_Baseport.h"
#include "SEC_OMX_Venc.h"
#include "library_register.h"
#include "SEC_OMX_H264enc.h"
#include "SsbSipMfcApi.h"

#undef  SEC_LOG_TAG
#define SEC_LOG_TAG    "SEC_H264_ENC"
#define SEC_LOG_OFF
#include "SEC_OSAL_Log.h"


/* H.264 Encoder Supported Levels & profiles */
SEC_OMX_VIDEO_PROFILELEVEL supportedAVCProfileLevels[] ={
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32},
    {OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4},

    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1b},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel11},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel12},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel13},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel2},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel21},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel22},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel3},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel31},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel32},
    {OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel4},

    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1b},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel11},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel12},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel13},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel2},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel21},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel22},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel3},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel31},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel32},
    {OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel4}};


OMX_U32 OMXAVCProfileToProfileIDC(OMX_VIDEO_AVCPROFILETYPE profile)
{
    OMX_U32 ret = 0; //default OMX_VIDEO_AVCProfileMain

    if (profile == OMX_VIDEO_AVCProfileMain)
        ret = 0;
    else if (profile == OMX_VIDEO_AVCProfileHigh)
        ret = 1;
    else if (profile == OMX_VIDEO_AVCProfileBaseline)
        ret = 2;

    return ret;
}

OMX_U32 OMXAVCLevelToLevelIDC(OMX_VIDEO_AVCLEVELTYPE level)
{
    OMX_U32 ret = 40; //default OMX_VIDEO_AVCLevel4

    if (level == OMX_VIDEO_AVCLevel1)
        ret = 10;
    else if (level == OMX_VIDEO_AVCLevel1b)
        ret = 9;
    else if (level == OMX_VIDEO_AVCLevel11)
        ret = 11;
    else if (level == OMX_VIDEO_AVCLevel12)
        ret = 12;
    else if (level == OMX_VIDEO_AVCLevel13)
        ret = 13;
    else if (level == OMX_VIDEO_AVCLevel2)
        ret = 20;
    else if (level == OMX_VIDEO_AVCLevel21)
        ret = 21;
    else if (level == OMX_VIDEO_AVCLevel22)
        ret = 22;
    else if (level == OMX_VIDEO_AVCLevel3)
        ret = 30;
    else if (level == OMX_VIDEO_AVCLevel31)
        ret = 31;
    else if (level == OMX_VIDEO_AVCLevel32)
        ret = 32;
    else if (level == OMX_VIDEO_AVCLevel4)
        ret = 40;

    return ret;
}

OMX_U8 *FindDelimiter(OMX_U8 *pBuffer, OMX_U32 size)
{
    int i;

    for (i = 0; i < size - 3; i++) {
        if ((pBuffer[i] == 0x00)   &&
            (pBuffer[i+1] == 0x00) &&
            (pBuffer[i+2] == 0x00) &&
            (pBuffer[i+3] == 0x01))
            return (pBuffer + i);
    }

    return NULL;
}

void H264PrintParams(SSBSIP_MFC_ENC_H264_PARAM h264Arg)
{
    SEC_OSAL_Log(SEC_LOG_TRACE, "SourceWidth             : %d\n", h264Arg.SourceWidth);
    SEC_OSAL_Log(SEC_LOG_TRACE, "SourceHeight            : %d\n", h264Arg.SourceHeight);
    SEC_OSAL_Log(SEC_LOG_TRACE, "ProfileIDC              : %d\n", h264Arg.ProfileIDC);
    SEC_OSAL_Log(SEC_LOG_TRACE, "LevelIDC                : %d\n", h264Arg.LevelIDC);
    SEC_OSAL_Log(SEC_LOG_TRACE, "IDRPeriod               : %d\n", h264Arg.IDRPeriod);
    SEC_OSAL_Log(SEC_LOG_TRACE, "NumberReferenceFrames   : %d\n", h264Arg.NumberReferenceFrames);
    SEC_OSAL_Log(SEC_LOG_TRACE, "NumberRefForPframes     : %d\n", h264Arg.NumberRefForPframes);
    SEC_OSAL_Log(SEC_LOG_TRACE, "SliceMode               : %d\n", h264Arg.SliceMode);
    SEC_OSAL_Log(SEC_LOG_TRACE, "SliceArgument           : %d\n", h264Arg.SliceArgument);
    SEC_OSAL_Log(SEC_LOG_TRACE, "NumberBFrames           : %d\n", h264Arg.NumberBFrames);
    SEC_OSAL_Log(SEC_LOG_TRACE, "LoopFilterDisable       : %d\n", h264Arg.LoopFilterDisable);
    SEC_OSAL_Log(SEC_LOG_TRACE, "LoopFilterAlphaC0Offset : %d\n", h264Arg.LoopFilterAlphaC0Offset);
    SEC_OSAL_Log(SEC_LOG_TRACE, "LoopFilterBetaOffset    : %d\n", h264Arg.LoopFilterBetaOffset);
    SEC_OSAL_Log(SEC_LOG_TRACE, "SymbolMode              : %d\n", h264Arg.SymbolMode);
    SEC_OSAL_Log(SEC_LOG_TRACE, "PictureInterlace        : %d\n", h264Arg.PictureInterlace);
    SEC_OSAL_Log(SEC_LOG_TRACE, "Transform8x8Mode        : %d\n", h264Arg.Transform8x8Mode);
    SEC_OSAL_Log(SEC_LOG_TRACE, "RandomIntraMBRefresh    : %d\n", h264Arg.RandomIntraMBRefresh);
    SEC_OSAL_Log(SEC_LOG_TRACE, "PadControlOn            : %d\n", h264Arg.PadControlOn);
    SEC_OSAL_Log(SEC_LOG_TRACE, "LumaPadVal              : %d\n", h264Arg.LumaPadVal);
    SEC_OSAL_Log(SEC_LOG_TRACE, "CbPadVal                : %d\n", h264Arg.CbPadVal);
    SEC_OSAL_Log(SEC_LOG_TRACE, "CrPadVal                : %d\n", h264Arg.CrPadVal);
    SEC_OSAL_Log(SEC_LOG_TRACE, "EnableFRMRateControl    : %d\n", h264Arg.EnableFRMRateControl);
    SEC_OSAL_Log(SEC_LOG_TRACE, "EnableMBRateControl     : %d\n", h264Arg.EnableMBRateControl);
    SEC_OSAL_Log(SEC_LOG_TRACE, "FrameRate               : %d\n", h264Arg.FrameRate);
    SEC_OSAL_Log(SEC_LOG_TRACE, "Bitrate                 : %d\n", h264Arg.Bitrate);
    SEC_OSAL_Log(SEC_LOG_TRACE, "FrameQp                 : %d\n", h264Arg.FrameQp);
    SEC_OSAL_Log(SEC_LOG_TRACE, "QSCodeMax               : %d\n", h264Arg.QSCodeMax);
    SEC_OSAL_Log(SEC_LOG_TRACE, "QSCodeMin               : %d\n", h264Arg.QSCodeMin);
    SEC_OSAL_Log(SEC_LOG_TRACE, "CBRPeriodRf             : %d\n", h264Arg.CBRPeriodRf);
    SEC_OSAL_Log(SEC_LOG_TRACE, "DarkDisable             : %d\n", h264Arg.DarkDisable);
    SEC_OSAL_Log(SEC_LOG_TRACE, "SmoothDisable           : %d\n", h264Arg.SmoothDisable);
    SEC_OSAL_Log(SEC_LOG_TRACE, "StaticDisable           : %d\n", h264Arg.StaticDisable);
    SEC_OSAL_Log(SEC_LOG_TRACE, "ActivityDisable         : %d\n", h264Arg.ActivityDisable);
}

void Set_H264ENC_Param(SSBSIP_MFC_ENC_H264_PARAM *pH264Arg, SEC_OMX_BASECOMPONENT *pSECComponent)
{
    SEC_OMX_BASEPORT          *pSECInputPort = NULL;
    SEC_OMX_BASEPORT          *pSECOutputPort = NULL;
    SEC_H264ENC_HANDLE        *pH264Enc = NULL;

    pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    pSECInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    pSECOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];

    pH264Arg->codecType    = H264_ENC;
    pH264Arg->SourceWidth  = pSECOutputPort->portDefinition.format.video.nFrameWidth;
    pH264Arg->SourceHeight = pSECOutputPort->portDefinition.format.video.nFrameHeight;
    pH264Arg->IDRPeriod    = pH264Enc->AVCComponent[OUTPUT_PORT_INDEX].nPFrames + 1;
    pH264Arg->SliceMode    = 0;
    pH264Arg->RandomIntraMBRefresh = 0;
    pH264Arg->EnableFRMRateControl = 1;        // 0: Disable, 1: Frame level RC
    pH264Arg->Bitrate      = pSECOutputPort->portDefinition.format.video.nBitrate;
    pH264Arg->FrameQp      = 20;
    pH264Arg->FrameQp_P    = 20;
    pH264Arg->QSCodeMax    = 30;
    pH264Arg->QSCodeMin    = 10;
    pH264Arg->CBRPeriodRf  = 100;
    pH264Arg->PadControlOn = 0;             // 0: disable, 1: enable
    pH264Arg->LumaPadVal   = 0;
    pH264Arg->CbPadVal     = 0;
    pH264Arg->CrPadVal     = 0;

    pH264Arg->ProfileIDC   = OMXAVCProfileToProfileIDC(pH264Enc->AVCComponent[OUTPUT_PORT_INDEX].eProfile); //0;  //(OMX_VIDEO_AVCProfileMain)
    pH264Arg->LevelIDC     = OMXAVCLevelToLevelIDC(pH264Enc->AVCComponent[OUTPUT_PORT_INDEX].eLevel);       //40; //(OMX_VIDEO_AVCLevel4)
    pH264Arg->FrameQp_B    = 20;
    pH264Arg->FrameRate    = (pSECInputPort->portDefinition.format.video.xFramerate) >> 16;
    pH264Arg->SliceArgument = 0;          // Slice mb/byte size number
    pH264Arg->NumberBFrames = 0;            // 0 ~ 2
    pH264Arg->NumberReferenceFrames = 1;
    pH264Arg->NumberRefForPframes   = 1;
    pH264Arg->LoopFilterDisable     = 1;    // 1: Loop Filter Disable, 0: Filter Enable
    pH264Arg->LoopFilterAlphaC0Offset = 0;
    pH264Arg->LoopFilterBetaOffset    = 0;
    pH264Arg->SymbolMode       = 0;         // 0: CAVLC, 1: CABAC
    pH264Arg->PictureInterlace = 0;
    pH264Arg->Transform8x8Mode = 0;         // 0: 4x4, 1: allow 8x8
    pH264Arg->EnableMBRateControl = 0;        // 0: Disable, 1:MB level RC
    pH264Arg->DarkDisable     = 1;
    pH264Arg->SmoothDisable   = 1;
    pH264Arg->StaticDisable   = 1;
    pH264Arg->ActivityDisable = 1;

    SEC_OSAL_Log(SEC_LOG_TRACE, "pSECPort->eControlRate: 0x%x", pSECOutputPort->eControlRate);
    switch (pSECOutputPort->eControlRate) {
    case OMX_Video_ControlRateDisable:
        /* TBD */
        break;
    case OMX_Video_ControlRateVariable:
        /* TBD */
        break;
    default:
        break;
    }

    H264PrintParams(*pH264Arg);
}

OMX_ERRORTYPE SEC_MFC_H264Enc_GetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nParamIndex,
    OMX_INOUT OMX_PTR     pComponentParameterStructure)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;

    FunctionIn();

    if (hComponent == NULL || pComponentParameterStructure == NULL) {
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

    switch (nParamIndex) {
    case OMX_IndexParamVideoAvc:
    {
        OMX_VIDEO_PARAM_AVCTYPE *pDstAVCComponent = (OMX_VIDEO_PARAM_AVCTYPE *)pComponentParameterStructure;
        OMX_VIDEO_PARAM_AVCTYPE *pSrcAVCComponent = NULL;
        SEC_H264ENC_HANDLE      *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pDstAVCComponent, sizeof(OMX_VIDEO_PARAM_AVCTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pDstAVCComponent->nPortIndex >= ALL_PORT_NUM) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
        pSrcAVCComponent = &pH264Enc->AVCComponent[pDstAVCComponent->nPortIndex];

        SEC_OSAL_Memcpy(pDstAVCComponent, pSrcAVCComponent, sizeof(OMX_VIDEO_PARAM_AVCTYPE));
    }
        break;
    case OMX_IndexParamStandardComponentRole:
    {
        OMX_PARAM_COMPONENTROLETYPE *pComponentRole = (OMX_PARAM_COMPONENTROLETYPE *)pComponentParameterStructure;
        ret = SEC_OMX_Check_SizeVersion(pComponentRole, sizeof(OMX_PARAM_COMPONENTROLETYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        SEC_OSAL_Strcpy((char *)pComponentRole->cRole, SEC_OMX_COMPOMENT_H264_ENC_ROLE);
    }
        break;
    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *pDstProfileLevel = (OMX_VIDEO_PARAM_PROFILELEVELTYPE*)pComponentParameterStructure;
        SEC_OMX_VIDEO_PROFILELEVEL *pProfileLevel = NULL;
        OMX_U32 maxProfileLevelNum = 0;

        ret = SEC_OMX_Check_SizeVersion(pDstProfileLevel, sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pDstProfileLevel->nPortIndex >= ALL_PORT_NUM) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pProfileLevel = supportedAVCProfileLevels;
        maxProfileLevelNum = sizeof(supportedAVCProfileLevels) / sizeof(SEC_OMX_VIDEO_PROFILELEVEL);

        if (pDstProfileLevel->nProfileIndex >= maxProfileLevelNum) {
            ret = OMX_ErrorNoMore;
            goto EXIT;
        }

        pProfileLevel += pDstProfileLevel->nProfileIndex;
        pDstProfileLevel->eProfile = pProfileLevel->profile;
        pDstProfileLevel->eLevel = pProfileLevel->level;
    }
        break;
    case OMX_IndexParamVideoProfileLevelCurrent:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *pDstProfileLevel = (OMX_VIDEO_PARAM_PROFILELEVELTYPE*)pComponentParameterStructure;
        OMX_VIDEO_PARAM_AVCTYPE *pSrcAVCComponent = NULL;
        SEC_H264ENC_HANDLE        *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pDstProfileLevel, sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pDstProfileLevel->nPortIndex >= ALL_PORT_NUM) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
        pSrcAVCComponent = &pH264Enc->AVCComponent[pDstProfileLevel->nPortIndex];

        pDstProfileLevel->eProfile = pSrcAVCComponent->eProfile;
        pDstProfileLevel->eLevel = pSrcAVCComponent->eLevel;
    }
        break;
    case OMX_IndexParamVideoErrorCorrection:
    {
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *pDstErrorCorrectionType = (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *)pComponentParameterStructure;
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *pSrcErrorCorrectionType = NULL;
        SEC_H264ENC_HANDLE        *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pDstErrorCorrectionType, sizeof(OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pDstErrorCorrectionType->nPortIndex != OUTPUT_PORT_INDEX) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
        pSrcErrorCorrectionType = &pH264Enc->errorCorrectionType[OUTPUT_PORT_INDEX];

        pDstErrorCorrectionType->bEnableHEC = pSrcErrorCorrectionType->bEnableHEC;
        pDstErrorCorrectionType->bEnableResync = pSrcErrorCorrectionType->bEnableResync;
        pDstErrorCorrectionType->nResynchMarkerSpacing = pSrcErrorCorrectionType->nResynchMarkerSpacing;
        pDstErrorCorrectionType->bEnableDataPartitioning = pSrcErrorCorrectionType->bEnableDataPartitioning;
        pDstErrorCorrectionType->bEnableRVLC = pSrcErrorCorrectionType->bEnableRVLC;
    }
        break;
    default:
        ret = SEC_OMX_VideoEncodeGetParameter(hComponent, nParamIndex, pComponentParameterStructure);
        break;
    }
EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_MFC_H264Enc_SetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nIndex,
    OMX_IN OMX_PTR        pComponentParameterStructure)
{
    OMX_ERRORTYPE           ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;

    FunctionIn();

    if (hComponent == NULL || pComponentParameterStructure == NULL) {
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

    switch (nIndex) {
    case OMX_IndexParamVideoAvc:
    {
        OMX_VIDEO_PARAM_AVCTYPE *pDstAVCComponent = NULL;
        OMX_VIDEO_PARAM_AVCTYPE *pSrcAVCComponent = (OMX_VIDEO_PARAM_AVCTYPE *)pComponentParameterStructure;
        SEC_H264ENC_HANDLE      *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pSrcAVCComponent, sizeof(OMX_VIDEO_PARAM_AVCTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pSrcAVCComponent->nPortIndex >= ALL_PORT_NUM) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
        pDstAVCComponent = &pH264Enc->AVCComponent[pSrcAVCComponent->nPortIndex];

        SEC_OSAL_Memcpy(pDstAVCComponent, pSrcAVCComponent, sizeof(OMX_VIDEO_PARAM_AVCTYPE));
    }
        break;
    case OMX_IndexParamStandardComponentRole:
    {
        OMX_PARAM_COMPONENTROLETYPE *pComponentRole = (OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure;

        ret = SEC_OMX_Check_SizeVersion(pComponentRole, sizeof(OMX_PARAM_COMPONENTROLETYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if ((pSECComponent->currentState != OMX_StateLoaded) && (pSECComponent->currentState != OMX_StateWaitForResources)) {
            ret = OMX_ErrorIncorrectStateOperation;
            goto EXIT;
        }

        if (!SEC_OSAL_Strcmp((char*)pComponentRole->cRole, SEC_OMX_COMPOMENT_H264_ENC_ROLE)) {
            pSECComponent->pSECPort[OUTPUT_PORT_INDEX].portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
        } else {
            ret = OMX_ErrorBadParameter;
            goto EXIT;
        }
    }
        break;
    case OMX_IndexParamVideoProfileLevelCurrent:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *pSrcProfileLevel = (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)pComponentParameterStructure;
        OMX_VIDEO_PARAM_AVCTYPE *pDstAVCComponent = NULL;
        SEC_H264ENC_HANDLE        *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pSrcProfileLevel, sizeof(OMX_VIDEO_PARAM_PROFILELEVELTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pSrcProfileLevel->nPortIndex >= ALL_PORT_NUM) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;

        pDstAVCComponent = &pH264Enc->AVCComponent[pSrcProfileLevel->nPortIndex];
        pDstAVCComponent->eProfile = pSrcProfileLevel->eProfile;
        pDstAVCComponent->eLevel = pSrcProfileLevel->eLevel;
    }
        break;
    case OMX_IndexParamVideoErrorCorrection:
    {
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *pSrcErrorCorrectionType = (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *)pComponentParameterStructure;
        OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *pDstErrorCorrectionType = NULL;
        SEC_H264ENC_HANDLE        *pH264Enc = NULL;

        ret = SEC_OMX_Check_SizeVersion(pSrcErrorCorrectionType, sizeof(OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if (pSrcErrorCorrectionType->nPortIndex != OUTPUT_PORT_INDEX) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }

        pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
        pDstErrorCorrectionType = &pH264Enc->errorCorrectionType[OUTPUT_PORT_INDEX];

        pDstErrorCorrectionType->bEnableHEC = pSrcErrorCorrectionType->bEnableHEC;
        pDstErrorCorrectionType->bEnableResync = pSrcErrorCorrectionType->bEnableResync;
        pDstErrorCorrectionType->nResynchMarkerSpacing = pSrcErrorCorrectionType->nResynchMarkerSpacing;
        pDstErrorCorrectionType->bEnableDataPartitioning = pSrcErrorCorrectionType->bEnableDataPartitioning;
        pDstErrorCorrectionType->bEnableRVLC = pSrcErrorCorrectionType->bEnableRVLC;
    }
        break;
    default:
        ret = SEC_OMX_VideoEncodeSetParameter(hComponent, nIndex, pComponentParameterStructure);
        break;
    }
EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_MFC_H264Enc_SetConfig(
    OMX_HANDLETYPE hComponent,
    OMX_INDEXTYPE nIndex,
    OMX_PTR pComponentConfigStructure)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE     *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT *pSECComponent = NULL;

    FunctionIn();

    if (hComponent == NULL || pComponentConfigStructure == NULL) {
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
    if (pSECComponent->currentState == OMX_StateInvalid) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

    switch (nIndex) {
    default:
        ret = SEC_OMX_SetConfig(hComponent, nIndex, pComponentConfigStructure);
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_MFC_H264Enc_ComponentRoleEnum(OMX_HANDLETYPE hComponent, OMX_U8 *cRole, OMX_U32 nIndex)
{
    OMX_ERRORTYPE            ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE       *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT   *pSECComponent = NULL;

    FunctionIn();

    if ((hComponent == NULL) || (cRole == NULL)) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    if (nIndex == (MAX_COMPONENT_ROLE_NUM-1)) {
        SEC_OSAL_Strcpy((char *)cRole, SEC_OMX_COMPOMENT_H264_ENC_ROLE);
        ret = OMX_ErrorNone;
    } else {
        ret = OMX_ErrorNoMore;
    }

EXIT:
    FunctionOut();

    return ret;
}

/* MFC Init */
OMX_ERRORTYPE SEC_MFC_H264Enc_Init(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE              ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT     *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_H264ENC_HANDLE        *pH264Enc = NULL;
    OMX_PTR                    hMFCHandle = NULL;
    OMX_S32                    returnCodec = 0;

    FunctionIn();

    pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    pH264Enc->hMFCH264Handle.bConfiguredMFC = OMX_FALSE;
    pSECComponent->bUseFlagEOF = OMX_FALSE;
    pSECComponent->bSaveFlagEOS = OMX_FALSE;

    /* MFC(Multi Function Codec) encoder and CMM(Codec Memory Management) driver open */
    hMFCHandle = (OMX_PTR)SsbSipMfcEncOpen();
    if (hMFCHandle == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    pH264Enc->hMFCH264Handle.hMFCHandle = hMFCHandle;

    Set_H264ENC_Param(&(pH264Enc->hMFCH264Handle.mfcVideoAvc), pSECComponent);

    returnCodec = SsbSipMfcEncInit(hMFCHandle, &(pH264Enc->hMFCH264Handle.mfcVideoAvc));
    if (returnCodec != MFC_RET_OK) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    /* Allocate encoder's input buffer */
    returnCodec = SsbSipMfcEncGetInBuf(hMFCHandle, &(pH264Enc->hMFCH264Handle.inputInfo));
    if (returnCodec != MFC_RET_OK) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    SEC_OSAL_Log(SEC_LOG_TRACE, "pH264Enc->hMFCH264Handle.inputInfo.YVirAddr : 0x%x", pH264Enc->hMFCH264Handle.inputInfo.YVirAddr);
    SEC_OSAL_Log(SEC_LOG_TRACE, "pH264Enc->hMFCH264Handle.inputInfo.CVirAddr : 0x%x", pH264Enc->hMFCH264Handle.inputInfo.CVirAddr);

    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.YPhyAddr = pH264Enc->hMFCH264Handle.inputInfo.YPhyAddr;
    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.CPhyAddr = pH264Enc->hMFCH264Handle.inputInfo.CPhyAddr;
    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.YVirAddr = pH264Enc->hMFCH264Handle.inputInfo.YVirAddr;
    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.CVirAddr = pH264Enc->hMFCH264Handle.inputInfo.CVirAddr;
    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.YSize = pH264Enc->hMFCH264Handle.inputInfo.YSize;
    pSECComponent->processData[INPUT_PORT_INDEX].specificBufferHeader.CSize = pH264Enc->hMFCH264Handle.inputInfo.CSize;

    SEC_OSAL_Memset(pSECComponent->timeStamp, -19771003, sizeof(OMX_TICKS) * MAX_TIMESTAMP);
    SEC_OSAL_Memset(pSECComponent->nFlags, 0, sizeof(OMX_U32) * MAX_FLAGS);
    pH264Enc->hMFCH264Handle.indexTimestamp = 0;

EXIT:
    FunctionOut();

    return ret;
}

/* MFC Terminate */
OMX_ERRORTYPE SEC_MFC_H264Enc_Terminate(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_H264ENC_HANDLE    *pH264Enc = NULL;
    OMX_PTR                hMFCHandle = NULL;

    FunctionIn();

    pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    hMFCHandle = pH264Enc->hMFCH264Handle.hMFCHandle;

    if (hMFCHandle != NULL) {
        SsbSipMfcEncClose(hMFCHandle);
        hMFCHandle = pH264Enc->hMFCH264Handle.hMFCHandle = NULL;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_MFC_H264_Encode(OMX_COMPONENTTYPE *pOMXComponent, SEC_OMX_DATA *pInputData, SEC_OMX_DATA *pOutputData)
{
    OMX_ERRORTYPE              ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT     *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_H264ENC_HANDLE        *pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    SSBSIP_MFC_ENC_INPUT_INFO *pInputInfo = &pH264Enc->hMFCH264Handle.inputInfo;
    SSBSIP_MFC_ENC_OUTPUT_INFO outputInfo;
    SEC_OMX_BASEPORT          *pSECPort = NULL;
    MFC_ENC_ADDR_INFO          addrInfo;
    OMX_U32                    oneFrameSize = pInputData->dataLen;
    OMX_S32                    returnCodec = 0;

    FunctionIn();

    if (pH264Enc->hMFCH264Handle.bConfiguredMFC == OMX_FALSE) {
        returnCodec = SsbSipMfcEncGetOutBuf(pH264Enc->hMFCH264Handle.hMFCHandle, &outputInfo);
        if (returnCodec != MFC_RET_OK)
        {
            SEC_OSAL_Log(SEC_LOG_TRACE, "%s - SsbSipMfcEncGetOutBuf Failed\n", __func__);
            ret = OMX_ErrorUndefined;
            goto EXIT;
        } else {
            char *p = NULL;
            int iSpsSize = 0;
            int iPpsSize = 0;

            p = FindDelimiter((OMX_U8 *)outputInfo.StrmVirAddr + 4, outputInfo.headerSize - 4);

            iSpsSize = (unsigned int)p - (unsigned int)outputInfo.StrmVirAddr;
            pH264Enc->hMFCH264Handle.headerData.pHeaderSPS = (OMX_PTR)outputInfo.StrmVirAddr;
            pH264Enc->hMFCH264Handle.headerData.SPSLen = iSpsSize;

            iPpsSize = outputInfo.headerSize - iSpsSize;
            pH264Enc->hMFCH264Handle.headerData.pHeaderPPS = outputInfo.StrmVirAddr + iSpsSize;
            pH264Enc->hMFCH264Handle.headerData.PPSLen = iPpsSize;
        }

        /* SEC_OSAL_Memcpy((void*)(pOutputData->dataBuffer), (const void*)(outputInfo.StrmVirAddr), outputInfo.headerSize); */
        pOutputData->dataBuffer = outputInfo.StrmVirAddr;
        pOutputData->allocSize = outputInfo.headerSize;
        pOutputData->dataLen = outputInfo.headerSize;
        pOutputData->timeStamp = pInputData->timeStamp;
        pOutputData->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
        pOutputData->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

        pH264Enc->hMFCH264Handle.bConfiguredMFC = OMX_TRUE;

        ret = OMX_ErrorNone;
        goto EXIT;
    }

    if ((pInputData->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) &&
        (pSECComponent->bUseFlagEOF == OMX_FALSE)) {
        pSECComponent->bUseFlagEOF = OMX_TRUE;
    }

    pSECComponent->timeStamp[pH264Enc->hMFCH264Handle.indexTimestamp] = pInputData->timeStamp;
    pSECComponent->nFlags[pH264Enc->hMFCH264Handle.indexTimestamp] = pInputData->nFlags;
    SsbSipMfcEncSetConfig(pH264Enc->hMFCH264Handle.hMFCHandle, MFC_ENC_SETCONF_FRAME_TAG, &(pH264Enc->hMFCH264Handle.indexTimestamp));
    pH264Enc->hMFCH264Handle.indexTimestamp++;
    if (pH264Enc->hMFCH264Handle.indexTimestamp >= MAX_TIMESTAMP)
        pH264Enc->hMFCH264Handle.indexTimestamp = 0;

    if (oneFrameSize <= 0) {
        pOutputData->timeStamp = pInputData->timeStamp;
        pOutputData->nFlags = pInputData->nFlags;

        ret = OMX_ErrorNone;
        goto EXIT;
    }

    pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    if (pSECPort->portDefinition.format.video.eColorFormat == SEC_OMX_COLOR_FormatNV12PhysicalAddress) {
#define USE_FIMC_FRAME_BUFFER
#ifdef USE_FIMC_FRAME_BUFFER
        SEC_OSAL_Memcpy(&addrInfo.pAddrY, pInputData->dataBuffer, sizeof(addrInfo.pAddrY));
        SEC_OSAL_Memcpy(&addrInfo.pAddrC, pInputData->dataBuffer + sizeof(addrInfo.pAddrY), sizeof(addrInfo.pAddrC));
        pInputInfo->YPhyAddr = addrInfo.pAddrY;
        pInputInfo->CPhyAddr = addrInfo.pAddrC;
        ret = SsbSipMfcEncSetInBuf(pH264Enc->hMFCH264Handle.hMFCHandle, pInputInfo);
        if (ret != MFC_RET_OK) {
            SEC_OSAL_Log(SEC_LOG_TRACE, "Error : SsbSipMfcEncSetInBuf() \n");
            ret = OMX_ErrorUndefined;
            goto EXIT;
        }
#else
        OMX_U32 width, height;

        width = pSECPort->portDefinition.format.video.nFrameWidth;
        height = pSECPort->portDefinition.format.video.nFrameHeight;

        SEC_OSAL_Memcpy(pInputInfo->YVirAddr, pInputData->dataBuffer, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
        SEC_OSAL_Memcpy(pInputInfo->CVirAddr, pInputData->dataBuffer + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)), ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2)));
#endif
    }

    returnCodec = SsbSipMfcEncExe(pH264Enc->hMFCH264Handle.hMFCHandle);
    if (returnCodec == MFC_RET_OK) {
        OMX_S32 indexTimestamp = 0;

        returnCodec = SsbSipMfcEncGetOutBuf(pH264Enc->hMFCH264Handle.hMFCHandle, &outputInfo);
        if ((SsbSipMfcEncGetConfig(pH264Enc->hMFCH264Handle.hMFCHandle, MFC_ENC_GETCONF_FRAME_TAG, &indexTimestamp) != MFC_RET_OK) ||
            (((indexTimestamp < 0) || (indexTimestamp > MAX_TIMESTAMP)))){
            pOutputData->timeStamp = pInputData->timeStamp;
            pOutputData->nFlags = pInputData->nFlags;
        } else {
            pOutputData->timeStamp = pSECComponent->timeStamp[indexTimestamp];
            pOutputData->nFlags = pSECComponent->nFlags[indexTimestamp];
        }

        if (returnCodec == MFC_RET_OK) {
            /** Fill Output Buffer **/
            pOutputData->dataBuffer = outputInfo.StrmVirAddr;
            pOutputData->allocSize = outputInfo.dataSize;
            pOutputData->dataLen = outputInfo.dataSize;
            pOutputData->usedDataLen = 0;

            pOutputData->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
            if (outputInfo.frameType == MFC_FRAME_TYPE_I_FRAME)
                    pOutputData->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

            SEC_OSAL_Log(SEC_LOG_TRACE, "MFC Encode OK!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

            ret = OMX_ErrorNone;
        }
    }

    if (returnCodec != MFC_RET_OK) {
        SEC_OSAL_Log(SEC_LOG_ERROR, "In %s : SsbSipMfcEncExe OR SsbSipMfcEncGetOutBuf Failed!!!\n", __func__);
        ret = OMX_ErrorUndefined;
    }

EXIT:
    FunctionOut();

    return ret;
}

/* MFC Encode */
OMX_ERRORTYPE SEC_MFC_H264Enc_bufferProcess(OMX_COMPONENTTYPE *pOMXComponent, SEC_OMX_DATA *pInputData, SEC_OMX_DATA *pOutputData)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    SEC_OMX_BASECOMPONENT   *pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    SEC_H264ENC_HANDLE      *pH264Enc = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    SEC_OMX_BASEPORT        *pSECInputPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    SEC_OMX_BASEPORT        *pSECOutputPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    OMX_BOOL                 endOfFrame = OMX_FALSE;
    OMX_BOOL                 flagEOS = OMX_FALSE;

    FunctionIn();

    if ((!CHECK_PORT_ENABLED(pSECInputPort)) || (!CHECK_PORT_ENABLED(pSECOutputPort)) ||
            (!CHECK_PORT_POPULATED(pSECInputPort)) || (!CHECK_PORT_POPULATED(pSECOutputPort))) {
        ret = OMX_ErrorNone;
        goto EXIT;
    }
    if (OMX_FALSE == SEC_Check_BufferProcess_State(pSECComponent)) {
        ret = OMX_ErrorNone;
        goto EXIT;
    }

    ret = SEC_MFC_H264_Encode(pOMXComponent, pInputData, pOutputData);
    if (ret != OMX_ErrorNone) {
        pSECComponent->pCallbacks->EventHandler((OMX_HANDLETYPE)pOMXComponent,
                                        pSECComponent->callbackData,
                                        OMX_EventError, ret, 0, NULL);
    } else {
        pInputData->usedDataLen += pInputData->dataLen;
        pInputData->remainDataLen = pInputData->dataLen - pInputData->usedDataLen;
        pInputData->dataLen -= pInputData->usedDataLen;
        pInputData->usedDataLen = 0;

        /* pOutputData->usedDataLen = 0; */
        pOutputData->remainDataLen = pOutputData->dataLen - pOutputData->usedDataLen;
    }

EXIT:
    FunctionOut();

    return ret;
}

OSCL_EXPORT_REF OMX_ERRORTYPE SEC_OMX_ComponentInit(OMX_HANDLETYPE hComponent, OMX_STRING componentName)
{
    OMX_ERRORTYPE            ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE       *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT   *pSECComponent = NULL;
    SEC_OMX_BASEPORT        *pSECPort = NULL;
    SEC_H264ENC_HANDLE      *pH264Enc = NULL;
    int i = 0;

    FunctionIn();

    if ((hComponent == NULL) || (componentName == NULL)) {
        ret = OMX_ErrorBadParameter;
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_ErrorBadParameter, Line:%d", __LINE__);
        goto EXIT;
    }
    if (SEC_OSAL_Strcmp(SEC_OMX_COMPOMENT_H264_ENC, componentName) != 0) {
        ret = OMX_ErrorBadParameter;
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_ErrorBadParameter, componentName:%s, Line:%d", componentName, __LINE__);
        goto EXIT;
    }

    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = SEC_OMX_VideoEncodeComponentInit(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    pSECComponent->codecType = HW_VIDEO_CODEC;

    pSECComponent->componentName = (OMX_STRING)SEC_OSAL_Malloc(MAX_OMX_COMPONENT_NAME_SIZE);
    if (pSECComponent->componentName == NULL) {
        SEC_OMX_VideoEncodeComponentDeinit(pOMXComponent);
        ret = OMX_ErrorInsufficientResources;
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_ErrorInsufficientResources, Line:%d", __LINE__);
        goto EXIT;
    }
    SEC_OSAL_Memset(pSECComponent->componentName, 0, MAX_OMX_COMPONENT_NAME_SIZE);

    pH264Enc = SEC_OSAL_Malloc(sizeof(SEC_H264ENC_HANDLE));
    if (pH264Enc == NULL) {
        SEC_OMX_VideoEncodeComponentDeinit(pOMXComponent);
        ret = OMX_ErrorInsufficientResources;
        SEC_OSAL_Log(SEC_LOG_ERROR, "OMX_ErrorInsufficientResources, Line:%d", __LINE__);
        goto EXIT;
    }
    SEC_OSAL_Memset(pH264Enc, 0, sizeof(SEC_H264ENC_HANDLE));
    pSECComponent->hCodecHandle = (OMX_HANDLETYPE)pH264Enc;

    SEC_OSAL_Strcpy(pSECComponent->componentName, SEC_OMX_COMPOMENT_H264_ENC);
    /* Set componentVersion */
    pSECComponent->componentVersion.s.nVersionMajor = VERSIONMAJOR_NUMBER;
    pSECComponent->componentVersion.s.nVersionMinor = VERSIONMINOR_NUMBER;
    pSECComponent->componentVersion.s.nRevision     = REVISION_NUMBER;
    pSECComponent->componentVersion.s.nStep         = STEP_NUMBER;
    /* Set specVersion */
    pSECComponent->specVersion.s.nVersionMajor = VERSIONMAJOR_NUMBER;
    pSECComponent->specVersion.s.nVersionMinor = VERSIONMINOR_NUMBER;
    pSECComponent->specVersion.s.nRevision     = REVISION_NUMBER;
    pSECComponent->specVersion.s.nStep         = STEP_NUMBER;

    /* Android CapabilityFlags */
    pSECComponent->capabilityFlags.iIsOMXComponentMultiThreaded                   = OMX_TRUE;
    pSECComponent->capabilityFlags.iOMXComponentSupportsExternalInputBufferAlloc  = OMX_TRUE;
    pSECComponent->capabilityFlags.iOMXComponentSupportsExternalOutputBufferAlloc = OMX_TRUE;
    pSECComponent->capabilityFlags.iOMXComponentSupportsMovableInputBuffers       = OMX_FALSE;
    pSECComponent->capabilityFlags.iOMXComponentSupportsPartialFrames             = OMX_FALSE;
    pSECComponent->capabilityFlags.iOMXComponentUsesNALStartCodes                 = OMX_TRUE;
    pSECComponent->capabilityFlags.iOMXComponentCanHandleIncompleteFrames         = OMX_TRUE;
    pSECComponent->capabilityFlags.iOMXComponentUsesFullAVCFrames                 = OMX_TRUE;

    /* Input port */
    pSECPort = &pSECComponent->pSECPort[INPUT_PORT_INDEX];
    pSECPort->portDefinition.format.video.nFrameWidth = DEFAULT_FRAME_WIDTH;
    pSECPort->portDefinition.format.video.nFrameHeight= DEFAULT_FRAME_HEIGHT;
    pSECPort->portDefinition.format.video.nStride = 0; /*DEFAULT_FRAME_WIDTH;*/
    pSECPort->portDefinition.nBufferSize = DEFAULT_VIDEO_INPUT_BUFFER_SIZE;
    pSECPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    SEC_OSAL_Memset(pSECPort->portDefinition.format.video.cMIMEType, 0, MAX_OMX_MIMETYPE_SIZE);
    SEC_OSAL_Strcpy(pSECPort->portDefinition.format.video.cMIMEType, "raw/video");
    pSECPort->portDefinition.format.video.eColorFormat = SEC_OMX_COLOR_FormatNV12PhysicalAddress;
    pSECPort->portDefinition.bEnabled = OMX_TRUE;

    /* Output port */
    pSECPort = &pSECComponent->pSECPort[OUTPUT_PORT_INDEX];
    pSECPort->portDefinition.format.video.nFrameWidth = DEFAULT_FRAME_WIDTH;
    pSECPort->portDefinition.format.video.nFrameHeight= DEFAULT_FRAME_HEIGHT;
    pSECPort->portDefinition.format.video.nStride = 0; /*DEFAULT_FRAME_WIDTH;*/
    pSECPort->portDefinition.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUFFER_SIZE;
    pSECPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    SEC_OSAL_Memset(pSECPort->portDefinition.format.video.cMIMEType, 0, MAX_OMX_MIMETYPE_SIZE);
    SEC_OSAL_Strcpy(pSECPort->portDefinition.format.video.cMIMEType, "video/avc");
    pSECPort->portDefinition.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    pSECPort->portDefinition.bEnabled = OMX_TRUE;

    for(i = 0; i < ALL_PORT_NUM; i++) {
        INIT_SET_SIZE_VERSION(&pH264Enc->AVCComponent[i], OMX_VIDEO_PARAM_AVCTYPE);
        pH264Enc->AVCComponent[i].nPortIndex = i;
        pH264Enc->AVCComponent[i].eProfile   = OMX_VIDEO_AVCProfileBaseline;
        pH264Enc->AVCComponent[i].eLevel     = OMX_VIDEO_AVCLevel4;
    }

    pOMXComponent->GetParameter      = &SEC_MFC_H264Enc_GetParameter;
    pOMXComponent->SetParameter      = &SEC_MFC_H264Enc_SetParameter;
    pOMXComponent->SetConfig         = &SEC_MFC_H264Enc_SetConfig;
    pOMXComponent->ComponentRoleEnum = &SEC_MFC_H264Enc_ComponentRoleEnum;
    pOMXComponent->ComponentDeInit   = &SEC_OMX_ComponentDeinit;

    pSECComponent->sec_mfc_componentInit      = &SEC_MFC_H264Enc_Init;
    pSECComponent->sec_mfc_componentTerminate = &SEC_MFC_H264Enc_Terminate;
    pSECComponent->sec_mfc_bufferProcess      = &SEC_MFC_H264Enc_bufferProcess;
    pSECComponent->sec_checkInputFrame        = NULL;

    pSECComponent->currentState = OMX_StateLoaded;

    ret = OMX_ErrorNone;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE SEC_OMX_ComponentDeinit(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE            ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE       *pOMXComponent = NULL;
    SEC_OMX_BASECOMPONENT   *pSECComponent = NULL;
    SEC_H264ENC_HANDLE      *pH264Dec = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    pSECComponent = (SEC_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    SEC_OSAL_Free(pSECComponent->componentName);
    pSECComponent->componentName = NULL;

    pH264Dec = (SEC_H264ENC_HANDLE *)pSECComponent->hCodecHandle;
    if (pH264Dec != NULL) {
        SEC_OSAL_Free(pH264Dec);
        pH264Dec = pSECComponent->hCodecHandle = NULL;
    }

    ret = SEC_OMX_VideoEncodeComponentDeinit(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    ret = OMX_ErrorNone;

EXIT:
    FunctionOut();

    return ret;
}
