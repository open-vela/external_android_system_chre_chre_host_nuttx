CXXFLAGS += -I$(CHRE_PREFIX)/host/nuttx/include
CXXFLAGS += -I$(CHRE_PREFIX)/host/common/include
CXXFLAGS += -I$(CHRE_PREFIX)/external/flatbuffers/include
CXXFLAGS += -I$(CHRE_PREFIX)/external/pigweed/pw_log_nanoapp/public_overrides
CXXFLAGS += -I$(CHRE_PREFIX)/chre_api/include
CXXFLAGS += -I$(CHRE_PREFIX)/chre_api/legacy/v1_7

CXXFLAGS += -I$(CHRE_PREFIX)/../../../system/core/libcutils/include
CXXFLAGS += -I$(CHRE_PREFIX)/../../../system/core/libutils/include
CXXFLAGS += -I$(CHRE_PREFIX)/../../../system/libbase/include
CXXFLAGS += -I$(CHRE_PREFIX)/../../../system/logging/include

CXXFLAGS += -DCHRE_MESSAGE_TO_HOST_MAX_SIZE=${CONFIG_CHRE_MESSAGE_TO_HOST_MAX_SIZE}
CXXFLAGS += -DCHRE_MESSAGE_HEAD_SIZE=256
CXXFLAGS += -DPRELOAD_CONFIG='${CONFIG_PRELOAD_CONFIG}'
CXXFLAGS += -DCHRE_SERVICE_CPUNAME=\"ap\"

PIGWEED_DIR = $(APPDIR)/netutils/connectedhomeip/pigweed
CXXFLAGS += -I$(PIGWEED_DIR)/third_party/fuchsia/repo/sdk/lib/stdcompat/include
CXXFLAGS += -I$(PIGWEED_DIR)/pw_assert/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_string/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_bytes/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_base64/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_polyfill/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_polyfill/public_overrides
CXXFLAGS += -I$(PIGWEED_DIR)/pw_polyfill/standard_library_public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_preprocessor/public
CFLAGS   += -I$(PIGWEED_DIR)/pw_preprocessor/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_tokenizer/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_varint/public
CFLAGS   += -I$(PIGWEED_DIR)/pw_varint/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_span/public
CXXFLAGS += -I$(PIGWEED_DIR)/pw_containers/public

CHRE_PIGWEED_DIR = $(CHRE_PREFIX)/external/pigweed
CXXFLAGS += -I$(CHRE_PIGWEED_DIR)/pw_assert_nanoapp/public_overrides

CXXFLAGS += -I$(APPDIR)/netutils/jsoncpp/jsoncpp/include
CXXFLAGS += -I$(APPDIR)/external/android/system/logging/include
CXXFLAGS += -I$(APPDIR)/netutils/connectedhomeip/pigweed/pw_log/public

CXXSRCS += $(PIGWEED_DIR)/pw_tokenizer/detokenize.cc
CXXSRCS += $(PIGWEED_DIR)/pw_tokenizer/decode.cc
$(PIGWEED_DIR)/pw_tokenizer/decode.cc_CXXFLAGS +=-Wno-double-promotion

CXXSRCS += $(PIGWEED_DIR)/pw_varint/varint.cc
CSRCS += $(PIGWEED_DIR)/pw_varint/varint_c.c
CXXSRCS += $(CHRE_PREFIX)/host/common/daemon_base.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/config_util.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/file_stream.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/fragmented_load_transaction.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/host_protocol_host.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/log_message_parser.cc
CXXSRCS += $(CHRE_PREFIX)/host/common/socket_server.cc

CXXSRCS += $(CHRE_PREFIX)/host/common/fbs_daemon_base.cc
CXXSRCS += $(CHRE_PREFIX)/host/nuttx/chrerpc.cc
