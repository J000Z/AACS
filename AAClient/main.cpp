// Distributed under GPLv3 only as specified in repository's root LICENSE file

#include "AaCommunicator.h"
#include "Library.h"
#include "Message.h"
#include <cstdint>
#include <gst/gst.h>
#include <iterator>
#include <libusb.h>
#include <stdexcept>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std;

int getSocketFd(std::string socketName) {
  int fd;
  if ((fd = socket(PF_UNIX, SOCK_SEQPACKET, 0)) < 0) {
    throw runtime_error("socket failed");
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, socketName.c_str());
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    throw runtime_error("connect failed");
  }
  return fd;
}

vector<uint8_t> getServiceDescriptor(int fd) {
  vector<uint8_t> msgOut = {0x02, 0x00, 0x00};
  if (write(fd, msgOut.data(), msgOut.size()) != msgOut.size())
    throw runtime_error("write failed");
  uint8_t buffer[10240];
  auto ret = read(fd, buffer, sizeof(buffer));
  if (ret <= 0)
    throw runtime_error("read failed");
  return vector<uint8_t>(buffer, buffer + ret);
}

int main(int argc, char **argv) {
  Library lib;
  for (auto dev : lib.getDeviceList()) {
    try {
      dev.switchToAOA();
      cout << dev << endl;
      cout << "switch ok!" << endl;
    } catch (...) {
      continue;
    }
  }
  sleep(1);
  auto devices = lib.getDeviceList();
  Device *device = nullptr;
  for (auto &dev : devices) {
    if (dev.getVid() != 0x18d1 ||
        (dev.getPid() != 0x2d00 && dev.getPid() != 0x2d01))
      continue;
    device = &dev;
  }
  if (device == nullptr)
    throw runtime_error("cannot find device");
  cout << "device found" << endl;
  auto fd = getSocketFd(argv[1]);
  auto sd = getServiceDescriptor(fd);
  cout << "got sd" << endl;
  gst_init(&argc, &argv);
  AaCommunicator communicator(*device, sd);
  int hi = 0;
  auto th = std::thread([fd, &communicator, &hi]() {
    uint8_t buffer[10240];
    while (true) {
      auto ret = read(fd, buffer, sizeof buffer);
      if (ret <= 0)
        throw runtime_error("read failed");
      cout << hi++ << " data from headunit: " << ret;
#ifdef PRINT_PACKET
      cout << " c: " << (int)buffer[0];
      for (auto i = 2; i < ret; i++) {
        cout << " 0x" << hex << (int)buffer[i];
      }
#endif
      cout << dec << endl;
      communicator.sendMessagePublic(buffer[0], (bool)buffer[1],
                                     vector<uint8_t>(buffer + 2, buffer + ret));
    }
  });
  int pi = 0;
  communicator.channelMessage.connect(
      [fd, &pi](uint8_t channel, bool specific,
                const std::vector<uint8_t> &msg) {
        vector<uint8_t> message;
        message.push_back(0x01); // raw data
        message.push_back(channel);
        message.push_back(specific ? 0xff : 0x00);
        copy(msg.begin(), msg.end(), back_inserter(message));
        auto ret = write(fd, message.data(), message.size());
        if (ret != message.size())
          throw runtime_error("write failed");
        cout << pi++ << " data from phone: " << message.size();
#ifdef PRINT_PACKET
        cout << " c: " << (int)channel;
        for (auto i = 0; i < msg.size(); i++) {
          cout << " 0x" << hex << (int)msg.data()[i];
        }
#endif
        cout << dec << endl;
      });
  communicator.setup();
  // sleep(100000);

  return 0;
}
