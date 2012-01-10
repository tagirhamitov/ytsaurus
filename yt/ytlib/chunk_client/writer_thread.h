﻿#pragma once
#include <ytlib/misc/lazy_ptr.h>
#include <ytlib/actions/action_queue.h>

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

/*!
 *  This thread is used for background operations in 
 *  #TRemoteChunkWriter, #NTableClient::TChunkWriter and 
 *  #NTableClient::TChunkSetReader
 */
extern TLazyPtr<TActionQueue> WriterThread;

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
