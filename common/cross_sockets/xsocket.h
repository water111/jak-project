#pragma once

/*!
 * @file xsocket.h
 * Cross platform socket library used for the listener.
 */

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#elif _WIN32
#include <WinSock2.h>
#endif

#ifdef __linux
const int TCP_SOCKET_LEVEL = SOL_TCP;
#elif __APPLE__
const int TCP_SOCKET_LEVEL = IPPROTO_TCP;
#elif _WIN32
const int TCP_SOCKET_LEVEL = IPPROTO_TCP;
#endif

int open_socket(int af, int type, int protocol);
void close_socket(int sock);
int set_socket_option(int socket, int level, int optname, const void* optval, int optlen);
int set_socket_timeout(int socket, long microSeconds);
int write_to_socket(int socket, const char* buf, int len);
int read_from_socket(int socket, char* buf, int len);
bool socket_timed_out();