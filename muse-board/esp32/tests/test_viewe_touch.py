"""Execute the actual VIEWE touch decoder against recorded-format packets."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest


class VieweTouchTest(unittest.TestCase):
    def test_controller_packets_and_invalid_coordinates(self):
        board_dir = Path(__file__).resolve().parents[1] / 'components/muse/boards'
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'touch.c'
            binary = Path(temp) / ('touch.exe' if os.name == 'nt' else 'touch')
            source.write_text(r'''
#include <assert.h>
#include "viewe_touch.h"
int main(void) {
    int x = -1, y = -1;
    /* CST9217: D000 block, X/Y each 12 bits, one contact. */
    uint8_t cst92[7] = {0, 14, 29, 0x91, 80, 1, 0};
    assert(viewe_touch_decode(0x5a, cst92, &x, &y) && x == 233 && y == 465);
    cst92[5] = 0;
    assert(!viewe_touch_decode(0x5a, cst92, &x, &y));
    cst92[5] = 1; cst92[3] = 0x92;  /* y=466, outside panel */
    assert(!viewe_touch_decode(0x5a, cst92, &x, &y));
    cst92[1] = 0; cst92[2] = 0; cst92[3] = 0;
    assert(viewe_touch_decode(0x5a, cst92, &x, &y) && x == 0 && y == 0);
    cst92[5] = 2;
    assert(!viewe_touch_decode(0x5a, cst92, &x, &y));
    /* CST816/820: register 02 starts with contact count. */
    uint8_t cst81[7] = {1, 0x81, 0xd1, 0x00, 0xe9, 0, 0};
    assert(viewe_touch_decode(0x15, cst81, &x, &y) && x == 465 && y == 233);
    cst81[1] = 0x41;  /* release event even if count is stale */
    assert(!viewe_touch_decode(0x15, cst81, &x, &y));
    cst81[1] = 1; cst81[2] = 0xd2;
    assert(!viewe_touch_decode(0x15, cst81, &x, &y));
    cst81[1] = 0; cst81[2] = 0; cst81[4] = 0;
    assert(viewe_touch_decode(0x15, cst81, &x, &y) && x == 0 && y == 0);
    cst81[0] = 0;
    assert(!viewe_touch_decode(0x15, cst81, &x, &y));
    assert(!viewe_touch_decode(0x22, cst81, &x, &y));
    return 0;
}
''', encoding='utf-8')
            cc = shlex.split(os.environ.get('CC', 'cc'))
            subprocess.run(cc + ['-std=c11', '-Wall', '-Wextra', '-Werror',
                                 '-I', str(board_dir), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
