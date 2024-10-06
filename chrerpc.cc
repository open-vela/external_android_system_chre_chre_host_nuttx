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

#include "chrerpc.h"

#include <json/json.h>
#include <netpacket/rpmsg.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <utils/SystemClock.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <thread>

#include "chre_host/file_stream.h"
#include "chre_host/napp_header.h"

#ifndef PRELOAD_CONFIG
#define PRELOAD_CONFIG "/vendor/etc/chre/product_nanoapps.json"
#endif

#ifdef CHRE_DAEMON_DEBUG
#define PRELOAD_CONFIG_DEBUG "/data/etc/chre/product_nanoapps.json"
#endif
// CHRE-side code

namespace fbs = ::chre::fbs;

namespace android {
namespace chre {

namespace {
int createEpollFd(int fdToEpoll) {
  struct epoll_event event;
  event.data.fd = fdToEpoll;
  event.events = EPOLLIN | EPOLLWAKEUP;
  int epollFd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_ctl(epollFd, EPOLL_CTL_ADD, event.data.fd, &event) != 0) {
    LOGE("Failed to add control interface to msg read fd errno: %s",
         strerror(errno));
    close(epollFd);
  }
  return epollFd;
}
}  // anonymous namespace

ChreRpc::ChreRpc(int domain) : mHandle(-1) { mDomain = domain; }

int ChreRpc::connectSocketServer() {
  struct sockaddr* addr;
  socklen_t addrlen = 0;
  if (mDomain == AF_UNIX) {
    struct sockaddr_un uaddr;
    uaddr.sun_family = AF_UNIX;
    addr = (struct sockaddr*)&uaddr;
    strlcpy(uaddr.sun_path, SOCKET_NAME, UNIX_PATH_MAX);
    addrlen = sizeof(struct sockaddr_un);
    mHandle = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
  } else {
    struct sockaddr_rpmsg raddr = {
        .rp_family = AF_RPMSG,
        .rp_cpu = CHRE_SERVICE_CPUNAME,
        .rp_name = "",
    };
    addr = (struct sockaddr*)&raddr;
    strlcpy(raddr.rp_name, SOCKET_NAME, RPMSG_SOCKET_NAME_SIZE);
    addrlen = sizeof(struct sockaddr_rpmsg);
    mHandle = socket(PF_RPMSG, SOCK_STREAM, 0);
  }
  if (mHandle < 0) {
    LOGE("mHandle create failed errno=%d\n", errno);
    return -1;
  }
  int ret = connect(mHandle, addr, addrlen);
  if (ret < 0) {
    if (errno == EINPROGRESS) {
      LOGD("mHandle is connecting");
      ret = mHandle;
    } else {
      LOGE("mHandle connect failed errno=%d", errno);
      close(mHandle);
      mHandle = -1;
    }
  }
  if (ret >= 0) {
    fcntl(mHandle, F_SETFL, O_NONBLOCK);
  }
  return ret;
}  // namespace chre

bool ChreRpc::init() {
  bool ret = false;
  if (connectSocketServer() < 0) {
    LOGE("connectSocketServer failed: %s", strerror(errno));
  } else {
    mMsgToHostThread = std::thread(&ChreRpc::msgToHostThreadEntry, this);
    if (mMsgToHostThread.native_handle()) {
      ret = true;
      mMsgToHostThreadRunning = true;
    }
  }
  return ret;
}

void ChreRpc::deinit() {
  joinToHostThread();
  close(mHandle);
  mHandle = -1;
}

void ChreRpc::joinToHostThread() {
  if (mMsgToHostThreadRunning) {
    mMsgToHostThreadRunning = false;
    if (mMsgToHostThread.joinable()) {
      mMsgToHostThread.join();
    }
  }
}

void ChreRpc::msgToHostThreadEntry() {
  ssize_t messageLen = 0;
  ssize_t max_package_size =
      CHRE_MESSAGE_TO_HOST_MAX_SIZE + CHRE_MESSAGE_HEAD_SIZE;
  uint8_t* messageBuffer = (uint8_t*)calloc(1, max_package_size);
  if (!messageBuffer) {
    LOGE("out of memorry");
    return;
  }

  int epollFd = createEpollFd(mHandle);
  if (epollFd < 0) {
    free(messageBuffer);
    LOGE("epoll_create failed");
    return;
  }

  while (mMsgToHostThreadRunning) {
    struct epoll_event retEvent;
    int nEvents = epoll_wait(epollFd, &retEvent, 1 /* maxEvents */,
                             -1 /* infinite timeout */);
    if (nEvents <= 0) {
      // epoll_wait will get interrupted if the CHRE daemon is shutting down,
      // check this condition before logging an error.
      if (mMsgToHostThreadRunning) {
        LOGE("Epolling failed: %s", strerror(errno));
      }
    } else {
      ssize_t n = recv(mHandle, messageBuffer + messageLen,
                       max_package_size - messageLen, MSG_WAITALL);
      if (n <= 0) {
        LOGE("Invalid command received. CHRE shutting down errno=%d", errno);
        break;
      } else {
        messageLen += n;
        if (::chre::HostProtocolCommon::verifyMessage(messageBuffer,
                                                      messageLen)) {
          onMessageReceived(messageBuffer, messageLen);
          messageLen = 0;
        } else if (messageLen >= max_package_size) {
          messageLen = 0;
        }
      }
    }
  }
  constexpr auto kDelayAfterCrash = std::chrono::seconds(3);
  bool firstDetection = !mCrashDetected.exchange(true);
  auto delay = (firstDetection) ? kDelayAfterCrash : kDelayAfterCrash * 2;
  std::this_thread::sleep_for(delay);
  LOGE("Exiting daemon");
  std::exit(EXIT_FAILURE);
}

void ChreRpc::preloadedNanoapps() {
  std::ifstream configFileStream;
#ifdef CHRE_DAEMON_DEBUG
  if (access(PRELOAD_CONFIG_DEBUG, F_OK) == 0) {
    configFileStream.open(PRELOAD_CONFIG_DEBUG);
  } else
#endif
    configFileStream.open(PRELOAD_CONFIG);

  Json::CharReaderBuilder builder;
  Json::Value config;
  if (!configFileStream.is_open()) {
    LOGE("Failed to open config file product_nanoapps.json: %d (%s)", errno,
         strerror(errno));
  } else if (!Json::parseFromStream(builder, configFileStream, &config,
                                    /* errorMessage = */ nullptr)) {
    LOGE("Failed to parse nanoapp config file");
  } else if (!config.isMember("nanoapps") || !config.isMember("source_dir")) {
    LOGE("Malformed preloaded nanoapps config");
  } else {
    const Json::Value& directory = config["source_dir"];
    for (Json::ArrayIndex i = 0; i < config["nanoapps"].size(); i++) {
      const Json::Value& nanoapp = config["nanoapps"][i];
      loadPreloadedNanoapp(directory.asString(), nanoapp.asString(), 1);
    }
  }
}

void ChreRpc::loadPreloadedNanoapp(const std::string& directory,
                                   const std::string& name,
                                   uint32_t transactionId) {
  std::vector<uint8_t> header;
  std::vector<uint8_t> nanoapp;
  std::string headerFilename = directory + "/" + name + ".napp_header";
  std::string nanoappFilename = directory + "/" + name;
  if (readFileContents(headerFilename.c_str(), header) &&
      readFileContents(nanoappFilename.c_str(), nanoapp)) {
    const auto* appHeader =
        reinterpret_cast<const NanoAppBinaryHeader*>(header.data());
    mPreloadedNanoappPendingTransaction = {
        .transactionId = transactionId,
        .fragmentId = 0,
        .nanoappId = appHeader->appId,
    };
    bool ret = true;
    mPreloadedNanoappPending =
        ChreDaemonBase::loadNanoapp(header, name, transactionId);
    if (!mPreloadedNanoappPending) {
      LOGE("Failed to send nanoapp name");
      ret = false;
    }
    if (mDomain != AF_UNIX) {
      uint32_t targetApiVersion = (appHeader->targetChreApiMajorVersion << 24) |
                                  (appHeader->targetChreApiMinorVersion << 16);
      ret = sendFragmentedNanoappLoad(
          appHeader->appId, appHeader->appVersion, appHeader->flags,
          targetApiVersion, nanoapp.data(), nanoapp.size(), transactionId);
      if (!ret) {
        LOGE("loadPreloadedNanoapp failed");
      }
    }
  }
}

bool ChreRpc::sendFragmentedNanoappLoad(uint64_t appId, uint32_t appVersion,
                                        uint32_t appFlags,
                                        uint32_t appTargetApiVersion,
                                        const uint8_t* appBinary,
                                        size_t appSize,
                                        uint32_t transactionId) {
  std::vector<uint8_t> binary(appSize);
  std::copy(appBinary, appBinary + appSize, binary.begin());

  FragmentedLoadTransaction transaction(
      transactionId, appId, appVersion, appFlags, appTargetApiVersion, binary,
      CHRE_MESSAGE_TO_HOST_MAX_SIZE - 256 /*fragmentSize*/);

  bool success = true;

  while (success && !transaction.isComplete()) {
    // Pad the builder to avoid allocation churn.
    int trytimes = 0;
    const auto& fragment = transaction.getNextRequest();
    flatbuffers::FlatBufferBuilder builder(fragment.binary.size() + 128);
    HostProtocolHost::encodeFragmentedLoadNanoappRequest(
        builder, fragment, true /* respondBeforeStart */);
    while ((success = sendFragmentAndWaitOnResponse(
                transactionId, builder, fragment.fragmentId, appId)) == false &&
           trytimes++ < 3) {
      usleep(1000000);
    }
  }

  return success;
}
bool ChreRpc::sendFragmentAndWaitOnResponse(
    uint32_t transactionId, flatbuffers::FlatBufferBuilder& builder,
    uint32_t fragmentId, uint64_t appId) {
  bool success = true;

  mPreloadedNanoappPendingTransaction = {
      .transactionId = transactionId,
      .fragmentId = fragmentId,
      .nanoappId = appId,
  };
  mPreloadedNanoappPending = sendMessageToChre(
      mHostEndpointId, builder.GetBufferPointer(), builder.GetSize());
  if (!mPreloadedNanoappPending) {
    LOGE("Failed to send nanoapp fragment");
    success = false;
  } else {
    std::chrono::seconds timeout(2);
    std::unique_lock<std::mutex> lock(mloadedNanoappsMutex);
    bool signaled = mPreloadedNanoappsCond.wait_for(
        lock, timeout, [this] { return !mPreloadedNanoappPending; });

    if (!signaled) {
      LOGE("Nanoapp fragment load timed out");
      success = false;
    }
  }
  return success;
}

bool ChreRpc::doSendMessage(void* data, size_t length) {
  size_t send_len = 0;
  bool success = false;
  if (length > CHRE_MESSAGE_TO_HOST_MAX_SIZE) {
    LOGE("Message too large (got %zu, max %d bytes)", length,
         CHRE_MESSAGE_TO_HOST_MAX_SIZE);
  } else if ((send_len = send(mHandle, data, length, 0)) == length) {
    success = true;
  } else {
    LOGE("send_len=%zu != %zu errno=%d", send_len, length, errno);
  }
  return success;
}

void ChreRpc::handleDaemonMessage(const uint8_t* message) {
  std::unique_ptr<fbs::MessageContainerT> container =
      fbs::UnPackMessageContainer(message);
  if (container->message.type != fbs::ChreMessage::LoadNanoappResponse) {
    LOGE("Invalid message from CHRE directed to daemon");
  } else {
    const auto* response = container->message.AsLoadNanoappResponse();

    if (!mPreloadedNanoappPending) {
      LOGE("Received nanoapp load response with no pending load");
    } else if (mPreloadedNanoappPendingTransaction.transactionId !=
               response->transaction_id) {
      LOGE("Received nanoapp load response with invalid transaction id");
    } else if (mPreloadedNanoappPendingTransaction.fragmentId !=
               response->fragment_id) {
      LOGE("Received nanoapp load response with invalid fragment id");
    } else if (!response->success) {
#ifdef CHRE_DAEMON_METRIC_ENABLED
      std::vector<VendorAtomValue> values(3);
      values[0].set<VendorAtomValue::longValue>(
          mPreloadedNanoappPendingTransaction.nanoappId);
      values[1].set<VendorAtomValue::intValue>(
          Atoms::ChreHalNanoappLoadFailed::TYPE_PRELOADED);
      values[2].set<VendorAtomValue::intValue>(
          Atoms::ChreHalNanoappLoadFailed::REASON_ERROR_GENERIC);
      const VendorAtom atom{
          .atomId = Atoms::CHRE_HAL_NANOAPP_LOAD_FAILED,
          .values{std::move(values)},
      };
      ChreDaemonBase::reportMetric(atom);
#endif  // CHRE_DAEMON_METRIC_ENABLED

    } else {
      mPreloadedNanoappPending = false;
    }
    mPreloadedNanoappsCond.notify_one();
  }
}

#ifdef CONFIG_CHREHOST

void ChreRpc::onMessageReceived(const unsigned char* messageBuffer,
                                size_t messageLen) {
  uint16_t hostClientId;
  fbs::ChreMessage messageType;
  if (!HostProtocolHost::extractHostClientIdAndType(
          messageBuffer, messageLen, &hostClientId, &messageType)) {
    LOGW("Failed to extract host client ID from message - sending broadcast");
    hostClientId = ::chre::kHostClientIdUnspecified;
  }
  if (messageType == fbs::ChreMessage::LogMessage) {
    std::unique_ptr<fbs::MessageContainerT> container =
        fbs::UnPackMessageContainer(messageBuffer);
    const auto* logMessage = container->message.AsLogMessage();
    const std::vector<int8_t>& logData = logMessage->buffer;

    getLogger().log(reinterpret_cast<const uint8_t*>(logData.data()),
                    logData.size());
  } else if (messageType == fbs::ChreMessage::LogMessageV2) {
    std::unique_ptr<fbs::MessageContainerT> container =
        fbs::UnPackMessageContainer(messageBuffer);
    const auto* logMessage = container->message.AsLogMessageV2();
    const std::vector<int8_t>& logDataBuffer = logMessage->buffer;
    const auto* logData =
        reinterpret_cast<const uint8_t*>(logDataBuffer.data());
    uint32_t numLogsDropped = logMessage->num_logs_dropped;

    getLogger().logV2(logData, logDataBuffer.size(), numLogsDropped);
  } else if (messageType == fbs::ChreMessage::TimeSyncRequest) {
    sendTimeSync(true /* logOnError */);
  } else if (messageType == fbs::ChreMessage::LowPowerMicAccessRequest) {
    configureLpma(true /* enabled */);
  } else if (messageType == fbs::ChreMessage::LowPowerMicAccessRelease) {
    configureLpma(false /* enabled */);
  } else if (messageType == fbs::ChreMessage::MetricLog) {
#ifdef CHRE_DAEMON_METRIC_ENABLED
    std::unique_ptr<fbs::MessageContainerT> container =
        fbs::UnPackMessageContainer(messageBuffer);
    const auto* metricMsg = container->message.AsMetricLog();
    handleMetricLog(metricMsg);
#endif  // CHRE_DAEMON_METRIC_ENABLED
  } else if (messageType == fbs::ChreMessage::NanConfigurationRequest) {
    std::unique_ptr<fbs::MessageContainerT> container =
        fbs::UnPackMessageContainer(messageBuffer);
    handleNanConfigurationRequest(
        container->message.AsNanConfigurationRequest());
  } else if (messageType == fbs::ChreMessage::HostEndpointConnected) {
    std::unique_ptr<fbs::MessageContainerT> container =
        fbs::UnPackMessageContainer(messageBuffer);
    mHostEndpointId =
        container->message.AsHostEndpointConnected()->host_endpoint;
  } else if (hostClientId == kHostClientIdDaemon) {
    handleDaemonMessage(messageBuffer);
  } else if (mHostMessageCallback) {
    mHostMessageCallback(messageBuffer);
  }
}

bool ChreRpc::unloadNanoapp(uint64_t appId) {
  FlatBufferBuilder builder(64);
  HostProtocolHost::encodeUnloadNanoappRequest(
      builder, 1, appId, true /* allowSystemNanoappUnload */);
  return sendMessageToChre(mHostEndpointId, builder.GetBufferPointer(),
                           builder.GetSize());
}

bool ChreRpc::queryNanoApps() {
  FlatBufferBuilder builder(64);
  HostProtocolHost::encodeNanoappListRequest(builder);
  return sendMessageToChre(mHostEndpointId, builder.GetBufferPointer(),
                           builder.GetSize());
}

#else
void ChreRpc::run() {
  constexpr char kChreSocketName[] = "chre";
  auto serverCb = [&](uint16_t clientId, void* data, size_t len) {
    if (mCrashDetected) {
      LOGW("Dropping data, CHRE restart in process...");
    } else {
      sendMessageToChre(clientId, data, len);
    }
  };
  mServer.run(kChreSocketName, true /* allowSocketCreation */, serverCb);
}
#endif

}  // namespace chre
}  // namespace android
