/* User-space regression checks. Hardware and kernel callbacks are replaced below. */
#include "main.h"
#include "pci.h"
#include "rx.h"
#include "sec.h"
#include "fw.h"
#include "ps.h"

static struct rtw_dev device;
static int last_write = -1, last_clear = -1;
static unsigned wake_count, free_count;
static u8 density_at_wakeup;
static void record_wakeup(void)
{
    density_at_wakeup = device.dm_info.scan_density;
    ++wake_count;
}

#undef mutex_lock
#undef mutex_unlock
#undef dev_kfree_skb_any
#undef rtw_dbg
#define mutex_lock(m) ((void)0)
#define mutex_unlock(m) ((void)0)
#define rtw_leave_lps_deep(d) ((void)0)
#define rtw_get_lps_deep_mode(d) 0
#define rtw_fw_download_rsvd_page(d) ((void)0)
#define rtw_hci_flush_all_queues(d, drop) ((void)0)
#define rtw_mac_flush_all_queues(d, drop) ((void)0)
#define rtw_sec_write_cam(d, s, sta, key, type, idx) (last_write = (idx))
#define rtw_sec_clear_cam(d, s, idx) (last_clear = (idx))
#define rtw_dbg(...) ((void)0)
#define dev_kfree_skb_any(skb) (++free_count)
#define complete(c) record_wakeup()
#define rtw_coex_info_response(d, skb) ((void)0)
#define skb_queue_tail(q, skb) ((void)0)
#define ieee80211_queue_work(hw, work) ((void)0)

#include "audited_functions.inc"

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
static int check_rx_range(void)
{
    struct { struct rtw_rx_desc desc; u8 frame[24]; } sample = {};
    sample.desc.w0 = sizeof(sample.frame);
    CHECK(rtw_pci_rx_span_valid((const u8 *)&sample, sizeof(sample.desc), sizeof(sample)));
    CHECK(!rtw_pci_rx_span_valid((const u8 *)&sample, sizeof(sample.desc), sizeof(sample) - 1));
    sample.desc.w0 = 10;
    sample.frame[0] = 0xd4; /* ACK */
    CHECK(rtw_pci_rx_span_valid((const u8 *)&sample, sizeof(sample.desc), sizeof(sample)));
    sample.desc.w2 = RTW_RX_DESC_W2_C2H;
    sample.desc.w0 = sizeof(struct rtw_c2h_cmd);
    CHECK(rtw_pci_rx_span_valid((const u8 *)&sample, sizeof(sample.desc), sizeof(sample)));
    return 0;
}

static int check_cam(void)
{
    struct ieee80211_hw hw = {.priv = &device};
    struct ieee80211_key_conf key = {.cipher = WLAN_CIPHER_SUITE_CCMP,
                                     .flags = IEEE80211_KEY_FLAG_PAIRWISE};
    device.sec.total_cam_num = RTW_MAX_SEC_CAM_NUM;
    device.sec.cam_map[0] = ~0UL;
    CHECK(rtw_ops_set_key(&hw, SET_KEY, NULL, NULL, &key) == -ENOSPC);
    CHECK(last_write == -1);
    key.hw_key_idx = RTW_MAX_SEC_CAM_NUM - 1;
    CHECK(rtw_ops_set_key(&hw, DISABLE_KEY, NULL, NULL, &key) == 0);
    CHECK(last_clear == key.hw_key_idx);
    clear_bit(key.hw_key_idx, device.sec.cam_map);
    CHECK(rtw_ops_set_key(&hw, SET_KEY, NULL, NULL, &key) == 0);
    CHECK(last_write == last_clear && key.hw_key_idx == last_clear);
    return 0;
}

static int check_scan_result(void)
{
    u8 bytes[] = {C2H_SCAN_RESULT, 0, 42};
    struct sk_buff skb = {.data = bytes, .len = sizeof(bytes)};
    rtw_fw_c2h_cmd_rx_irqsafe(&device, 0, &skb);
    CHECK(wake_count == 1 && density_at_wakeup == 42 && free_count == 1);
    skb.len = sizeof(struct rtw_c2h_cmd);
    rtw_fw_c2h_cmd_rx_irqsafe(&device, 0, &skb);
    CHECK(wake_count == 1 && device.dm_info.scan_density == 42 && free_count == 2);
    return 0;
}
/* Only the queue decision is run here. The mbuf callbacks count ownership
 * transfers, and the checked-in release tests cover the TX path. */
struct test_entry { void *packet; u64 queuedUS; };
static unsigned _dataDropped, _dataDropExpired, _dataDropInvalidEntry, _dataDropUnreadable;
static unsigned reads, frees;
static bool copy_fails;
#define mbuf_freem(packet) (++frees)
#define mbuf_copydata(packet, offset, length, destination) (++reads, (int)copy_fails)
static bool accepted(struct test_entry entry, u64 now)
{
    do {
#include "audited_awdl.inc"
        return true;
    } while (false);
    return false;
}
static int check_awdl_accounting(void)
{
    static u8 dummy;
    struct test_entry entry = {.packet = &dummy, .queuedUS = 100};
    CHECK(accepted(entry, 1200100) && reads == 1 && frees == 0);
    CHECK(!accepted(entry, 1200101) && _dataDropExpired == 1 && reads == 1);
    CHECK(!accepted(entry, 99) && _dataDropInvalidEntry == 1 && reads == 1);
    copy_fails = true;
    CHECK(!accepted(entry, 101) && _dataDropUnreadable == 1 && reads == 2);
    entry.packet = NULL;
    CHECK(!accepted(entry, 101) && _dataDropInvalidEntry == 2 && reads == 2);
    CHECK(_dataDropped == 4 && frees == 3);
    return 0;
}
int main(void)
{
    int result = check_rx_range();
    if (!result) result = check_cam();
    if (!result) result = check_scan_result();
    if (!result) result = check_awdl_accounting();
    return result;
}
