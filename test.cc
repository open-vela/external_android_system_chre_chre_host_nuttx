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

#include "chrerpc.h"

#ifdef CONFIG_CHREHOST

namespace fbs = ::chre::fbs;

void onMessageReceivedFromChre(const void* message) {
  fbs::NanoappMessageT nMsg;
  std::unique_ptr<fbs::MessageContainerT> container =
      fbs::UnPackMessageContainer(message);
  LOGD("get container->message.type=%d", (int)container->message.type);

  fbs::ChreMessageUnion& msg = container->message;
  // maybe need lock
  switch (container->message.type) {
    case fbs::ChreMessage::NanoappMessage:
      LOGD("get NanoappMessage");
      nMsg = *msg.AsNanoappMessage();
      lib_dumpbuffer("NanoappMessage", &nMsg.message[0], nMsg.message.size());
      break;

    case fbs::ChreMessage::HubInfoResponse:
      LOGD("onMessageReceivedFromChre HubInfoResponse");
      break;

    case fbs::ChreMessage::NanoappListResponse:
      LOGD("onMessageReceivedFromChre NanoappListResponse");
      break;

    case fbs::ChreMessage::LoadNanoappResponse:
      LOGD("onMessageReceivedFromChre LoadNanoappResponse");
      break;

    case fbs::ChreMessage::UnloadNanoappResponse:
      LOGD("onMessageReceivedFromChre UnloadNanoappResponse");
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
    daemon.registerCallback(onMessageReceivedFromChre);
    // preload nanoapp list from product_nanoapps.json
    // daemon.preloadedNanoapps();
    // load a specific nanoapp
    while (1) {
      daemon.loadPreloadedNanoapp("/data/chre", "message_world", 1);
      sleep(3);
      daemon.unloadNanoapp(0x0123456789000003);
      sleep(5);
    }
  }

  return 0;
}
