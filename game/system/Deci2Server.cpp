/*!
 * @file Deci2Server.cpp
 * Basic implementation of a DECI2 server.
 * Works with deci2.cpp (sceDeci2) to implement the networking on target
 */

#include <cstdio>
#include <cassert>
#include <utility>

// TODO - i think im not including the dependency right..?
#include "common/cross_sockets/xsocket.h"

#ifdef __linux
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#elif __APPLE__
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#elif _WIN32
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif

#include "common/listener_common.h"
#include "common/versions.h"
#include "Deci2Server.h"

Deci2Server::Deci2Server(std::function<bool()> shutdown_callback) {
  buffer = new char[BUFFER_SIZE];
  want_exit = std::move(shutdown_callback);
}

Deci2Server::~Deci2Server() {
  // if accept thread is running, kill it
  if (accept_thread_running) {
    kill_accept_thread = true;
    accept_thread.join();
    accept_thread_running = false;
  }

  delete[] buffer;

  close_server_socket();
  close_socket(new_sock);
}

/*!
 * Start waiting for the Listener to connect
 */
bool Deci2Server::init() {
  server_socket = open_socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    server_socket = -1;
    return false;
  }

#ifdef __linux
  int server_socket_opt = SO_REUSEADDR | SO_REUSEPORT;
#elif _WIN32
  int server_socket_opt = SO_EXCLUSIVEADDRUSE;
#elif __APPLE__
  int server_socket_opt = SO_REUSEADDR | SO_REUSEPORT;
#endif

  int opt = 1;
  if (set_socket_option(server_socket, SOL_SOCKET, server_socket_opt, &opt, sizeof(opt)) < 0) {
    close_server_socket();
    return false;
  };

  if (set_socket_option(server_socket, TCP_SOCKET_LEVEL, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
    close_server_socket();
    return false;
  }

  if (set_socket_timeout(server_socket, 100000) < 0) {
    close_server_socket();
    return false;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(DECI2_PORT);

  if (bind(server_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("[Deci2Server] Failed to bind\n");
    close_server_socket();
    return false;
  }

  if (listen(server_socket, 0) < 0) {
    printf("[Deci2Server] Failed to listen\n");
    close_server_socket();
    return false;
  }

  server_initialized = true;
  accept_thread_running = true;
  kill_accept_thread = false;
  accept_thread = std::thread(&Deci2Server::accept_thread_func, this);
  return true;
}

void Deci2Server::close_server_socket() {
  close_socket(server_socket);
  server_socket = -1;
}

/*!
 * Return true if the listener is connected.
 */
bool Deci2Server::check_for_listener() {
  if (server_connected) {
    if (accept_thread_running) {
      accept_thread.join();
      accept_thread_running = false;
    }
    return true;
  } else {
    return false;
  }
}

/*!
 * Send data from buffer. User must provide appropriate headers.
 */
void Deci2Server::send_data(void* buf, u16 len) {
  lock();
  if (!server_connected) {
    printf("[DECI2] send while not connected, not sending!\n");
  } else {
    uint16_t prog = 0;
    while (prog < len) {
      int wrote = write_to_socket(new_sock, (char*)(buf) + prog, len - prog);
      prog += wrote;
      if (!server_connected || want_exit()) {
        unlock();
        return;
      }
    }
  }
  unlock();
}

/*!
 * Lock the DECI mutex. Should be done before modifying protocols.
 */
void Deci2Server::lock() {
  deci_mutex.lock();
}

/*!
 * Unlock the DECI mutex. Should be done after modifying protocols.
 */
void Deci2Server::unlock() {
  deci_mutex.unlock();
}

/*!
 * Wait for protocols to become ready.
 * This avoids the case where we receive messages before protocol handlers are set up.
 */
void Deci2Server::wait_for_protos_ready() {
  if (protocols_ready)
    return;
  std::unique_lock<std::mutex> lk(deci_mutex);
  cv.wait(lk, [&] { return protocols_ready; });
}

/*!
 * Inform server that protocol handlers are ready.
 * Will unblock wait_for_protos_ready and incoming messages will be dispatched to these
 * protocols.  You can change the protocol handlers, but you should lock the mutex before
 * doing so.
 */
void Deci2Server::send_proto_ready(Deci2Driver* drivers, int* driver_count) {
  lock();
  d2_drivers = drivers;
  d2_driver_count = driver_count;
  protocols_ready = true;
  unlock();
  cv.notify_all();
}

void Deci2Server::run() {
  int desired_size = (int)sizeof(Deci2Header);
  int got = 0;

  while (got < desired_size) {
    assert(got + desired_size < BUFFER_SIZE);
    auto x = read_from_socket(new_sock, buffer + got, desired_size - got);
    if (want_exit()) {
      return;
    }
    got += x > 0 ? x : 0;
  }

  auto* hdr = (Deci2Header*)(buffer);
  fprintf(stderr, "[DECI2] Got message: %d %d 0x%x %c -> %c\n", hdr->len, hdr->rsvd, hdr->proto,
          hdr->src, hdr->dst);

  hdr->rsvd = got;

  // see what protocol we got:
  lock();

  int handler = -1;
  for (int i = 0; i < *d2_driver_count; i++) {
    auto& prot = d2_drivers[i];
    if (prot.active && prot.protocol) {
      if (handler != -1) {
        printf("[DECI2] Warning: more than on protocol handler for this message!\n");
      }
      handler = i;
    }
  }

  if (handler == -1) {
    printf("[DECI2] Warning: no handler for this message, ignoring...\n");
    unlock();
    return;
  }

  auto& driver = d2_drivers[handler];

  int sent_to_program = 0;
  while (!want_exit() && (hdr->rsvd < hdr->len || sent_to_program < hdr->rsvd)) {
    // send what we have to the program
    if (sent_to_program < hdr->rsvd) {
      //      driver.next_recv_size = 0;
      //      driver.next_recv = nullptr;
      driver.recv_buffer = buffer + sent_to_program;
      driver.available_to_receive = hdr->rsvd - sent_to_program;
      (driver.handler)(DECI2_READ, driver.available_to_receive, driver.opt);
      //      memcpy(driver.next_recv, buffer + sent_to_program, driver.next_recv_size);
      sent_to_program += driver.recv_size;
    }

    // receive from network
    if (hdr->rsvd < hdr->len) {
      auto x = read_from_socket(new_sock, buffer + hdr->rsvd, hdr->len - hdr->rsvd);
      if (want_exit()) {
        return;
      }
      got += x > 0 ? x : 0;
      hdr->rsvd = got;
    }
  }

  (driver.handler)(DECI2_READDONE, 0, driver.opt);
  unlock();
}

/*!
 * Background thread for waiting for the listener.
 */
void Deci2Server::accept_thread_func() {
  socklen_t l = sizeof(addr);
  while (!kill_accept_thread) {
    // TODO - might want to do a WSAStartUp call here as well, else it won't be balanced on the
    // close
    new_sock = accept(server_socket, (sockaddr*)&addr, &l);
    if (new_sock >= 0) {
      set_socket_timeout(new_sock, 100000);
      u32 versions[2] = {versions::GOAL_VERSION_MAJOR, versions::GOAL_VERSION_MINOR};
      write_to_socket(new_sock, (char*)&versions, 8);  // todo, check result?
      server_connected = true;
      return;
    }
  }
}
