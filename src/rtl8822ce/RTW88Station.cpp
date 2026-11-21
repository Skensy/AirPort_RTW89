// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include "AirportRTW88.hpp"
#include "AirportRTW88Interface.hpp"
#include "RTW88AssocWire.hpp"
#include "../kext/rtw89_scan_ioctl.hpp"
#include <libkern/copyio.h>
#include <libkern/version.h>
#include <sys/errno.h>

namespace {
// The family passes a BSD envelope here, while apple80211Request expects
// decoded controller objects. Keep all user pointers outside the gate.
struct StationRequest {
    unsigned command;
    int selector;
    void *payload;
};

class StationBuffer {
public:
    explicit StationBuffer(size_t bytes) : size(bytes), data(IOMallocZero(bytes)) {}
    StationBuffer(const StationBuffer &) = delete;
    StationBuffer &operator=(const StationBuffer &) = delete;
    ~StationBuffer() {
        if (data) {
            if (secret) RTW88AssocWire::wipe(data, size);
            IOFree(data, size);
        }
    }
    const size_t size;
    void *const data;
    bool secret = false;
};
}

IOReturn AirportRTW88::setHardwareAddress(const IOEthernetAddress *addr)
{
    if (!_ieee80211 || !addr) return kIOReturnNotReady;
    IOReturn result = _ieee80211->setStationAddress(addr->bytes);
    if (!result) memcpy(_macAddr.bytes, addr->bytes, sizeof(_macAddr.bytes));
    return result;
}

IOReturn AirportRTW88::stationChannels(apple80211_channels_info *out)
{
    RTW88Channel channels[APPLE80211_AWDL_MAX_CHANNELS_ABI] = {};
    uint32_t count = 0;
    IOReturn result = _ieee80211->copyChannels(channels, APPLE80211_AWDL_MAX_CHANNELS_ABI, &count);
    if (result && result != kIOReturnNoSpace) return result;
    bzero(out, sizeof(*out));
    out->version = APPLE80211_VERSION;
    out->num_chan_specs = static_cast<uint16_t>(min(count, (uint32_t)APPLE80211_AWDL_MAX_CHANNELS_ABI));
    for (unsigned i = 0; i < out->num_chan_specs; ++i) {
        const auto &ch = channels[i];
        out->chan_num[i] = static_cast<uint8_t>(ch.number);
        out->chan_spec[i] = ch.number;
        out->passive[i] = ch.passive;
        out->radar_dfs[i] = ch.radar;
        out->support_40Mhz[i] = ch.number != 14;
        out->support_80Mhz[i] = ch.number > 14;
    }
    return kIOReturnSuccess;
}

