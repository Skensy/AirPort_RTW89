# Additional objects for the RTW88 AirPort target.
SYNC_OBJS := $(BUILD_DIR)/airport/RTW88Station.o $(BUILD_DIR)/airport/rtw88_station_mac.o $(BUILD_DIR)/airport/owly_native_config.o $(BUILD_DIR)/airport/RTW88Peer.o $(BUILD_DIR)/airport/RTW88Sync.o $(BUILD_DIR)/airport/owly_awdl_sync.o $(BUILD_DIR)/airport/owly_awdl_frame.o
AIRPORT_ALL_OBJS += $(SYNC_OBJS)
$(AIRPORT_OUT_BIN): $(SYNC_OBJS)
$(BUILD_DIR)/airport/RTW88%.o: $(RTW88_LOCAL)/RTW88%.cpp | $(BUILD_DIR)/airport
	$(CXX) -I$(KEXT_SRC) $(KEXT_CXXFLAGS) -D__IO80211_TARGET=__MAC_13_0 -c $< -o $@
$(BUILD_DIR)/airport/owly_%.o: $(RTW88_SYNC)/owly_%.c | $(BUILD_DIR)/airport
	$(CC) $(KEXT_FLAGS) -DRTW89_MACOS -c $< -o $@
-include $(SYNC_OBJS:.o=.d)


$(BUILD_DIR)/airport/rtw88_station_mac.o: $(RTW88_LOCAL)/rtw88_station_mac.c | $(BUILD_DIR)/airport
	$(CC) $(DRIVER_CFLAGS) -c $< -o $@
