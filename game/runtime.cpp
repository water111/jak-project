/*!
 * @file runtime.cpp
 * Setup and launcher for the runtime.
 */

#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#elif _WIN32
#include <io.h>
#include <third-party/mman/mman.h>
#include <Windows.h>
#elif __APPLE__
#include <unistd.h>
#include <sys/mman.h>
#endif

#include <chrono>
#include <cstring>
#include <thread>

#include "common/log/log.h"
#include "runtime.h"
#include "system/SystemThread.h"
#include "sce/libcdvd_ee.h"
#include "sce/deci2.h"
#include "sce/sif_ee.h"
#include "sce/iop.h"
#include "game/system/Deci2Server.h"

#include "game/kernel/fileio.h"
#include "game/kernel/kboot.h"
#include "game/kernel/klink.h"
#include "game/kernel/kscheme.h"
#include "game/kernel/kdsnetm.h"
#include "game/kernel/klisten.h"
#include "game/kernel/kmemcard.h"
#include "game/kernel/kprint.h"
#include "game/kernel/kdgo.h"

#include "game/system/iop_thread.h"

#include "game/overlord/dma.h"
#include "game/overlord/iso.h"
#include "game/overlord/fake_iso.h"
#include "game/overlord/iso_queue.h"
#include "game/overlord/ramdisk.h"
#include "game/overlord/iso_cd.h"
#include "game/overlord/overlord.h"
#include "game/overlord/srpc.h"
#include "game/overlord/stream.h"

#include "common/goal_constants.h"
#include "common/cross_os_debug/xdbg.h"

u8* g_ee_main_mem = nullptr;

