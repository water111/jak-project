/*!
 * @file dmac.cpp
 * DMAC implementation for the "PS2 virtual machine".
 * Not meant to work as a full DMAC emulator, just enough to inspect DMA packets.
 */

#include "dmac.h"
#include "vm.h"
#include "game/runtime.h"
#include "game/kernel/kmalloc.h"
#include "common/log/log.h"

namespace VM {

Ptr<DmaCommonRegisters> dmac;
Ptr<DmaChannelRegisters> dmac_ch[10];

void dmac_init_globals() {
  dmac = kmalloc(kdebugheap, sizeof(DmaCommonRegisters), KMALLOC_ALIGN_16 | KMALLOC_MEMSET, "dmac")
             .cast<DmaCommonRegisters>();

  for (int i = 0; i < 10; ++i) {
    dmac_ch[i] =
        kmalloc(kdebugheap, sizeof(DmaChannelRegisters), KMALLOC_ALIGN_16 | KMALLOC_MEMSET, "dmach")
            .cast<DmaChannelRegisters>();
  }
}

}  // namespace VM

#undef from_ee_mem