bool AirportRTW88::stationIoctl(unsigned long command, void *data, SInt32 &result)
{
    if (command != SIOCSA80211 && command != SIOCGA80211 &&
        command != 0x802869c8UL && command != 0xc02869c9UL) return false;
    if (version_major < 23 || !_netif || !_workLoop || !data)
        return false;
    // Some split SET calls carry an already decoded object. Only read the
    // envelope fields after identifying the station in its IFNAMSIZ prefix.
    const char *prefix = _netif->getNamePrefix();
    if (!prefix) return false;
    char name[IFNAMSIZ] = {};
    snprintf(name, sizeof(name), "%s%u", prefix, _netif->getUnitNumber());
    if (strncmp(name, static_cast<const char *>(data), sizeof(name))) return false;
    const auto req = *static_cast<const apple80211req *>(data);
    const bool set = command == SIOCSA80211 || command == 0x802869c8UL;
    const bool get = command == SIOCGA80211 || command == 0xc02869c9UL;
    const bool scan = set && (req.req_type == APPLE80211_IOC_SCAN_REQ ||
                              req.req_type == APPLE80211_IOC_SCAN_REQ_MULTIPLE);
    const bool join = set && req.req_type == APPLE80211_IOC_ASSOCIATE &&
                      RTW88AssocWire::supportedRequestSize(req.req_len);
    const bool results = get && req.req_type == APPLE80211_IOC_SCAN_RESULT;
    const bool channels = get && req.req_type == APPLE80211_IOC_CHANNELS_INFO;
    if (!scan && !join && !results && !channels) return false;
    const size_t length = req.req_len;
    const size_t minGet = channels ? sizeof(apple80211_channels_info) : sizeof(apple80211_scan_result);
    if (!length || length > 65536 || (get && length < minGet)) {
        result = EINVAL;
        return true;
    }
    if (!req.req_data) { result = EFAULT; return true; }
    const auto user = reinterpret_cast<user_addr_t>(req.req_data);
    const size_t nativeSize = scan ? sizeof(apple80211_scan_data) :
                              join ? sizeof(apple80211_assoc_data) : length;
    StationBuffer native(nativeSize);
    if (!native.data) { result = ENOMEM; return true; }
    int selector = req.req_type;
    if (scan) {
        const bool single = selector == APPLE80211_IOC_SCAN_REQ;
        const size_t legacy = single ? sizeof(apple80211_scan_data) : sizeof(apple80211_scan_multiple_data);
        if (length != legacy && !rtw89HostScanLayout(static_cast<unsigned>(selector), length)) return false;
        StationBuffer wire(length);
        if (!wire.data) { result = ENOMEM; return true; }
        result = copyin(user, wire.data, length);
        if (result) return true;
        auto decoded = static_cast<apple80211_scan_data *>(native.data);
        if (!rtw89DecodeHostScan(static_cast<unsigned>(selector), wire.data, length, *decoded)) { result = EINVAL; return true; }
        selector = APPLE80211_IOC_SCAN_REQ;
    } else if (join) {
        native.secret = true;
        uint8_t head[RTW88AssocWire::PrefixSize] = {};
        RTW88AssocWire::Personal fields = {};
        result = copyin(user, head, sizeof(head));
        bool valid = !result && RTW88AssocWire::decode(head, sizeof(head), fields);
        RTW88AssocWire::wipe(head, sizeof(head));
        if (result) return true;
        if (!valid) return false; // The family handles other authentication suites.
        auto *assoc = static_cast<apple80211_assoc_data *>(native.data);
        assoc->version = assoc->ad_key.version = APPLE80211_VERSION;
        assoc->ad_mode = static_cast<uint16_t>(fields.mode);
        assoc->ad_auth_lower = static_cast<uint16_t>(fields.lower);
        assoc->ad_auth_upper = static_cast<uint16_t>(fields.upper);
        assoc->ad_ssid_len = fields.ssidLength;
        memcpy(assoc->ad_ssid, fields.ssid, fields.ssidLength);
        memcpy(assoc->ad_bssid.octet, fields.bssid, sizeof(fields.bssid));
        assoc->ad_key.key_len = fields.keyLength;
        assoc->ad_key.key_cipher_type = fields.cipher;
        memcpy(assoc->ad_key.key, fields.key, fields.keyLength);
        RTW88AssocWire::wipe(&fields, sizeof(fields));
    }

    StationRequest request = {static_cast<unsigned>(set ? SIOCSA80211 : SIOCGA80211), selector, native.data};
    auto dispatch = [](OSObject *owner, void *arg, void *, void *, void *) -> IOReturn {
        auto *self = static_cast<AirportRTW88 *>(owner);
        auto *r = static_cast<StationRequest *>(arg);
        if (!self->_netif || !self->_ieee80211) return kIOReturnNotReady;
        if (r->selector == APPLE80211_IOC_SCAN_RESULT) {
            apple80211_scan_result *entry = nullptr;
            auto ret = self->handleSCAN_RESULT(&entry);
            if (ret) return ret;
            if (!entry) return kIOReturnNotFound;
            memcpy(r->payload, entry, sizeof(*entry));
            return kIOReturnSuccess;
        }
        return self->apple80211Request(r->command, r->selector, self->_netif, r->payload);
    };
    result = _workLoop->runAction(dispatch, this, &request);
    if (result < 0 || result >= 256) {
        const auto error = errnoFromReturn(result);
        result = error ? error : EIO;
    }
    if (!result && get) result = copyout(native.data, user, length);
    return true;
}

SInt32 AirportRTW88::apple80211_ioctl(IO80211Interface *iface, IO80211VirtualInterface *vif,
                                      ifnet_t net, unsigned long cmd, void *data)
{
    SInt32 result;
    if (!vif && iface == _netif && stationIoctl(cmd, data, result)) return result;
    return IO80211Controller::apple80211_ioctl(iface, vif, net, cmd, data);
}

SInt32 AirportRTW88::apple80211_ioctl(IO80211SkywalkInterface *iface, unsigned long cmd, void *data)
{
    SInt32 result;
    if (stationIoctl(cmd, data, result)) return result;
    return IO80211Controller::apple80211_ioctl(iface, cmd, data);
}

SInt32 AirportRTW88::apple80211_ioctl_set(IO80211Interface *iface, IO80211VirtualInterface *vif,
                                          IO80211SkywalkInterface *skywalk, void *data)
{
    SInt32 result;
    if (!vif && iface == _netif && stationIoctl(SIOCSA80211, data, result)) return result;
    return IO80211Controller::apple80211_ioctl_set(iface, vif, skywalk, data);
}

SInt32 AirportRTW88::apple80211_ioctl_set(IO80211SkywalkInterface *iface, void *data)
{
    SInt32 result;
    if (stationIoctl(SIOCSA80211, data, result)) return result;
    return IO80211Controller::apple80211_ioctl_set(iface, data);
}
