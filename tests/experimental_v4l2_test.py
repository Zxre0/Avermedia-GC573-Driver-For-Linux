#!/usr/bin/env python3
"""Execute exact production interval functions with synthetic V4L2 context.

This is a host-side component regression, not kernel or hardware evidence.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class ExperimentalIntervalsTest(unittest.TestCase):
    def test_actual_interval_enumerator_never_advertises_over_60_when_opted_in(self):
        source = (PROJECT / "driver/gc573_capture.c").read_text()
        functions = []
        for name in ("cap_max_fps", "cap_enum_interval"):
            match = re.search(r"^static (?:unsigned int|int) " + name +
                              r"\(.*?^}", source, re.M | re.S)
            if match is None:
                self.fail("Missing production function: " + name)
            functions.append(match.group())
        program = r'''
#include <assert.h>
#include <stddef.h>
#include <time.h>
#include <linux/videodev2.h>
#include <linux/errno.h>
#include "gc573_modes.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
struct gc573_capture { unsigned int pcie_bandwidth, experimental_144hz; };
struct file { struct gc573_capture *capture; };
static struct gc573_capture *video_drvdata(struct file *f) { return f->capture; }
'''+ "\n".join(functions) + r'''
int main(void) {
    struct gc573_capture c={16000,0};
    struct file file={&c};
    struct v4l2_frmivalenum f={.pixel_format=V4L2_PIX_FMT_BGR24,.width=2560,.height=1440};
    unsigned int i;
    assert(!cap_enum_interval(&file,0,&f));
    assert(f.discrete.numerator==1 && f.discrete.denominator==120);
    c.experimental_144hz=1;
    for (i=0;i<8;i++) {
        f.index=i;
        assert(!cap_enum_interval(&file,0,&f));
        assert(f.discrete.denominator<=60*f.discrete.numerator);
    }
    f.index=8;
    assert(cap_enum_interval(&file,0,&f)==-EINVAL);
    c.pcie_bandwidth=15999; f.index=0;
    assert(cap_enum_interval(&file,0,&f)==-EINVAL);
    f.width=1920; f.height=1080;
    assert(!cap_enum_interval(&file,0,&f));
    assert(f.discrete.numerator==1 && f.discrete.denominator==60);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="gc573-interval-component-") as temp:
            path = Path(temp)
            (path / "test.c").write_text(program)
            compiled = subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11",
                "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I" + str(PROJECT / "driver"), str(path / "test.c"),
                "-o", str(path / "test")], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
