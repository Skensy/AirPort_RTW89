// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include "main.h"

int rtw88_program_station_mac(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
                              const uint8_t *mac)
{
    if (!hw || !hw->priv || !vif || !mac)
        return -EINVAL;
    struct rtw_dev *dev = hw->priv;
    struct rtw_vif *port = (struct rtw_vif *)vif->drv_priv;
    mutex_lock(&dev->mutex);
    memcpy(vif->addr, mac, ETH_ALEN);
    memcpy(port->mac_addr, mac, ETH_ALEN);
    rtw_vif_port_config(dev, port, PORT_SET_MAC_ADDR);
    mutex_unlock(&dev->mutex);
    return 0;
}
