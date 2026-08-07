// Save-state I/O: plain stdio on the SD card (a regular FILE*), replacing
// the appfs-handle-based version this file used to have on 8bkc hardware.
#include <stdio.h>

#include "shared.h"

extern t_bitmap bitmap;
extern t_cart cart;
extern t_snd snd;
extern t_input input;

void sms_system_save_state(void *fd) {
    FILE *f = (FILE *)fd;

    /* Save VDP context */
    fwrite(&vdp, sizeof(t_vdp), 1, f);

    /* Save SMS context */
    fwrite(&sms, sizeof(t_sms), 1, f);

    /* Save Z80 context */
    fwrite(Z80_Context, sizeof(z80_t), 1, f);

    /* Save SN76489 context */
    fwrite(&sn[0], sizeof(t_SN76496), 1, f);
}

void sms_system_load_state(void *fd) {
    FILE *f = (FILE *)fd;
    int i;

    /* Initialize everything */
    cpu_reset();
    sms_system_reset();

    /* Load VDP context */
    fread(&vdp, sizeof(t_vdp), 1, f);

    /* Load SMS context */
    fread(&sms, sizeof(t_sms), 1, f);

    /* Load Z80 context */
    fread(Z80_Context, sizeof(z80_t), 1, f);

    /* Load SN76489 context */
    fread(&sn[0], sizeof(t_SN76496), 1, f);

    cpu_readmap[0] = cart.rom + 0x0000; /* 0000-3FFF */
    cpu_readmap[1] = cart.rom + 0x2000;
    cpu_readmap[2] = cart.rom + 0x4000; /* 4000-7FFF */
    cpu_readmap[3] = cart.rom + 0x6000;
    cpu_readmap[4] = cart.rom + 0x0000; /* 0000-3FFF */
    cpu_readmap[5] = cart.rom + 0x2000;
    cpu_readmap[6] = sms.ram;
    cpu_readmap[7] = sms.ram;

    cpu_writemap[0] = sms.dummy;
    cpu_writemap[1] = sms.dummy;
    cpu_writemap[2] = sms.dummy;
    cpu_writemap[3] = sms.dummy;
    cpu_writemap[4] = sms.dummy;
    cpu_writemap[5] = sms.dummy;
    cpu_writemap[6] = sms.ram;
    cpu_writemap[7] = sms.ram;

    sms_mapper_w(3, sms.fcr[3]);
    sms_mapper_w(2, sms.fcr[2]);
    sms_mapper_w(1, sms.fcr[1]);
    sms_mapper_w(0, sms.fcr[0]);

    /* Restore palette */
    for (i = 0; i < PALETTE_SIZE; i += 1)
        palette_sync(i);
}
