// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

#include "common/defines.h"
#include <gtest/gtest.h>

#if WASMEDGE_OS_LINUX || WASMEDGE_OS_MACOS

#include "host/wasi/wasibase.h"
#include "host/wasi/wasifunc.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <string_view>

using namespace std::literals;

namespace {

void writeDummyMemoryContent(
    WasmEdge::Runtime::Instance::MemoryInstance &MemInst) noexcept {
  std::fill_n(MemInst.getPointer<uint8_t *>(0), 64, UINT8_C(0xa5));
}

void writeString(WasmEdge::Runtime::Instance::MemoryInstance &MemInst,
                 std::string_view String, uint32_t Ptr) noexcept {
  std::copy(String.begin(), String.end(), MemInst.getPointer<uint8_t *>(Ptr));
}

void writeAddrinfo(WasmEdge::Runtime::Instance::MemoryInstance &MemInst,
                   __wasi_addrinfo_t *WasiAddrinfo, uint32_t Ptr) {
  std::memcpy(
      MemInst.getPointer<__wasi_addrinfo_t *>(Ptr, sizeof(__wasi_addrinfo_t)),
      WasiAddrinfo, sizeof(__wasi_addrinfo_t));
}

void allocateAddrinfoArray(WasmEdge::Runtime::Instance::MemoryInstance &MemInst,
                           uint32_t Base, uint32_t Length,
                           uint32_t CanonnameMaxSize) {
  for (uint32_t Item = 0; Item < Length; Item++) {
    // allocate addrinfo struct
    auto *ResItemPtr = MemInst.getPointer<__wasi_addrinfo_t *>(
        Base, sizeof(__wasi_addrinfo_t));
    Base += sizeof(__wasi_addrinfo_t);

    // allocate sockaddr struct
    ResItemPtr->ai_addr = Base;
    ResItemPtr->ai_addrlen = sizeof(__wasi_sockaddr_t);
    auto *Sockaddr = MemInst.getPointer<__wasi_sockaddr_t *>(
        ResItemPtr->ai_addr, sizeof(__wasi_sockaddr_t));
    Base += ResItemPtr->ai_addrlen;
    // allocate sockaddr sa_data.
    Sockaddr->sa_data = Base;
    Sockaddr->sa_data_len = WasmEdge::Host::WASI::kSaDataLen;
    Base += Sockaddr->sa_data_len;
    // allocate ai_canonname
    ResItemPtr->ai_canonname = Base;
    ResItemPtr->ai_canonname_len = CanonnameMaxSize;
    Base += ResItemPtr->ai_canonname_len;
    if (Item != (Length - 1)) {
      ResItemPtr->ai_next = Base;
    }
  }
}
} // namespace

