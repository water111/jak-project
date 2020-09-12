/*!
 * @file klisten.cpp
 * Implementation of the Listener protocol
 * Done
 */

#include <cstring>
#include "klisten.h"
#include "kboot.h"
#include "kprint.h"
#include "kdsnetm.h"
#include "ksocket.h"
#include "kmalloc.h"
#include "klink.h"
#include "kscheme.h"
#include "common/symbols.h"

Ptr<Symbol> ListenerLinkBlock;
Ptr<Symbol> ListenerFunction;
Ptr<Symbol> kernel_dispatcher;
Ptr<Symbol> kernel_packages;
Ptr<u32> print_column;
u32 ListenerStatus;

void klisten_init_globals() {
  ListenerLinkBlock.offset = 0;
  ListenerFunction.offset = 0;
  kernel_dispatcher.offset = 0;
  kernel_packages.offset = 0;
  print_column.offset = 0;
  ListenerStatus = 0;
}

/*!
 * Initialize the Listener by setting up symbols shared between GOAL and C for the listener.
 * Also adds "kernel" to the kernel_packages list.
 * There was an "ACK" message sent here, but this is removed because we don't need it.
 */
void InitListener() {
  ListenerLinkBlock = intern_from_c("*listener-link-block*");
  ListenerFunction = intern_from_c("*listener-function*");
  kernel_dispatcher = intern_from_c("kernel-dispatcher");
  kernel_packages = intern_from_c("*kernel-packages*");
  print_column = intern_from_c("*print-column*").cast<u32>();
  ListenerLinkBlock->value = s7.offset;
  ListenerFunction->value = s7.offset;

  kernel_packages->value =
      new_pair(s7.offset + FIX_SYM_GLOBAL_HEAP, *((s7 + FIX_SYM_PAIR_TYPE).cast<u32>()),
               make_string_from_c("kernel"), kernel_packages->value);
  //  if(MasterDebug) {
  //    SendFromBufferD(MSG_ACK, 0, AckBufArea + sizeof(GoalMessageHeader), 0);
  //  }
}

/*!
 * Flush pending messages.  If debugging, will send to compiler, otherwise to stdout.
 */
void ClearPending() {
  if (!MasterDebug) {
    // if we aren't debugging print the print buffer to stdout.
    if (PrintPending.offset != 0) {
      auto size = strlen(PrintBufArea.cast<char>().c() + sizeof(GoalMessageHeader));
      if (size > 0) {
        printf("%s", PrintBufArea.cast<char>().c() + sizeof(GoalMessageHeader));
      }
    }
  } else {
    if (ListenerStatus) {
      if (OutputPending.offset != 0) {
        Ptr<char> msg = OutputBufArea.cast<char>() + sizeof(GoalMessageHeader);
        auto size = strlen(msg.c());
        // note - if size is ever greater than 2^16 this will cause an issue.
        SendFromBuffer(msg.c(), size);
        clear_output();
      }

      if (PrintPending.offset != 0) {
        char* msg = PrintBufArea.cast<char>().c() + sizeof(GoalMessageHeader);
        auto size = strlen(msg);
        while (size > 0) {
          // sends larger than 64 kB are broken by the GoalProtoBuffer thing, so they are split
          auto send_size = size;
          if (send_size > 64000) {
            send_size = 64000;
          }
          SendFromBufferD(2, 0, msg, send_size);
          size -= send_size;
          msg += send_size;
        }
        clear_print();
      }
    }
  }
}

/*!
 * Send an "ack" message. The original game had the AckBufArea which stores "ack", but did not
 * calculate the length correctly, so the message would not actually contain the "ack" text.
 * The "ack" text is unimportant, as the compiler can recognize the messages as ACK due to the
 * ListenerMessageKind::MSG_ACK field.  Both the type and msg_id fields are sent, which is enough
 * for it to work.
 */
void SendAck() {
  if (MasterDebug) {
    SendFromBufferD(u16(ListenerMessageKind::MSG_ACK), protoBlock.msg_id,
                    AckBufArea + sizeof(GoalMessageHeader),
                    strlen(AckBufArea + sizeof(GoalMessageHeader)));
  }
}

/*!
 * Handle an incoming listener message
 */
void ProcessListenerMessage(Ptr<char> msg) {
  // flag that the listener is connected!
  ListenerStatus = 1;
  switch (protoBlock.msg_kind) {
    case LTT_MSG_POKE:
      // just flush any pending stuff.
      ClearPending();
      break;
    case LTT_MSG_INSEPCT:
      inspect_object(atoi(msg.c()));
      ClearPending();
      break;
    case LTT_MSG_PRINT:
      print_object(atoi(msg.c()));
      ClearPending();
      break;
    case LTT_MSG_PRINT_SYMBOLS:
      printf("[ERROR] unsupported message kind LTT_MSG_PRINT_SYMBOLS (NYI)\n");
      break;
    case LTT_MSG_RESET:
      MasterExit = 1;
      if (protoBlock.msg_id == UINT64_MAX) {
        MasterExit = 2;
      }
      break;
    case LTT_MSG_CODE: {
      auto buffer = kmalloc(kdebugheap, MessCount, 0, "listener-link-block");
      memcpy(buffer.c(), msg.c(), MessCount);
      ListenerLinkBlock->value = buffer.offset + 4;
      // note - this will stash the linked code in the top level and free it.
      // it will then be used-after-free, but this is OK because nobody else will allocate.
      // the kernel dispatcher should immediately execute the listener function to avoid this
      // getting squashed.

      // this setup allows listener function execution to clean up after itself.
      ListenerFunction->value =
          link_and_exec(buffer, "*listener*", 0, kdebugheap, LINK_FLAG_FORCE_DEBUG).offset;
      fprintf(stderr, "ListenerFunction is now 0x%x\n", ListenerFunction->value);
      return;  // don't ack yet, this will happen after the function runs.
    } break;
    default:
      MsgErr("dkernel: unknown message error: <%d> of %d bytes\n", protoBlock.msg_kind, MessCount);
      break;
  }
  SendAck();
}
