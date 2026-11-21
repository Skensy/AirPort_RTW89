// SPDX-License-Identifier: GPL-2.0
#include "AirportRTW88.hpp"
#include <kern/clock.h>

bool AirportRTW88::startSyncTimer()
{
    _syncLock = IOLockAlloc();
    if (!_syncLock) return false;
    auto *timer = IOTimerEventSource::timerEventSource(this, syncTick);
    if (!timer) return false;
    if (_workLoop->addEventSource(timer) != kIOReturnSuccess) {
        timer->release();
        return false;
    }
    _syncTimer = timer;
    return true;
}

void AirportRTW88::stopSyncTimer()
{
    if (_syncTimer) {
        _syncTimer->disable();
        _syncTimer->cancelTimeout();
        _workLoop->removeEventSource(_syncTimer);
        _syncTimer->release();
        _syncTimer = nullptr;
    }
    bindAwdlPeer(nullptr, nullptr);
}

void AirportRTW88::setSyncInterface(const uint8_t *mac)
{
    if (!_syncLock) return;
    IOLockLock(_syncLock);
    owly_sync_reset(&_sync);
    _rawTemplateLength = 0;
    bzero(_syncMac, sizeof(_syncMac));
    if (mac) memcpy(_syncMac, mac, sizeof(_syncMac));
    owly_sync_request(&_sync, mac != nullptr);
    IOLockUnlock(_syncLock);
    if (!mac) bindAwdlPeer(nullptr, nullptr);
}

void AirportRTW88::enableSync(bool on)
{
    if (!_syncLock) return;
    IOLockLock(_syncLock);
    owly_sync_request(&_sync, on);
    if (_syncTimer && owly_sync_configured(&_sync)) _syncTimer->setTimeoutMS(1);
    IOLockUnlock(_syncLock);
}

IOReturn AirportRTW88::syncTemplate(bool set, apple80211_awdl_sync_frame_template *data)
{
    if (!_syncLock) return kIOReturnNotReady;
    // The legacy family passes the TLVs in a separate kernel buffer.
    if ((set && (data->version != APPLE80211_VERSION || data->payload_len > OWLY_SYNC_TEMPLATE_MAX)) ||
        (data->payload && reinterpret_cast<uintptr_t>(data->payload) < 0xffffff8000000000ULL))
        return kIOReturnBadArgument;
    IOLockLock(_syncLock);
    if (!set) {
        const uint32_t capacity = data->payload_len;
        data->version = APPLE80211_VERSION;
        data->payload_len = _rawTemplateLength;
        IOReturn result = kIOReturnNotReady;
        if (_rawTemplateLength) {
            result = kIOReturnNoSpace;
            if (data->payload && capacity >= _rawTemplateLength) {
                memcpy(data->payload, _rawTemplate, _rawTemplateLength);
                result = kIOReturnSuccess;
            }
        }
        IOLockUnlock(_syncLock);
        return result;
    }
    int result = owly_sync_template(&_sync, static_cast<const uint8_t *>(data->payload),
                                    data->payload_len, _syncMac);
    if (!result) {
        _rawTemplateLength = data->payload_len;
        if (_rawTemplateLength) memcpy(_rawTemplate, data->payload, _rawTemplateLength);
        if (_syncTimer && owly_sync_configured(&_sync)) _syncTimer->setTimeoutMS(1);
    }
    IOLockUnlock(_syncLock);
    return result ? kIOReturnBadArgument : kIOReturnSuccess;
}

void AirportRTW88::syncTick(OSObject *owner, IOTimerEventSource *timer)
{
    auto *self = static_cast<AirportRTW88 *>(owner);
    uint64_t nowNs;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nowNs);
    const uint64_t now = nowNs / 1000;
    IOLockLock(self->_syncLock);
    auto &sync = self->_sync;
    const uint8_t channel = self->_ieee80211->awdlOperatingChannel();
    owly_sync_reconcile(&sync, channel != 0, channel, now);
    size_t length = 0;
    if (owly_sync_frame(&sync, now, self->_syncFrame, sizeof(self->_syncFrame), &length) == 1) {
        if (self->_ieee80211->txRawManagementFrame(self->_syncFrame, length)) {
            owly_sync_result(&sync, 0);
            if (sync.handed_off == 1 || !(sync.handed_off % 64)) {
                self->setProperty("RTW88AWDLSyncTX", sync.handed_off, 64);
                self->setProperty("RTW88AWDLChannel", channel, 32);
            }
            if (sync.handed_off == 1)
                IOLog("AirPort_RTW88: RTL8822CE AWDL sync TX on channel %u\n", channel);
        } else {
            // Temporary TX allocation failures can retry at the next deadline.
            ++sync.failed;
        }
    }
    if (owly_sync_configured(&sync)) {
        const uint32_t delay = sync.active && sync.due_us > now ?
            (uint32_t)((sync.due_us - now + 999) / 1000) : 110;
        timer->setTimeoutMS(delay);
    }
    IOLockUnlock(self->_syncLock);
}