TEST(WasiTest, SocketUDP) {
  WasmEdge::Host::WASI::Environ Env;
  WasmEdge::Runtime::Instance::MemoryInstance MemInst(
      WasmEdge::AST::MemoryType(1));

  WasmEdge::Host::WasiSockOpen WasiSockOpen(Env);
  WasmEdge::Host::WasiFdClose WasiFdClose(Env);
  WasmEdge::Host::WasiSockBind WasiSockBind(Env);
  WasmEdge::Host::WasiSockSendTo WasiSockSendTo(Env);
  WasmEdge::Host::WasiSockRecvFrom WasiSockRecvFrom(Env);

  std::array<WasmEdge::ValVariant, 1> Errno;

  // Open and Close udp socket
  {
    uint32_t AddressFamily = __WASI_ADDRESS_FAMILY_INET4;
    uint32_t SockType = __WASI_SOCK_TYPE_SOCK_DGRAM;
    uint32_t Port = 12345;
    uint32_t FdServerPtr = 0;
    uint32_t FdClientPtr = 4;
    uint32_t SendtoRetPtr = 8;
    uint32_t RecvfromRetPtr = 12;
    uint32_t FlagPtr = 16;
    uint32_t AddrBufPtr = 100;
    uint32_t AddrBuflen = 4;
    uint32_t AddrPtr = 200;
    uint32_t MsgInPackPtr = 900;
    uint32_t MsgInPtr = 1000;
    uint32_t MsgOutPackPtr = 1900;
    uint32_t MsgOutPtr = 2000;

    writeDummyMemoryContent(MemInst);
    WasiSockOpen.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{AddressFamily,
                                                         SockType, FdServerPtr},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    EXPECT_NE(*MemInst.getPointer<const uint32_t *>(FdServerPtr), UINT32_C(-1));

    int32_t FdServer = *MemInst.getPointer<const int32_t *>(FdServerPtr);

    WasiSockOpen.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{AddressFamily,
                                                         SockType, FdClientPtr},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    EXPECT_NE(*MemInst.getPointer<const uint32_t *>(FdClientPtr), UINT32_C(-1));

    int32_t FdClient = *MemInst.getPointer<const int32_t *>(FdClientPtr);

    WasiSockOpen.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{AddressFamily,
                                                         SockType, FdClientPtr},
                     Errno);
    EXPECT_NE(*MemInst.getPointer<const uint32_t *>(FdClientPtr), UINT32_C(-1));

    auto *AddrBuf =
        MemInst.getPointer<uint8_t *>(AddrBufPtr, sizeof(uint8_t) * AddrBuflen);
    auto *Addr = MemInst.getPointer<__wasi_address_t *>(
        AddrPtr, sizeof(__wasi_address_t));

    ::memset(AddrBuf, 0x00, AddrBuflen);
    Addr->buf = AddrBufPtr;
    Addr->buf_len = AddrBuflen;

    WasiSockBind.run(
        &MemInst, std::array<WasmEdge::ValVariant, 3>{FdServer, AddrPtr, Port},
        Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);

    const auto Msg1 = "hello, wasmedge."sv;
    uint32_t Msg1Len = Msg1.size();
    writeString(MemInst, Msg1, MsgInPtr);

    auto *MsgInPack = MemInst.getPointer<__wasi_ciovec_t *>(
        MsgInPackPtr, sizeof(__wasi_ciovec_t));
    MsgInPack->buf = MsgInPtr;
    MsgInPack->buf_len = Msg1Len;

    auto *AddrBufSend =
        MemInst.getPointer<uint32_t *>(AddrBufPtr, sizeof(uint32_t));
    *AddrBufSend = htonl(INADDR_LOOPBACK);
    Addr->buf_len = sizeof(uint32_t);

    WasiSockSendTo.run(&MemInst,
                       std::array<WasmEdge::ValVariant, 7>{
                           FdClient, MsgInPackPtr, UINT32_C(1), AddrPtr,
                           INT32_C(Port), UINT32_C(0), SendtoRetPtr},
                       Errno);

    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);

    uint32_t MaxMsgBufLen = 100;
    auto *MsgBuf =
        MemInst.getPointer<char *>(MsgOutPtr, sizeof(char) * MaxMsgBufLen);
    ::memset(MsgBuf, 0x00, MaxMsgBufLen);

    auto *MsgOutPack = MemInst.getPointer<__wasi_ciovec_t *>(
        MsgOutPackPtr, sizeof(__wasi_ciovec_t));
    MsgOutPack->buf = MsgOutPtr;
    MsgOutPack->buf_len = MaxMsgBufLen;

    WasiSockRecvFrom.run(&MemInst,
                         std::array<WasmEdge::ValVariant, 7>{
                             FdServer, MsgOutPackPtr, UINT32_C(1), AddrPtr,
                             UINT32_C(0), RecvfromRetPtr, FlagPtr},
                         Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);

    std::string_view MsgRecv{MsgBuf, Msg1.size()};
    EXPECT_EQ(MsgRecv, Msg1);

    WasiFdClose.run(&MemInst, std::array<WasmEdge::ValVariant, 1>{FdServer},
                    Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    WasiFdClose.run(&MemInst, std::array<WasmEdge::ValVariant, 1>{FdClient},
                    Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    Env.fini();
  }
  // False SockType
  {
    uint32_t AddressFamily = __WASI_ADDRESS_FAMILY_INET4;
    uint32_t SockType = 2;

    writeDummyMemoryContent(MemInst);
    WasiSockOpen.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{AddressFamily,
                                                         SockType, UINT32_C(0)},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_INVAL);
    Env.fini();
  }
  // False AddressFamily
  {
    uint32_t AddressFamily = 2;
    uint32_t SockType = __WASI_SOCK_TYPE_SOCK_DGRAM;

    writeDummyMemoryContent(MemInst);
    WasiSockOpen.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{AddressFamily,
                                                         SockType, UINT32_C(0)},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_INVAL);
    Env.fini();
  }
  // Invaild Address Length for Bind
  {
    uint32_t Fd = 0;
    uint32_t Port = 12345;
    uint8_t_ptr AddrBufPtr = 100;
    uint32_t AddrBuflen = 7;
    uint32_t AddrPtr = 200;
    auto *AddrBuf =
        MemInst.getPointer<uint8_t *>(AddrBufPtr, sizeof(uint8_t) * AddrBuflen);
    auto *Addr = MemInst.getPointer<__wasi_address_t *>(
        AddrPtr, sizeof(__wasi_address_t));

    ::memset(AddrBuf, 0x00, AddrBuflen);
    Addr->buf = AddrBufPtr;
    Addr->buf_len = AddrBuflen;

    WasiSockBind.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{Fd, AddrPtr, Port},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_INVAL);
  }
  // Invaild Fd for Bind
  {
    uint32_t Fd = 0;
    uint32_t Port = 12345;
    uint8_t_ptr AddrBufPtr = 100;
    uint32_t AddrBuflen = 16;
    uint32_t AddrPtr = 200;
    auto *AddrBuf =
        MemInst.getPointer<uint8_t *>(AddrBufPtr, sizeof(uint8_t) * AddrBuflen);
    auto *Addr = MemInst.getPointer<__wasi_address_t *>(
        AddrPtr, sizeof(__wasi_address_t));

    ::memset(AddrBuf, 0x00, AddrBuflen);
    Addr->buf = AddrBufPtr;
    Addr->buf_len = AddrBuflen;

    WasiSockBind.run(&MemInst,
                     std::array<WasmEdge::ValVariant, 3>{Fd, AddrPtr, Port},
                     Errno);
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_BADF);
  }
}

