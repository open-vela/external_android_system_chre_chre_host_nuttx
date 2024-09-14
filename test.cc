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

#include <debug.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "chrerpc.h"

using namespace std;

#ifdef CONFIG_CHREHOST

namespace fbs = ::chre::fbs;

typedef struct {
  bool featureInst;
  int32_t featureType;
  vector<uint64_t> applist;
} TestData;

void handleNanoappMessage(const TestData* data,
                          const fbs::NanoappMessageT& message) {
  LOGI("featureInst=%d, featureType=%d\n", data->featureInst,
       data->featureType);
  LOGI("Got message from nanoapp 0x%" PRIx64 " to endpoint 0x%" PRIx16
       " with type 0x%" PRIx32 " and length %zu",
       message.app_id, message.host_endpoint, message.message_type,
       message.message.size());
}

void handleNanoappListResponse(const TestData* data,
                               const fbs::NanoappListResponseT& response) {
  LOGI("featureInst=%d, featureType=%d\n", data->featureInst,
       data->featureType);
  LOGI("Got nanoapp list response with %zu apps:", response.nanoapps.size());
  data->applist.clear();
  for (const std::unique_ptr<fbs::NanoappListEntryT>& nanoapp :
       response.nanoapps) {
    LOGI("  App ID 0x%016" PRIx64 " version 0x%" PRIx32
         " permissions 0x%" PRIx32 " enabled %d system %d",
         nanoapp->app_id, nanoapp->version, nanoapp->permissions,
         nanoapp->enabled, nanoapp->is_system);
    data->applist.push_back(nanoapp->app_id);
  }
}

void handleLoadNanoappResponse(const TestData* data,
                               const fbs::LoadNanoappResponseT& response) {
  LOGI("featureInst=%d, featureType=%d\n", data->featureInst,
       data->featureType);
  LOGI("Got load nanoapp response, transaction ID 0x%" PRIx32 " result %d",
       response.transaction_id, response.success);
}

void handleUnloadNanoappResponse(const TestData* data,
                                 const fbs::UnloadNanoappResponseT& response) {
  LOGI("featureInst=%d, featureType=%d\n", data->featureInst,
       data->featureType);
  LOGI("Got unload nanoapp response, transaction ID 0x%" PRIx32 " result %d",
       response.transaction_id, response.success);
}

void onMessageReceivedFromChre(TestData* data, const void* message) {
  fbs::NanoappMessageT nMsg;
  std::unique_ptr<fbs::MessageContainerT> container =
      fbs::UnPackMessageContainer(message);
  LOGD("get container->message.type=%d", (int)container->message.type);

  fbs::ChreMessageUnion& msg = container->message;
  // maybe need lock
  switch (container->message.type) {
    case fbs::ChreMessage::NanoappMessage:
      handleNanoappMessage(testprivdata, *msg.AsNanoappMessage());
      break;

    case fbs::ChreMessage::HubInfoResponse:
      LOGD("onMessageReceivedFromChre HubInfoResponse");
      break;

    case fbs::ChreMessage::NanoappListResponse:
      handleNanoappListResponse(testprivdata, *msg.AsNanoappListResponse());
      break;

    case fbs::ChreMessage::LoadNanoappResponse:
      handleLoadNanoappResponse(testprivdata, *msg.AsLoadNanoappResponse());
      break;

    case fbs::ChreMessage::UnloadNanoappResponse:
      handleUnloadNanoappResponse(testprivdata, *msg.AsUnloadNanoappResponse());
      break;

    case fbs::ChreMessage::DebugDumpData:
      LOGD("onMessageReceivedFromChre DebugDumpData");
      break;

    case fbs::ChreMessage::DebugDumpResponse:
      LOGD("onMessageReceivedFromChre DebugDumpResponse");
      break;

    case fbs::ChreMessage::SelfTestResponse:
      LOGD("onMessageReceivedFromChre SelfTestResponse");
      break;

    default:
      LOGD("Got invalid/unexpected message type %d",
           (int)container->message.type);
  }
}
#endif
extern "C" int main(int argc, char** argv) {
  int c;
  int rpctype = AF_UNIX;

  LOGD("RpcDaemon");

  while ((c = getopt(argc, argv, "R:")) != -1) {
    switch (c) {
      case 'R':
        LOGD("%s\n", optarg);
        rpctype = (atoi(optarg) == 0) ? AF_UNIX : AF_RPMSG;
        break;
      default:
        LOGI("Unknown option 0x%x\n", optopt);
        return -EPERM;
    }
  }
  android::chre::ChreRpc daemon(rpctype);

  if (!daemon.init()) {
    LOGE("failed to init the daemon");
  } else {
    TestData testdata;
    testdata.featureInst = true;
    testdata.featureType = 2;
    auto messageCb = [&testdata](const void* msg) {
      onMessageReceivedFromChre(&testdata, msg);
    };
    daemon.registerCallback(messageCb);
    // preload nanoapp list from product_nanoapps.json
    // daemon.preloadedNanoapps();
    // load a specific nanoapp
    while (1) {
      daemon.loadPreloadedNanoapp("/data/chre", "hello_world", 1);
      sleep(1);
      daemon.loadPreloadedNanoapp("/data/chre", "message_world", 1);
      sleep(3);
      for (auto it = testdata.applist.begin(); it != testdata.applist.end();
           ++it) {
        daemon.unloadNanoapp(*it);
        sleep(1);
      }
    }
  }
  return 0;
}
