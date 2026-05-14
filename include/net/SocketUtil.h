#pragma once

#include <string>

namespace SocketUtil {

int createListenSocket(int port);
bool setNonBlocking(int fd);
void closeFd(int fd);
std::string getPeerAddress(int fd);

} // namespace SocketUtil

