/*
 * Copyright (C) 2024 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CHRE_RPMSG_DAEMON_H_
#define CHRE_RPMSG_DAEMON_H_

#include "chre_host/fbs_daemon_base.h"
#include "chre_host/log.h"

using flatbuffers::FlatBufferBuilder;

namespace android {
namespace chre {

class ChreRpc : public FbsDaemonBase {
 public:
  ChreRpc(int domain);

  ~ChreRpc() { deinit(); }

  /**
   * Initializes and starts the message handling threads,
   * then proceeds to load any preloaded nanoapps.
   *
   * @return true on successful init
   */
  bool init();

  void preloadedNanoapps();
  void loadPreloadedNanoapp(const std::string &directory,
                            const std::string &name,
                            uint32_t transactionId) override;

#ifdef CONFIG_CHREHOST
  typedef std::function<void(const void *data)> HostMessageCallback;
  void registerCallback(HostMessageCallback hostMessageCallback) {
    mHostMessageCallback = hostMessageCallback;
  }
  void run() {}
  bool unloadNanoapp(uint64_t appId);
  uint16_t getHostEndpointId() { return mHostEndpointId; }
#else
  /**
   * Starts a socket server receive loop for inbound messages.
   */
  void run();
#endif

 protected:
  void handleDaemonMessage(const uint8_t *message) override;
  bool doSendMessage(void *data, size_t length) override;
  void configureLpma(bool enabled) override { (void)enabled; }
#ifdef CONFIG_CHREHOST
  void onMessageReceived(const unsigned char *messageBuffer,
                         size_t messageLen) override;
#endif

 private:
  int mHandle;
  int mDomain;
  const char *SOCKET_NAME = "chre";
  int connectSocketServer();
  int setup_epoll(int listen_socket);
  FragmentedLoadTransaction *mTransaction = NULL;

  std::thread mMsgToHostThread;
  std::atomic_bool mCrashDetected = false;
  std::atomic<bool> mMsgToHostThreadRunning = false;

#ifdef CONFIG_CHREHOST
  HostMessageCallback mHostMessageCallback;
  uint16_t mHostEndpointId = kHostClientIdDaemon;
#endif

  //! Set to the expected transaction, fragment, app ID for loading a nanoapp.
  struct Transaction {
    uint32_t transactionId;
    uint32_t fragmentId;
    uint64_t nanoappId;
  };
  Transaction mPreloadedNanoappPendingTransaction;

  //! The mutex used to guard state between the nanoapp messaging thread
  //! and loading preloaded nanoapps.
  std::mutex mloadedNanoappsMutex;

  //! The condition variable used to wait for a nanoapp to finish loading.
  std::condition_variable mPreloadedNanoappsCond;

  //! Set to true when a preloaded nanoapp is pending load.
  bool mPreloadedNanoappPending;

  /**
   * Shutsdown the daemon, stops all the worker threads created in init()
   * Since this is to be invoked at exit, it's mostly best effort, and is
   * invoked by the class destructor
   */
  void deinit();

  /**
   * Platform specific getTimeOffset for the chrerpc daemon
   *
   * @return clock drift offset in nanoseconds
   */
  int64_t getTimeOffset(bool *success) {
    int64_t timeOffset = 0;
    *success = true;
    return timeOffset;
  }

  /**
   * Entry point for the thread that receives messages sent by CHRE.
   */
  void msgToHostThreadEntry();

  /**
   * join to the host recv thread
   */
  void joinToHostThread();

  /**
   * Loads a nanoapp using fragments.
   *
   * @param appId The ID of the nanoapp to load.
   * @param appVersion The version of the nanoapp to load.
   * @param appFlags The flags specified by the nanoapp to be loaded.
   * @param appTargetApiVersion The version of the CHRE API that the app
   * targets.
   * @param appBinary The application binary code.
   * @param appSize The size of the appBinary.
   * @param transactionId The transaction ID to use when loading.
   * @return true if successful, false otherwise.
   */
  bool sendFragmentedNanoappLoad(uint64_t appId, uint32_t appVersion,
                                 uint32_t appFlags,
                                 uint32_t appTargetApiVersion,
                                 const uint8_t *appBinary, size_t appSize,
                                 uint32_t transactionId);
  bool sendFragmentAndWaitOnResponse(uint32_t transactionId,
                                     flatbuffers::FlatBufferBuilder &builder,
                                     uint32_t fragmentId, uint64_t appId);
};
}  // namespace chre
}  // namespace android

#endif  // CHRE_FASTRPC_DAEMON_H_
