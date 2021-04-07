/***************************************************************************/ /**
 * @file
 * @brief This file contains definitions for a CPC based NCP interface to the OpenThread stack.
 *******************************************************************************
 * # License
 * <b>Copyright 2019 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#include "ncp_cpc.hpp"

#include <stdio.h>

#include <openthread/ncp.h>
#include <openthread/platform/logging.h>
#include <openthread/platform/misc.h>

#include "openthread-core-config.h"
#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "common/instance.hpp"
#include "common/new.hpp"

#if OPENTHREAD_CONFIG_NCP_CPC_ENABLE

namespace ot {
namespace Ncp {

#if OPENTHREAD_ENABLE_NCP_VENDOR_HOOK == 0

static OT_DEFINE_ALIGNED_VAR(sNcpRaw, sizeof(NcpCPC), uint64_t);

extern "C" void otNcpInit(otInstance *aInstance)
{
    NcpCPC * ncpCPC  = nullptr;
    Instance *instance = static_cast<Instance *>(aInstance);

    ncpCPC = new (&sNcpRaw) NcpCPC(instance);

    if (ncpCPC == nullptr || ncpCPC != NcpBase::GetNcpInstance())
    {
        OT_ASSERT(false);
    }
}

#endif // OPENTHREAD_ENABLE_NCP_VENDOR_HOOK == 0

NcpCPC::NcpCPC(Instance *aInstance)
    : NcpBase(aInstance)
    , mCpcSendTask(*aInstance, SendToCPC, this)
{
    sl_status_t status = sl_cpc_open_user_endpoint(&mUserEp, 
                                                   SL_CPC_ENDPOINT_USER_ID_0, 
                                                   0, 
                                                   1);

    OT_ASSERT(status == SL_STATUS_ALREADY_EXISTS || status == SL_STATUS_OK);

    status = sl_cpc_set_endpoint_option(&mUserEp, 
                                        SL_CPC_ENDPOINT_ON_IFRAME_WRITE_COMPLETED, 
                                        reinterpret_cast<void *>(HandleCPCSendDone));

    OT_ASSERT(status == SL_STATUS_OK);

    mTxFrameBuffer.SetFrameAddedCallback(HandleFrameAddedToNcpBuffer, this);
}

void NcpCPC::HandleFrameAddedToNcpBuffer(void *                   aContext,
                                          Spinel::Buffer::FrameTag aTag,
                                          Spinel::Buffer::Priority aPriority,
                                          Spinel::Buffer *         aBuffer)
{
    OT_UNUSED_VARIABLE(aBuffer);
    OT_UNUSED_VARIABLE(aTag);
    OT_UNUSED_VARIABLE(aPriority);

    static_cast<NcpCPC *>(aContext)->HandleFrameAddedToNcpBuffer();
}

void NcpCPC::HandleFrameAddedToNcpBuffer(void)
{
    mCpcSendTask.Post();
}

void NcpCPC::SendToCPC(Tasklet &aTasklet)
{
    OT_UNUSED_VARIABLE(aTasklet);
    static_cast<NcpCPC *>(GetNcpInstance())->SendToCPC();
}

// may need to be updated to handle sleepy devices. Refer to NcpUart::EncodeAndSendToUart
void NcpCPC::SendToCPC(void)
{
    Spinel::Buffer &txFrameBuffer = mTxFrameBuffer;
    uint8_t *buffer;
    
    IgnoreError(txFrameBuffer.OutFrameBegin());
    uint8_t bufferLen = txFrameBuffer.OutFrameGetLength();
    buffer = (uint8_t *)malloc(bufferLen*sizeof(uint8_t));

    txFrameBuffer.OutFrameRead(bufferLen,buffer);
    
    // Just catch reset reason for now, need a better solution
    if(*buffer == 0x80 && *(buffer+1) == 0x06 && *(buffer+2) == 0x00 && *(buffer+3) == 0x72)
    {
        IgnoreError(txFrameBuffer.OutFrameRemove());
        return;
    }
    
    sl_cpc_write(&mUserEp, buffer, bufferLen, 0, NULL);

    IgnoreError(txFrameBuffer.OutFrameRemove());
}

void NcpCPC::HandleCPCSendDone(sl_cpc_user_endpoint_id_t endpoint_id,
                                void *buffer,
                                void *arg,
                                sl_status_t status)
{
    OT_UNUSED_VARIABLE(endpoint_id);
    OT_UNUSED_VARIABLE(arg);
    OT_UNUSED_VARIABLE(status);

    free(buffer);
}

extern "C" void efr32CpcProcess(void)
{
    NcpCPC *ncpCPC = static_cast<NcpCPC *>(NcpBase::GetNcpInstance());

    if (ncpCPC != nullptr)
    {
        ncpCPC->HandleCPCReceiveDone();
    }
}

void NcpCPC::HandleCPCReceiveDone(void)
{
    sl_status_t status;
    void *data;
    uint16_t dataLength;

    status = sl_cpc_read(&mUserEp,
                         &data,
                         &dataLength,
                         0,
                         SL_CPC_FLAG_NO_BLOCK); // In bare-metal read is always
                                                // non-blocking, but with rtos
                                                // since this function is called
                                                // in the cpc task, it must not
                                                // block.

    if (status != SL_STATUS_OK) {
        return;
    }

    super_t::HandleReceive(static_cast<uint8_t *>(data), dataLength);

    status = sl_cpc_free_rx_buffer(data);
    OT_ASSERT(status == SL_STATUS_OK);
}

extern "C" void otPlatUartReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
    OT_UNUSED_VARIABLE(aBuf);
    OT_UNUSED_VARIABLE(aBufLength);
}

extern "C" void otPlatUartSendDone(void)
{
}

} // namespace Ncp
} // namespace ot

#endif // OPENTHREAD_CONFIG_NCP_CPC_ENABLE
