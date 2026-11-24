/* User-space regression checks. Hardware and kernel callbacks are replaced below. */
#include "main.h"
#include "pci.h"
#include "rx.h"
#include "sec.h"
#include "fw.h"
#include "tx.h"
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

/* Exercise the actual report handlers with a controllable clock and PCI ring.
 * The status sink and free counter retain ownership evidence after callbacks. */
static unsigned long test_jiffies;
static struct sk_buff *last_completed;
static unsigned ack_count, noack_count;
static void rtw_tx_report_tx_status(struct rtw_dev *dev, struct sk_buff *skb, bool acked)
{
    last_completed = skb;
    if (acked) ++ack_count;
    else ++noack_count;
}

#undef jiffies
#define jiffies test_jiffies
#undef spin_lock_irqsave
#undef spin_unlock_irqrestore
#define spin_lock_irqsave(lock, flags) ((void)0)
#define spin_unlock_irqrestore(lock, flags) ((void)0)
#define mod_timer(timer, expires) ((void)0)
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
static struct rtw_tx_pkt_info request_report(struct sk_buff *packet)
{
    struct rtw_tx_pkt_info info = {};
    IEEE80211_SKB_CB(packet)->flags = IEEE80211_TX_CTL_REQ_TX_STATUS;
    rtw_tx_report_enable(&device, &info, packet);
    return info;
}

static void firmware_report(u8 sn, u8 status, bool v1)
{
    u8 bytes[sizeof(struct rtw_c2h_cmd) + 10] = {};
    struct sk_buff report = {.data = bytes, .len = sizeof(bytes)};
    struct rtw_c2h_cmd *cmd = (struct rtw_c2h_cmd *)bytes;
    cmd->payload[v1 ? 8 : 6] = sn;
    cmd->payload[v1 ? 9 : 0] = status;
    rtw_tx_report_handle(&device, &report, v1 ? C2H_CCX_TX_RPT + 1 : C2H_CCX_TX_RPT);
}

static int check_report_order(void)
{
    struct sk_buff first = {}, second = {};
    test_jiffies = 100;
    struct rtw_tx_pkt_info a = request_report(&first);
    struct rtw_tx_pkt_info b = request_report(&second);
    CHECK(a.report && b.report && a.sn != b.sn);
    firmware_report(a.sn, 0, true);
    firmware_report(a.sn, 0xc0, true); /* duplicate must not replace the ACK */
    firmware_report(b.sn + 4, 0, false); /* unissued */
    rtw_tx_report_enqueue(&device, &second, b.sn);
    firmware_report(b.sn, 0x40, false);
    CHECK(noack_count == 1 && last_completed == &second);
    rtw_tx_report_enqueue(&device, &first, a.sn);
    CHECK(ack_count == 1 && last_completed == &first);
    firmware_report(a.sn, 0, true);
    CHECK(!rtw88_tx_report_pending(&device) && !atomic_read(&device.tx_report.wait_depth));
    CHECK(atomic_read(&device.tx_report.diag_early_cached) == 1);
    return 0;
}

static int check_report_wrap(void)
{
    static struct sk_buff packets[66];
    struct rtw_tx_pkt_info request;
    struct rtw_tx_report *r = &device.tx_report;
    unsigned initial_free = free_count;
    test_jiffies += 1000;
    u8 first_tag = 0;
    for (unsigned i = 0; i < 64; ++i) {
        request = request_report(&packets[i]);
        CHECK(request.report);
        if (i == 0) first_tag = request.sn; /* ring still owns this skb */
        else rtw_tx_report_enqueue(&device, &packets[i], request.sn);
    }
    CHECK(rtw88_tx_report_pending(&device) == 64 && atomic_read(&r->wait_depth) == 63);
    CHECK(!request_report(&packets[64]).report);
    CHECK(!(IEEE80211_SKB_CB(&packets[64])->flags & IEEE80211_TX_CTL_REQ_TX_STATUS));
    test_jiffies += RTW_TX_PROBE_TIMEOUT;
    request = request_report(&packets[65]); /* no idle timer during this burst */
    CHECK(request.report && request.sn == first_tag && free_count == initial_free + 63);
    rtw_tx_report_enqueue(&device, &packets[0], first_tag); /* previous generation */
    CHECK(free_count == initial_free + 64 && !atomic_read(&r->wait_depth));
    rtw_tx_report_enqueue(&device, &packets[65], first_tag);
    firmware_report(first_tag, 0, true);
    CHECK(last_completed == &packets[65] && ack_count == 2 && noack_count == 1);
    return 0;
}

static int check_report_deadlines(void)
{
    struct sk_buff stalled = {}, queued = {};
    unsigned initial_free = free_count;
    test_jiffies += 1000;
    struct rtw_tx_pkt_info a = request_report(&stalled);
    firmware_report(a.sn, 0, true);
    test_jiffies += RTW_TX_PROBE_TIMEOUT;
    firmware_report(a.sn, 0x40, true);
    rtw_tx_report_enqueue(&device, &stalled, a.sn);
    CHECK(free_count == initial_free + 1 && !rtw88_tx_report_pending(&device));
    CHECK(atomic_read(&device.tx_report.diag_early_expired) == 1);
    struct rtw_tx_pkt_info b = request_report(&queued);
    rtw_tx_report_enqueue(&device, &queued, b.sn);
    rtw_tx_report_purge_timer(&device.tx_report.purge_timer);
    CHECK(atomic_read(&device.tx_report.wait_depth) == 1); /* fresh */
    rtw_tx_report_expire(&device, true); /* shutdown */
    CHECK(free_count == initial_free + 2 && !rtw88_tx_report_pending(&device));
    CHECK(ack_count == 2 && noack_count == 1);
    return 0;
}

static int check_report_load(void)
{
    static struct sk_buff packets[1500];
    struct rtw_tx_report *r = &device.tx_report;
    unsigned initial_free = free_count;
    test_jiffies += 1000;
    for (unsigned i = 0; i < ARRAY_SIZE(packets); ++i) {
        struct rtw_tx_pkt_info request = request_report(&packets[i]);
        CHECK(request.report);
        rtw_tx_report_enqueue(&device, &packets[i], request.sn);
        CHECK(atomic_read(&r->wait_depth) <= 32);
        test_jiffies += 16;
    }
    CHECK(free_count > initial_free + 1400);
    test_jiffies += RTW_TX_PROBE_TIMEOUT;
    rtw_tx_report_purge_timer(&r->purge_timer);
    CHECK(atomic_read(&r->wait_depth) == 0 && rtw88_tx_report_pending(&device) == 0);
    CHECK(free_count == initial_free + ARRAY_SIZE(packets));
    CHECK(ack_count == 2 && noack_count == 1);
    return 0;
}
int main(void)
{
    int result = check_rx_range();
    if (!result) result = check_cam();
    if (!result) result = check_scan_result();
    if (!result) result = check_awdl_accounting();
    if (!result) result = check_report_order();
    if (!result) result = check_report_wrap();
    if (!result) result = check_report_deadlines();
    if (!result) result = check_report_load();
    return result;
}
