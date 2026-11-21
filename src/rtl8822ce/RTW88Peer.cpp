// SPDX-License-Identifier: GPL-2.0
#include "AirportRTW88.hpp"
#include "rtw89_awdl_action_decode.h"
#include <kern/clock.h>
#include <IOKit/network/IOOutputQueue.h>
#include <libkern/OSByteOrder.h>

// Methods exported by the Tahoe kit's IO80211FamilyLegacy 1200.12.2b1.
extern "C" IO80211Controller *rtw88VifController(IO80211VirtualInterface *)
    __asm__("__ZN23IO80211VirtualInterface13getControllerEv");
extern "C" bool rtw88ProcessAction(OSObject *, mbuf_t, UInt, void *, size_t)
    __asm__("__ZN22IO80211AWDLPeerManager23actionFrameProcessInputEP6__mbufjPvm");
extern "C" void rtw88PushPacket(IO80211VirtualInterface *, mbuf_t)
    __asm__("__ZN23IO80211VirtualInterface10pushPacketEP6__mbuf");

class RTW88AwdlInterface : public IO80211P2PInterface {
    OSDeclareDefaultStructors(RTW88AwdlInterface)
public:
    bool createPeerManager(ether_addr *mac, IO80211PeerManager **peer) override {
        bool ok = IO80211P2PInterface::createPeerManager(mac, peer);
        auto *controller = OSDynamicCast(AirportRTW88, rtw88VifController(this));
        if (ok && peer && controller) controller->bindAwdlPeer(this, *peer);
        return ok;
    }
    UInt32 outputPacket(mbuf_t packet, void *) override {
        auto *controller = OSDynamicCast(AirportRTW88, rtw88VifController(this));
        if (controller) return controller->outputAwdlPacket(this, packet);
        if (packet) mbuf_freem(packet);
        return kIOReturnOutputDropped;
    }
};
OSDefineMetaClassAndStructors(RTW88AwdlInterface, IO80211P2PInterface)
static_assert(sizeof(RTW88AwdlInterface) == sizeof(IO80211P2PInterface),
              "The native virtual interface owns its entire object layout");

IO80211VirtualInterface *AirportRTW88::newAwdlInterface()
{
    if (IO80211P2PInterface::metaClass->getClassSize() > sizeof(RTW88AwdlInterface))
        return nullptr;
    return new RTW88AwdlInterface;
}

void AirportRTW88::bindAwdlPeer(IO80211VirtualInterface *vif, IO80211PeerManager *raw)
{
    if (!_syncLock) return;
    auto *peer = reinterpret_cast<OSObject *>(raw);
    if (peer && !peer->metaCast("IO80211AWDLPeerManager")) return;
    if (peer) peer->retain();
    if (vif) vif->retain();
    IOLockLock(_syncLock);
    OSObject *oldPeer = _peer;
    auto *oldVif = _peerVif;
    _peer = peer;
    _peerVif = vif;
    IOLockUnlock(_syncLock);
    if (oldPeer) oldPeer->release();
    if (oldVif) oldVif->release();
}

UInt32 AirportRTW88::outputAwdlPacket(IO80211VirtualInterface *vif, mbuf_t packet)
{
    if (_ieee80211 && _awdlManager && vif == _awdlManager->awdlInterface())
        return _ieee80211->txAWDLDataFrame(packet) ? kIOReturnOutputSuccess : kIOReturnOutputDropped;
    if (packet) mbuf_freem(packet);
    return kIOReturnOutputDropped;
}

void AirportRTW88::injectRxActionFrame(const uint8_t *frame, uint32_t len,
                                       int8_t rssi, uint16_t channel)
{
    owly_action_info action = {};
    if (!_syncLock || !_ieee80211 || !channel || channel != _ieee80211->awdlOperatingChannel() ||
        rssi >= 0 || owly_decode_action(frame, len, &action) != OWLY_ACTION_DECODED ||
        !action.complete || (frame[10] & 1)) return;

    // The family borrows a contiguous action body, excluding the 802.11 header.
    const size_t bytes = len - 24;
    mbuf_t packet = nullptr;
    unsigned segments = 1;
    if (mbuf_allocpacket(MBUF_DONTWAIT, bytes, &segments, &packet)) return;
    if (mbuf_next(packet) || mbuf_len(packet) + mbuf_trailingspace(packet) < bytes) {
        mbuf_freem(packet);
        return;
    }
    mbuf_setlen(packet, bytes);
    mbuf_pkthdr_setlen(packet, bytes);
    memcpy(mbuf_data(packet), frame + 24, bytes);
    struct Input { mbuf_t packet; alignas(8) uint8_t metadata[72]; } input = {packet, {}};
    apple80211_channel chan = {APPLE80211_VERSION, channel,
        static_cast<uint32_t>(APPLE80211_C_FLAG_20MHZ |
                              (channel > 14 ? APPLE80211_C_FLAG_5GHZ : APPLE80211_C_FLAG_2GHZ))};
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    uint32_t ts = OSSwapHostToBigInt32(static_cast<uint32_t>(ns / 1000));
    int32_t signal = rssi;
    memcpy(input.metadata, frame + 10, 6);
    memcpy(input.metadata + 12, &chan, sizeof(chan));
    memcpy(input.metadata + 44, &signal, 4);
    memcpy(input.metadata + 48, &ts, 4);
    _workLoop->runAction([](OSObject *owner, void *arg, void *, void *, void *) -> IOReturn {
        auto *self = static_cast<AirportRTW88 *>(owner);
        auto *in = static_cast<Input *>(arg);
        IOLockLock(self->_syncLock);
        auto *peer = self->_peer;
        if (peer) peer->retain();
        IOLockUnlock(self->_syncLock);
        if (!peer) return kIOReturnNotReady;
        rtw88ProcessAction(peer, in->packet, 12, in->metadata, sizeof(in->metadata));
        ++self->_peerActions;
        if (self->_peerActions == 1 || !(self->_peerActions % 64))
            self->setProperty("RTW88AWDLActionRX", self->_peerActions, 64);
        peer->release();
        return kIOReturnSuccess;
    }, this, &input);
    mbuf_freem(packet);
}

void AirportRTW88::injectRxAWDLFrame(mbuf_t m)
{
    if (!m) return;
    uint8_t dst[6];
    if (!_syncLock || mbuf_copydata(m, 0, sizeof(dst), dst)) { mbuf_freem(m); return; }
    IOLockLock(_syncLock);
    auto *vif = _peerVif;
    if (vif && ((dst[0] & 1) || !memcmp(dst, _syncMac, 6))) vif->retain();
    else vif = nullptr;
    IOLockUnlock(_syncLock);
    if (vif) {
        rtw88PushPacket(vif, m);
        vif->release();
    } else mbuf_freem(m);
}
