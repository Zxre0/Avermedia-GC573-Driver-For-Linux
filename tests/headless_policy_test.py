#!/usr/bin/env python3
"""Execute production combined-mode policy with synthetic MMIO/protocol edges.
This is component evidence only, never kernel/device capture evidence.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

PROJECT=Path(__file__).resolve().parents[1]


class HeadlessPolicyTest(unittest.TestCase):
    def test_opt_in_reaches_input_ready_without_an_external_display(self):
        source=(PROJECT/'driver/gc573_pci.c').read_text()
        match=re.search(r'^static int gc573_combined_poll\(.*?^}',source,re.M|re.S)
        if match is None:
            self.fail('Missing production combined poll')
        program=r'''
#include <assert.h>
#include <stdbool.h>
#include <linux/errno.h>
#include "gc573_hdmi.h"
#include "gc573_passthrough.h"
#include "gc573_modes.h"
#define smp_store_release(p,v) (*(p)=(v))
struct gc573_device {
    struct gc573_passthrough_state external;
    struct gc573_hdmi hdmi;
    struct gc573_block_io hdmi_io;
    unsigned int combined_changes,combined_phase,combined_format_waits,combined_period;
    int combined_error;
    bool hdmi_ready;
};
static bool experimental_144hz;
static unsigned int external_calls,internal_calls,receiver_calls;
struct sim { unsigned int regs[1100]; };
static unsigned int read_reg(void *ctx,unsigned int offset) {return ((struct sim*)ctx)->regs[offset/4];}
static void write_reg(void *ctx,unsigned int offset,unsigned int value) {((struct sim*)ctx)->regs[offset/4]=value;}
static int fake_external(const struct gc573_block_io *io,struct gc573_passthrough_state *p) {
    external_calls++; p->stable=0; p->waiting=1; return 0;
}
static int fake_internal(const struct gc573_block_io *io,struct gc573_splitter_result *id,
    struct gc573_splitter_link_result *link,struct gc573_splitter_video_result *r,unsigned int opt) {
    assert(opt==1); internal_calls++; return 0;
}
static int fake_receiver(const struct gc573_block_io *io,const struct gc573_signal_result *s,
    struct gc573_receiver_video_result *r,unsigned int enable) {
    assert(enable==3); receiver_calls++; r->width=2560; r->height=1440;
    r->pixel_max_khz=590000; return 0;
}
#define gc573_passthrough_poll fake_external
#define gc573_splitter_video_internal_profile fake_internal
#define gc573_receiver_video fake_receiver
#define gc573_splitter_video_link_wait(r,ret) 0
#define gc573_splitter_video_format_wait(r,ret) 0
#define gc573_receiver_video_retryable(r,ret) 0
'''+match.group()+r'''
int main(void) {
    struct sim sim={0};
    struct gc573_device card={0};
    card.hdmi_io=(struct gc573_block_io){.ctx=&sim,.read=read_reg,.write=write_reg};
    sim.regs[0x1004/4]=1; sim.regs[0x1008/4]=640;
    sim.regs[0x100c/4]=1440; sim.regs[0x1010/4]=694444;
    /* The legacy combined profile retains its real-display dependency. */
    assert(!gc573_combined_poll(&card));
    assert(external_calls==1 && !internal_calls && !card.hdmi_ready);
    experimental_144hz=true;
    for (unsigned int i=0;i<4;i++) assert(!gc573_combined_poll(&card));
    assert(external_calls==1); /* No monitor probe, EDID rewrite or TX2 setup. */
    assert(internal_calls==1 && receiver_calls==1 && card.hdmi_ready);
    assert(card.combined_phase==3 && card.combined_period==694444);
    struct gc573_device ready_card=card;
    sim.regs[0x1010/4]=719424; /* ~139 Hz: inside the old 10% drift band. */
    assert(!gc573_combined_poll(&card) && !card.hdmi_ready && !card.combined_phase);
    card=ready_card; sim.regs[0x1010/4]=694444;
    /* A mode/rate change invalidates capture readiness. */
    sim.regs[0x1008/4]=480;
    assert(!gc573_combined_poll(&card) && !card.hdmi_ready && !card.combined_phase);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='gc573-headless-policy-') as temp:
            path=Path(temp)
            (path/'test.c').write_text(program)
            compiled=subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra',
                '-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',
                '-fno-omit-frame-pointer','-I'+str(PROJECT/'driver'),str(path/'test.c'),
                '-o',str(path/'test')],capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            result=subprocess.run([str(path/'test')],capture_output=True,text=True,cwd=temp)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':
    unittest.main()