TEST(WasiTest, GetAddrinfo) {
  WasmEdge::Host::WASI::Environ Env;
  WasmEdge::Runtime::Instance::MemoryInstance MemInst(
      WasmEdge::AST::MemoryType(1));

  WasmEdge::Host::WasiGetAddrinfo WasiGetAddrinfo(Env);

  std::array<WasmEdge::ValVariant, 1> Errno;

  uint32_t NodePtr = 0;
  uint32_t ServicePtr = 32;
  uint32_t HintsPtr = 48;
  uint32_t ResLengthPtr = 100;
  uint32_t ResultPtr = 104;
  std::string Node = "";
  std::string Service = "27015";
  uint32_t MaxLength = 10;
  uint32_t CanonnameMaxSize = 50;

  const uint32_t NodeLen = Node.size();
  const uint32_t ServiceLen = Service.size();

  __wasi_addrinfo_t Hints;
  std::memset(&Hints, 0, sizeof(Hints));
  Hints.ai_family = __WASI_ADDRESS_FAMILY_INET4;   /* Allow IPv4 */
  Hints.ai_socktype = __WASI_SOCK_TYPE_SOCK_DGRAM; /* Datagram socket */
  Hints.ai_flags = __WASI_AIFLAGS_AI_PASSIVE;      /* For wildcard IP address */
  writeString(MemInst, Node, NodePtr);
  writeString(MemInst, Service, ServicePtr);
  writeAddrinfo(MemInst, &Hints, HintsPtr);
  auto *ResLength =
      MemInst.getPointer<uint32_t *>(ResLengthPtr, sizeof(uint32_t));
  *ResLength = 0;
  auto *Result =
      MemInst.getPointer<uint8_t_ptr *>(ResultPtr, sizeof(uint8_t_ptr));
  *Result = 108;
  // allocate Res Item;
  allocateAddrinfoArray(MemInst, *Result, MaxLength, CanonnameMaxSize);

  Env.init({}, "test"s, {}, {});
  // MaxLength == 0;
  {
    uint32_t TmpResMaxLength = 0;
    EXPECT_TRUE(WasiGetAddrinfo.run(&MemInst,
                                    std::initializer_list<WasmEdge::ValVariant>{
                                        NodePtr, NodeLen, ServicePtr,
                                        ServiceLen, HintsPtr, ResultPtr,
                                        TmpResMaxLength, ResLengthPtr},
                                    Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_AIMEMORY);
  }
  // MemInst is nullptr
  {
    EXPECT_TRUE(
        WasiGetAddrinfo.run(nullptr,
                            std::initializer_list<WasmEdge::ValVariant>{
                                NodePtr, NodeLen, ServicePtr, ServiceLen,
                                HintsPtr, ResultPtr, MaxLength, ResLengthPtr},
                            Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_FAULT);
  }
  // Node and Service are all nullptr
  {
    uint32_t TmpNodeLen = 0;
    uint32_t TmpServiceLen = 0;
    EXPECT_TRUE(
        WasiGetAddrinfo.run(&MemInst,
                            std::initializer_list<WasmEdge::ValVariant>{
                                NodePtr, TmpNodeLen, ServicePtr, TmpServiceLen,
                                HintsPtr, ResultPtr, MaxLength, ResLengthPtr},
                            Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_AINONAME);
  }
  // node is nullptr, service is not nullptr
  {
    uint32_t TmpNodeLen = 0;
    EXPECT_TRUE(
        WasiGetAddrinfo.run(&MemInst,
                            std::initializer_list<WasmEdge::ValVariant>{
                                NodePtr, TmpNodeLen, ServicePtr, ServiceLen,
                                HintsPtr, ResultPtr, MaxLength, ResLengthPtr},
                            Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    auto *Res =
        MemInst.getPointer<uint8_t_ptr *>(ResultPtr, sizeof(uint8_t_ptr));

    auto *ResHead = MemInst.getPointer<__wasi_addrinfo_t *>(
        *Res, sizeof(__wasi_addrinfo_t));
    auto *ResItem = ResHead;
    EXPECT_NE(*ResLength, 0);
    for (uint32_t Idx = 0; Idx < *ResLength; Idx++) {
      EXPECT_NE(ResItem->ai_addrlen, 0);
      auto *TmpSockAddr = MemInst.getPointer<__wasi_sockaddr_t *>(
          ResItem->ai_addr, sizeof(__wasi_sockaddr_t));
      EXPECT_EQ(TmpSockAddr->sa_data_len, 14);
      EXPECT_EQ(MemInst.getPointer<char *>(TmpSockAddr->sa_data,
                                           TmpSockAddr->sa_data_len)[0],
                'i');
      if (Idx != (*ResLength) - 1) {
        ResItem = MemInst.getPointer<__wasi_addrinfo_t *>(
            ResItem->ai_next, sizeof(__wasi_addrinfo_t));
      }
    }
  }
  allocateAddrinfoArray(MemInst, *Result, MaxLength, CanonnameMaxSize);
  // hints.ai_flag is ai_canonname but has an error
  {
    Hints.ai_flags = __WASI_AIFLAGS_AI_CANONNAME;
    writeAddrinfo(MemInst, &Hints, HintsPtr);
    EXPECT_TRUE(
        WasiGetAddrinfo.run(&MemInst,
                            std::initializer_list<WasmEdge::ValVariant>{
                                NodePtr, NodeLen, ServicePtr, ServiceLen,
                                HintsPtr, ResultPtr, MaxLength, ResLengthPtr},
                            Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_AIBADFLAG);
  }

  // node is nullptr, service is not nullptr
  {
    std::string TmpNode = "google.com";
    writeString(MemInst, TmpNode, NodePtr);
    uint32_t TmpNodeLen = TmpNode.size();
    EXPECT_TRUE(
        WasiGetAddrinfo.run(&MemInst,
                            std::initializer_list<WasmEdge::ValVariant>{
                                NodePtr, TmpNodeLen, ServicePtr, ServiceLen,
                                HintsPtr, ResultPtr, MaxLength, ResLengthPtr},
                            Errno));
    EXPECT_EQ(Errno[0].get<int32_t>(), __WASI_ERRNO_SUCCESS);
    EXPECT_NE(*ResLength, 0);
    auto *Res =
        MemInst.getPointer<uint8_t_ptr *>(ResultPtr, sizeof(uint8_t_ptr));

    auto *ResHead = MemInst.getPointer<__wasi_addrinfo_t *>(
        *Res, sizeof(__wasi_addrinfo_t));
    EXPECT_NE(ResHead->ai_canonname_len, 0);
    EXPECT_STREQ(MemInst.getPointer<const char *>(ResHead->ai_canonname,
                                                  ResHead->ai_canonname_len),
                 "google.com");
    auto *WasiSockAddr = MemInst.getPointer<__wasi_sockaddr_t *>(
        ResHead->ai_addr, sizeof(__wasi_sockaddr_t));
    EXPECT_EQ(WasiSockAddr->sa_data_len, 14);
  }
}
#endif

GTEST_API_ int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
