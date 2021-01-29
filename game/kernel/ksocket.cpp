/*!
 * @file ksocket.cpp
 * GOAL Socket connection to listener using DECI2/DSNET
 * DONE!
 */

#include "ksocket.h"
#include "kdsnetm.h"
#include "kprint.h"
#include "kboot.h"
#include "fileio.h"
#include "klisten.h"
/*!
 * Update GOAL message header after receiving and verify message is ok.
 * Return the size of the message in bytes (not including DECI or GOAL headers)
 * Return -1 on error.
 * The buffer parameter is unused.
 * DONE, removed call to FlushCache(0);
 */
u32 ReceiveToBuffer(char* buff) {
  (void)buff;

  // if we received less than the size of the message header, we either got nothing, or there was an
  // error
  if (protoBlock.last_receive_size < (int)sizeof(ListenerMessageHeader)) {
    return -1;
  }

  // FlushCache(0);
  ListenerMessageHeader* gbuff = protoBlock.receive_buffer;
  u32 msg_size = gbuff->msg_size;

  // check it's our protocol
  if (gbuff->deci2_header.proto == DECI2_PROTOCOL) {
    // null terminate
    ((u8*)gbuff)[sizeof(ListenerMessageHeader) + msg_size] = '\0';
    // copy stuff to block
    protoBlock.msg_kind = u32(gbuff->msg_kind);
    protoBlock.msg_id = gbuff->msg_id;
    // and mark message as received!
    protoBlock.last_receive_size = -1;
  } else {
    // not our protocol, something has gone wrong.
    MsgErr("dkernel: got a bad packet to goal proto (goal #x%lx bytes %d %d %d %ld %d)\n",
           (int64_t)protoBlock.receive_buffer, protoBlock.last_receive_size,
           u32(protoBlock.receive_buffer->msg_kind), protoBlock.receive_buffer->u6,
           protoBlock.receive_buffer->msg_id, msg_size);
    protoBlock.last_receive_size = -1;
    return -1;
  }
  return msg_size;
}

/*!
 * Do a DECI2 send and block until it is complete.
 * The message type is OUTPUT
 * DONE, EXACT
 */
s32 SendFromBuffer(char* buff, s32 size) {
  return SendFromBufferD(u16(ListenerMessageKind::MSG_OUTPUT), 0, buff, size);
}

/*!
 * Just prepare the Ack buffer, doesn't actually connect.
 * Must be called before attempting to use the socket connection.
 * DONE, EXACT
 */
void InitListenerConnect() {
  if (MasterDebug) {
    kstrcpy(AckBufArea + sizeof(ListenerMessageHeader), "ack");
  }
}

/*!
 * Does nothing.
 * DONE, EXACT
 */
void InitCheckListener() {}

/*!
 * Doesn't actually wait for a message, just checks if there's currently a message.
 * Doesn't actually send an ack either.
 * More accurate name would be "CheckForMessage"
 * Returns pointer to the message.
 * Updates MessCount to be equal to the size of the new message
 * DONE, EXACT
 */
Ptr<char> WaitForMessageAndAck() {
  if (!MasterDebug) {
    MessCount = -1;
  } else {
    MessCount = ReceiveToBuffer((char*)MessBufArea.c() + sizeof(ListenerMessageHeader));
  }

  if (MessCount < 0) {
    return Ptr<char>(0);
  }

  return MessBufArea.cast<char>() + sizeof(ListenerMessageHeader);
}

/*!
 * Doesn't close anything, just print a closed message.
 * DONE, EXACT
 */
void CloseListener() {
  Msg(6, "dconnect: closed socket at kernel side\n");
}