namespace {

int g_argc = 0;
char** g_argv = nullptr;

/*!
 * SystemThread function for running the DECI2 communication with the GOAL compiler.
 */

void deci2_runner(SystemThreadInterface& iface) {
  // callback function so the server knows when to give up and shutdown
  std::function<bool()> shutdown_callback = [&]() { return iface.get_want_exit(); };

  // create and register server
  Deci2Server server(shutdown_callback);
  ee::LIBRARY_sceDeci2_register(&server);

  // now its ok to continue with initialization
  iface.initialization_complete();

  // in our own thread, wait for the EE to register the first protocol driver
  lg::debug("[DECI2] Waiting for EE to register protos");
  server.wait_for_protos_ready();
  // then allow the server to accept connections
  if (!server.init()) {
    assert(false);
  }

  lg::debug("[DECI2] Waiting for listener...");
  // lg::debug("[DECI2] Waiting for listener..."); --> disabled temporarily, some weird race
  // condition?
  bool saw_listener = false;
  while (!iface.get_want_exit()) {
    if (server.check_for_listener()) {
      if (!saw_listener) {
        lg::debug("[DECI2] Connected!");
      }
      saw_listener = true;
      // we have a listener, run!
      server.run();
    } else {
      // no connection yet.  Do a sleep so we don't spam checking the listener.
      std::this_thread::sleep_for(std::chrono::microseconds(50000));
    }
  }
}

// EE System

/*!
 * SystemThread Function for the EE (PS2 Main CPU)
 */
void ee_runner(SystemThreadInterface& iface) {
// Allocate Main RAM. Must have execute enabled.
#ifdef __LINUX__
  {
    if (EE_MEM_LOW_MAP) {
      g_ee_main_mem =
          (u8*)mmap((void*)0x10000000, EE_MAIN_MEM_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_32BIT | MAP_PRIVATE | MAP_POPULATE | 0, 0);
    } else {
      g_ee_main_mem =
          (u8*)mmap((void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    }
#elif __WIN32
  {
    if (EE_MEM_LOW_MAP) {
      g_ee_main_mem =
          (u8*)mmap((void*)0x10000000, EE_MAIN_MEM_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_32BIT | MAP_PRIVATE | MAP_POPULATE | 0, 0);
    } else {
      g_ee_main_mem =
          (u8*)mmap((void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    }
#elif __APPLE__
  {
    g_ee_main_mem =
        (u8*)mmap((void*)EE_MAIN_MEM_MAP, EE_MAIN_MEM_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE,
                  MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
  }
#endif

    if (g_ee_main_mem == (u8*)(-1)) {
      lg::debug("Failed to initialize main memory! {}", strerror(errno));
      iface.initialization_complete();
      return;
    }

    lg::debug("Main memory mapped at 0x{:016x}", (u64)(g_ee_main_mem));
    lg::debug("Main memory size 0x{:x} bytes ({:.3f} MB)", EE_MAIN_MEM_SIZE,
              (double)EE_MAIN_MEM_SIZE / (1 << 20));

    lg::debug("[EE] Initialization complete!");
    iface.initialization_complete();

    lg::debug("[EE] Run!");
    memset((void*)g_ee_main_mem, 0, EE_MAIN_MEM_SIZE);

    // prevent access to the first 1 MB of memory.
    // On the PS2 this is the kernel and can't be accessed either.
    // this may not work well on systems with a page size > 1 MB.
    mprotect((void*)g_ee_main_mem, EE_MAIN_MEM_LOW_PROTECT, PROT_NONE);
    fileio_init_globals();
    kboot_init_globals();
    kdgo_init_globals();
    kdsnetm_init_globals();
    klink_init_globals();

    kmachine_init_globals();
    kscheme_init_globals();
    kmalloc_init_globals();

    klisten_init_globals();
    kmemcard_init_globals();
    kprint_init_globals();

    // Added for OpenGOAL's debugger
    xdbg::allow_debugging();

    goal_main(g_argc, g_argv);
    lg::debug("[EE] Done!");

    //  // kill the IOP todo
    iop::LIBRARY_kill();

    munmap(g_ee_main_mem, EE_MAIN_MEM_SIZE);

    // after main returns, trigger a shutdown.
    iface.trigger_shutdown();
  }

  /*!
   * SystemThread function for running the IOP (separate I/O Processor)
   */
  void iop_runner(SystemThreadInterface & iface) {
    IOP iop;
    lg::debug("[IOP] Restart!");
    iop.reset_allocator();
    ee::LIBRARY_sceSif_register(&iop);
    iop::LIBRARY_register(&iop);

    // todo!
    dma_init_globals();
    iso_init_globals();
    fake_iso_init_globals();
    // iso_api
    iso_cd_init_globals();
    iso_queue_init_globals();
    // isocommon
    // overlord
    ramdisk_init_globals();
    // sbank
    // soundcommon
    srpc_init_globals();
    // ssound
    stream_init_globals();

    iface.initialization_complete();

    lg::debug("[IOP] Wait for OVERLORD to start...");
    iop.wait_for_overlord_start_cmd();
    if (iop.status == IOP_OVERLORD_INIT) {
      lg::debug("[IOP] Run!");
    } else {
      lg::debug("[IOP] Shutdown!");
      return;
    }

    iop.reset_allocator();

    // init

    start_overlord(iop.overlord_argc, iop.overlord_argv);  // todo!

    // unblock the EE, the overlord is set up!
    iop.signal_overlord_init_finish();

    // IOP Kernel loop
    while (!iface.get_want_exit() && !iop.want_exit) {
      // the IOP kernel just runs at full blast, so we only run the IOP when the EE is waiting on
      // the IOP. Each time the EE is waiting on the IOP, it will run an iteration of the IOP
      // kernel.
      iop.wait_run_iop();
      iop.kernel.dispatchAll();
    }

    // stop all threads in the iop kernel.
    // if the threads are not stopped nicely, we will deadlock on trying to destroy the kernel's
    // condition variables.
    iop.kernel.shutdown();
  }
}  // namespace

/*!
 * Main function to launch the runtime.
 * Arguments are currently ignored.
 */
u32 exec_runtime(int argc, char** argv) {
  g_argc = argc;
  g_argv = argv;

  // step 1: sce library prep
  iop::LIBRARY_INIT();
  ee::LIBRARY_INIT_sceCd();
  ee::LIBRARY_INIT_sceDeci2();
  ee::LIBRARY_INIT_sceSif();

  // step 2: system prep
  SystemThreadManager tm;
  auto& deci_thread = tm.create_thread("DMP");
  auto& iop_thread = tm.create_thread("IOP");
  auto& ee_thread = tm.create_thread("EE");

  // step 3: start the EE!
  iop_thread.start(iop_runner);
  ee_thread.start(ee_runner);
  deci_thread.start(deci2_runner);

  // step 4: wait for EE to signal a shutdown, which will cause the DECI thread to join.
  deci_thread.join();
  // DECI has been killed, shutdown!

  // to be extra sure
  tm.shutdown();

  // join and exit
  tm.join();
  lg::info("GOAL Runtime Shutdown (code {})", MasterExit);
  return MasterExit;
}
