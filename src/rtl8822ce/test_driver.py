"""Run the patched functions with the port's real C headers and in-memory I/O stubs."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
linux = root / 'rtw88-stable/drivers/net/wireless/realtek/rtw88'
feixiao = root / 'Feixiao'
tests = Path(__file__).resolve().parent
functions = {
    'tx.c': ['void rtw_tx_report_expire(', 'static void rtw_tx_report_enable(',
             'void rtw_tx_report_purge_timer(', 'void rtw_tx_report_enqueue(',
             'void rtw_tx_report_handle(', 'u32 rtw88_tx_report_pending('],
    'pci.c': ['static bool rtw_pci_rx_span_valid('],
    'sec.c': ['int rtw_sec_get_free_cam('],
    'mac80211.c': ['static int rtw_ops_set_key('],
    'fw.c': ['static void rtw_fw_scan_result(', 'void rtw_fw_c2h_cmd_rx_irqsafe('],
}
with tempfile.TemporaryDirectory(prefix='rtw88-tests-', dir='/tmp') as temp:
    out = Path(temp)
    with (out / 'audited_functions.inc').open('w') as include:
        for filename, signatures in functions.items():
            source = (linux / filename).read_text()
            for signature in signatures:
                start = source.index(signature)
                end = source.index('\n}\n', start) + 3
                include.write(source[start:end] + '\n')
    awdl = (feixiao / 'src/kext/RTW88AWDLManager.cpp').read_text()
    start = awdl.index('        const bool invalid = !entry.packet || now < entry.queuedUS;')
    end = awdl.index('        bool ready = false;', start)
    (out / 'audited_awdl.inc').write_text(awdl[start:end])
    makefile = out / 'host.mk'
    makefile.write_text(
        'include Makefile\n'
        'audit-test:\n'
        '\t$(CC) $(DRIVER_CFLAGS) -O1 -Wno-shorten-64-to-32 -I$(AUDIT_TEMP) '
        '-c $(AUDIT_TEST) -o $(AUDIT_TEMP)/test.o\n'
        '\t$(CC) -arch x86_64 $(AUDIT_TEMP)/test.o -o $(AUDIT_TEMP)/test\n'
    )
    subprocess.run(['make', '-s', '-C', str(feixiao), '-f', str(makefile), 'audit-test',
                    f'AUDIT_TEMP={out}', f'AUDIT_TEST={tests / "test_driver.c"}'], check=True)
    subprocess.run([out / 'test'], check=True)
    print('PASS CCX feedback expiry, bounded tags, rollover, PCI/C2H reordering and explicit ACK/no-ACK')
    print('PASS RX DMA range, CAM slot selection/removal, scan notification order, AWDL drop reasons and ownership')
