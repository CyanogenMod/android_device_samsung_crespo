/*
 *
 * Copyright 2011 Samsung Electronics S.LSI Co. LTD
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
 * @file       IYV12ColorConverter.cpp
 * @brief
 * @author     SeungBeom Kim (sbcrux.kim@samsung.com)
 * @version    1.0
 * @history
 *    2010.7.27 : Create
 */

#include <media/stagefright/openmax/OMX_IVCommon.h>
#include <IYV12ColorConverter.h>


int SEC_getDecoderOutputFormat()
{
    return (int)OMX_COLOR_FormatYUV420Planar;
}

int SEC_convertDecoderOutputToYV12(
    void* decoderBits, int decoderWidth, int decoderHeight,
    ARect decoderRect, void* dstBits)
{
    int ret = -1;
    return ret;
}

int SEC_getEncoderInputFormat()
{
    return (int)OMX_COLOR_FormatYUV420Planar;
}

int SEC_convertYV12ToEncoderInput(
    void* srcBits, int srcWidth, int srcHeight,
    int encoderWidth, int encoderHeight, ARect encoderRect,
    void* encoderBits)
{
    int ret = -1;
    return ret;
}

int SEC_getEncoderInputBufferInfo(
    int srcWidth, int srcHeight,
    int* encoderWidth, int* encoderHeight,
    ARect* encoderRect, int* encoderBufferSize)
{
    int ret = -1;
    return ret;
}

void getYV12ColorConverter(IYV12ColorConverter *converter)
{
    converter->getDecoderOutputFormat     = SEC_getDecoderOutputFormat;
    converter->convertDecoderOutputToYV12 = SEC_convertDecoderOutputToYV12;
    converter->getEncoderInputFormat      = SEC_getEncoderInputFormat;
    converter->convertDecoderOutputToYV12 = SEC_convertDecoderOutputToYV12;
    converter->getEncoderInputBufferInfo  = SEC_getEncoderInputBufferInfo;
    return;
}

