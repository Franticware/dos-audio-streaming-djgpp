/*
 * Play and record digitized sound sample on soundblaster DAC/ADC using DMA.
 * This source code is in the public domain.
 * 
 * Modification History
 *
 *  9-Nov-93    David Baggett       Wrote it based on Sound Blaster
 *              <dmb@ai.mit.edu>    Freedom project and Linux code.
 *
 *  24-Jun-94   Gerhard Kordmann    - modified for recording facilities
 *                                  - added keyboard safety-routines to
 *                                    allow stopping of playing/recording
 *                                  - removed click while buffer switch 
 *                                  - added bugfixes by Grzegorz Jablonski
 *                                    and several safety checks
 *                                  - added free dosmem at end of program
 *              <grzegorz@kmm-lx.p.lod.edu.pl>

 *  08-Jul-94   Gerhard Kordmann    - click also removed in recording
 *                                  - changes from dosmem... to memcpy
 *  03-Sep-94   Gerhard Kordmann    - added Highspeed DMA for frequencies
 *                                    above 37000 Hz (also in sb.h)
 *                                  - removed memcpy due to new dpmi-style
 *                                    of djgpp
 *              <kordmann@ldv01.Uni-Trier.de>
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dos.h>
#include <string.h>
#include <pc.h>
#include "sb.h"


/*  GO32 DPMI structs for accessing DOS memory. */
static _go32_dpmi_seginfo dosmem;   /* DOS (conventional) memory buffer */

static _go32_dpmi_seginfo oldirq_rm;    /* original real mode IRQ */
static _go32_dpmi_registers rm_regs;
static _go32_dpmi_seginfo rm_si;    /* real mode interrupt segment info */

static _go32_dpmi_seginfo oldirq_pm;    /* original prot-mode IRQ */
static _go32_dpmi_seginfo pm_si;    /* prot-mode interrupt segment info */

/*  Card parameters  */
unsigned int    sb_ioaddr;
unsigned int    sb_irq;
unsigned int    sb_dmachan;

/* Is a sound currently playing or recorded ? */
volatile int    sb_dma_active = 0;

/* Conventional memory buffers for DMA. */
static volatile int sb_bufnum = 0;
static char     *sb_buf[2];
static unsigned int sb_buflen[2];

/* Info about current sample */
static unsigned char    *sb_curdata;    /* pointer to next bit of data */
static unsigned long    sb_curlength;   /* total length length left to play */
static int HIGHSPEED;                   /* flag for normal/highspeed DMA */


/* DMA chunk size, in bytes.
 * This parameter determines how big our DMA buffers are.  We play
 * the sample by piecing together chunks that are this big.  This
 * means that we don't have to copy the entire sample down into
 * conventional memory before playing it.  (A nice side effect of
 * this is that we can play samples that are longer than 64K.)
 *
 * Setting this is tricky.  If it's too small, we'll get lots
 * of interrupts, and slower machines might not be able to keep
 * up.  Furthermore, the smaller this is, the more grainy the
 * sound will come out.
 *
 * On the other hand, if we make it too big there will be a noticeable
 * delay between a call to sb_play and when the sound actually starts
 * playing, which is unacceptable for things like games where sound
 * effects should be "instantaneous".
 *
 */
#define DMA_CHUNK (16000)


/* Interrupt handler for recording
 *
 * This is called in both protected mode and in real mode -- this means
 * we don't have to switch modes when we service the interrupt.
 */
void sb_intr_rec(_go32_dpmi_registers *reg)
{
    register unsigned n = sb_bufnum;        /* buffer we just recorded */

    inportb(sb_ioaddr + SB_DSP_DATA_AVAIL); /* Acknowledge soundblaster */

    sb_rec_buffer(1 - n);                   /* Start next buffer recording */
    
    sb_empty_buffer(n);                     /* Save this buffer */
    
    outportb(0x20, 0x20);                   /* Acknowledge the interrupt */

    enable();
}

/* Save buffer n in sb_curdata and calculate size for next time */
void sb_empty_buffer(register unsigned n)
{
    if(sb_buflen[n] > 0) {
 		dosmemget((unsigned long) sb_buf[n], sb_buflen[n], sb_curdata);
        sb_curdata += sb_buflen[n];
        sb_curlength -= sb_buflen[n];

        /* for determination of buffersize keep in mind, that right now  */
        /*  the other buffer is recording up to DMA_CHUNK bytes. Only if */
        /*  - after saving this other buffer - there is still work to do,*/
        /*  then this buffer can take the rest in the next round         */
        if (sb_curlength > DMA_CHUNK) {
            if (sb_curlength > 2*DMA_CHUNK)
                sb_buflen[n] = DMA_CHUNK;
            else
                sb_buflen[n] = sb_curlength - DMA_CHUNK;
        }
        else 
            sb_buflen[n] = 0;
    }
}

void sb_rec_buffer(register unsigned n)
{
    int     t;
    unsigned char   im, tm;
    
    if (sb_buflen[n] == 0) {                /* See if we're already done */
        sb_dma_active = 0;
        return;
    }
    disable();

    im = inportb(0x21);                     /* Enable interrupts on PIC */
    tm = ~(1 << sb_irq);
    outportb(0x21,im & tm);

    outportb(SB_DMA_MASK, 5);               /* Set DMA mode to 'record' */
    outportb(SB_DMA_FF, 0);
    outportb(SB_DMA_MODE, 0x45);
    
    sb_bufnum = n;                          /* Set transfer address */
    t = (int) ((unsigned long) sb_buf[n] >> 16) ;
    outportb(SB_DMAPAGE + 3, t & 0xFF);
    t = (int) ((unsigned long) sb_buf[n] & 0xFFFF);
    outportb(SB_DMA + 2 * sb_dmachan, t & 0xFF);
    outportb(SB_DMA + 2 * sb_dmachan, t >> 8);
                                            /* Set transfer length byte count */
    outportb(SB_DMA + 2 * sb_dmachan + 1, (sb_buflen[n]-1) & 0xFF);
    outportb(SB_DMA + 2 * sb_dmachan + 1, (sb_buflen[n]-1) >> 8);

    outportb(SB_DMA_MASK, sb_dmachan);      /* Unmask DMA channel */

    enable();

    if(HIGHSPEED) {
	sb_writedac(SB_SET_BLOCKSIZE);  /* prepare block programming */
    }
    else {
	sb_writedac(SB_DMA_ADC);        /* command byte for DMA ADC transfer */
    }


    sb_writedac((sb_buflen[n]-1) & 0xFF);       /* sb_write length */
    sb_writedac((sb_buflen[n]-1) >> 8);

    if(HIGHSPEED)
	sb_writedac(SB_HIGH_DMA_8_BIT_ADC);  /* command byte for high speed DMA ADC transfer */

    sb_dma_active = 1;                  /* A sound is recorded now. */
}

/* Record a sample through the ADC using DMA. */
/* return number of bytes actually recorded */
unsigned long sb_rec(unsigned char *data, unsigned long length)
{
    sb_install_interrupts(sb_intr_rec); /* Install our interrupt handlers */

    sb_curdata = data;                      /* Prime the buffers */
    sb_curlength = length;
    if(length > DMA_CHUNK)
        sb_buflen[0] = DMA_CHUNK;
    else
        sb_buflen[0] = length;

    if(length <= DMA_CHUNK)
        sb_buflen[1] = 0;
    else
        if(length > 2*DMA_CHUNK)
            sb_buflen[1] = DMA_CHUNK;
        else
            sb_buflen[1] = length-DMA_CHUNK;
    
    sb_rec_buffer(0);               /* Start the first buffer recording.    */
    while (sb_dma_active)
        if(kbhit()) {               /* kbhit crashed sometimes              */
	    int rest;
            rest = sb_read_counter();       /* samples still to record      */
	    if(HIGHSPEED)
		sb_reset();                 /* writedac blocked in HS mode  */
	    else
		sb_writedac(SB_HALT_DMA);   /* stop playing                 */
            rest = sb_buflen[sb_bufnum] - rest;/* samples already recorded  */
            length -= (sb_curlength - rest);/* total samples recorded       */
            sb_buflen[sb_bufnum] = rest;    /* save those samples           */
            sb_empty_buffer(sb_bufnum);
            sb_dma_active = 0;              /* and exit the loop            */
            sb_buflen[0] = sb_buflen[1] = sb_curlength = 0; /* clean up     */
        }
    sb_cleanup_ints();                   /* remove interrupts */
    return length;
}


void sb_intr_play(_go32_dpmi_registers *reg)
{
    register unsigned n = sb_bufnum;        /* buffer we just played	*/

    inportb(sb_ioaddr + SB_DSP_DATA_AVAIL); /* Acknowledge soundblaster */
    
    sb_play_buffer(1 - n);                  /* Start next buffer player */
    
    sb_fill_buffer(n);          /* Fill this buffer for next time around */
    
    outportb(0x20, 0x20);                   /* Acknowledge the interrupt */

    enable();
}

/* Fill buffer n with the next data. */
void sb_fill_buffer(register unsigned n)
{
    if (sb_curlength > DMA_CHUNK) {
        sb_buflen[n] = DMA_CHUNK;
        dosmemput(sb_curdata, DMA_CHUNK, (unsigned long) sb_buf[n]);
        sb_curlength -= DMA_CHUNK;
        sb_curdata += DMA_CHUNK;
    }
    else if (sb_curlength <= 0) {
        sb_buflen[n] = 0;
        sb_curlength = 0;
    }
    else {
        sb_buflen[n] = sb_curlength;
        dosmemput(sb_curdata, sb_curlength, (unsigned long) sb_buf[n]);
        sb_curdata += sb_curlength;
        sb_curlength = 0;
    }
}

void sb_play_buffer(register unsigned n)
{
    int     t;
    unsigned char   im, tm;
    if (sb_buflen[n] <= 0) {                /* See if we're already done */
        sb_dma_active = 0;
        return;
    }
    disable();

    im = inportb(0x21);                     /* Enable interrupts on PIC */
    tm = ~(1 << sb_irq);
    outportb(0x21,im & tm);

    outportb(SB_DMA_MASK, 5);               /* Set DMA mode 'play' */
    outportb(SB_DMA_FF, 0);
    outportb(SB_DMA_MODE, 0x49);

    sb_bufnum = n;                          /* Set transfer address */
    t = (int) ((unsigned long) sb_buf[n] >> 16);
    outportb(SB_DMAPAGE + 3, t);
    t = (int) ((unsigned long) sb_buf[n] & 0xFFFF);
    outportb(SB_DMA + 2 * sb_dmachan, t & 0xFF);
    outportb(SB_DMA + 2 * sb_dmachan, t >> 8);
                                        /* Set transfer length byte count */
    outportb(SB_DMA + 2 * sb_dmachan + 1, (sb_buflen[n]-1) & 0xFF);
    outportb(SB_DMA + 2 * sb_dmachan + 1, (sb_buflen[n]-1) >> 8);

    outportb(SB_DMA_MASK, sb_dmachan);      /* Unmask DMA channel */

    enable();

    if(HIGHSPEED) {
	sb_writedac(SB_SET_BLOCKSIZE);      /* prepare block programming */
    }
    else {
	sb_writedac(SB_DMA_8_BIT_DAC);      /* command byte for DMA DAC transfer */
    }

    sb_writedac((sb_buflen[n]-1) & 0xFF);   /* sb_write length */
    sb_writedac((sb_buflen[n]-1) >> 8);

    if(HIGHSPEED)
	sb_writedac(SB_HIGH_DMA_8_BIT_DAC);  /* command byte for high speed DMA DAC transfer */

    sb_dma_active = 1;                       /* A sound is playing now. */
}

/* Play a sample through the DAC using DMA. */
void sb_play(unsigned char *data, unsigned long length)
{

    sb_install_interrupts(sb_intr_play);    /* Install our interrupt handlers */
    
    sb_curdata = data;                      /* Prime the buffers */
    sb_curlength = length;

    sb_fill_buffer(0);
    sb_fill_buffer(1);
    
    sb_play_buffer(0);                  /* Start the first buffer playing.	*/
    while (sb_dma_active) {             	/* keyboard overflow crashes	*/
	if(kbhit()) {                           /*  so did kbhit()              */
            if(getch() == 27) {
		if(HIGHSPEED)
		    sb_reset();                 /* writedac blocked in HS mode */
		else
            	    sb_writedac(SB_HALT_DMA);
                sb_dma_active = 0;
                sb_buflen[0] = sb_buflen[1] = sb_curlength = 0;
            }
            else
                kbclear();
        }
    }
    sb_cleanup_ints();                           /* remove interrupts */
}

/* Set sampling/playback rate.
 * Parameter is rate in Hz (samples per second).
 */
void sb_set_sample_rate(unsigned int rate)
{
    unsigned char tc;

    if(rate > 37000) HIGHSPEED = 1;
    else HIGHSPEED = 0;
    if(HIGHSPEED)
	tc = (unsigned char) ((65536 - 256000000/rate) >> 8);
    else
	tc = (unsigned char) (256 - 1000000/rate);

    sb_writedac(SB_TIME_CONSTANT);  /* Command byte for sample rate */
    sb_writedac(tc);                /* Sample rate time constant */
}

void sb_voice(int state)
{
    sb_writedac(state ? SB_SPEAKER_ON : SB_SPEAKER_OFF);
}

/* Read soundblaster card parameters from BLASTER enivronment variable .*/
void sb_getparams()
{
    char *t, *blaster;

    sb_ioaddr = 0x220;          /* Set arguments to Soundblaster defaults */
    sb_irq = 7;
    sb_dmachan = 1;

    t = getenv("BLASTER");
    if (!t)
        return;

    blaster = strdup(t);                /* Get a copy */

    t = strtok(blaster, " \t");         /* Parse the BLASTER variable */
    while (t) {
        switch (t[0]) {
            case 'A':
            case 'a':
                /* I/O address */
                sscanf(t + 1, "%x", &sb_ioaddr);
                break;
            case 'I':
            case 'i':
                /* IRQ */
                sb_irq = atoi(t + 1);
                break;
            case 'D':
            case 'd':
                /* DMA channel */
                sb_dmachan = atoi(t + 1);
                break;
            case 'T':
            case 't':
                /* what is this? */
                break;
                
            default:
                printf("Unknown BLASTER option %c\n",t[0]);
                break;
        }
        t = strtok(NULL," \t");
    }

    free(blaster);  
    return;
}

/* Init the soundblaster card. */
/* gk : modified for autodetection */
int sb_initcard(void)
{
    int i,j,error=0;
    int NrOfBases = 6;
    int Bases[] = {0x210,0x220,0x230,0x240,0x250,0x260};
    int Base;

    for(i=0;i<NrOfBases && error == 0;i++) {
        Base = Bases[i];
        outportb(Base + SB_DSP_RESET,1);

        inportb(Base + SB_DSP_RESET);              /* Kill some time */
        inportb(Base + SB_DSP_RESET);
        inportb(Base + SB_DSP_RESET);
        inportb(Base + SB_DSP_RESET);
    
        outportb(Base + SB_DSP_RESET, 0);
        for(j=0;j<100;j++) {
            if(sb_read_dac(Base) == 0xAA) {
                error = 1;
                break;
            }
        }
    }
    if(error == 0) 
        Base = 0;
    return(Base);
}

int sb_read_dac(int Base)
{
    int i;

    for(i=0;i<10000;i++) {
        if(inportb(Base + SB_DSP_DATA_AVAIL) & 0x080)
            break;
    }
    return(inportb(Base+0x0A));
}


void sb_install_interrupts(void (*sb_intr)(_go32_dpmi_registers *))
{
    sb_install_rm_interrupt(sb_intr);
    sb_install_pm_interrupt(sb_intr);
}

/*
 * Install our interrupt as the real mode interrupt handler for 
 * the IRQ the soundblaster is on.
 *
 * We accomplish this by have GO32 allocate a real mode callback for us.
 * The callback packages our protected mode code up in a real mode wrapper.
 */
void sb_install_rm_interrupt(void (*sb_intr)(_go32_dpmi_registers *))
{
    int ret;

    rm_si.pm_offset = (int) sb_intr;
    ret = _go32_dpmi_allocate_real_mode_callback_iret(&rm_si, &rm_regs);
    if (ret != 0) {
        printf("cannot allocate real mode callback, error=%04x\n",ret);
        exit(1);
    }

    disable();
    _go32_dpmi_get_real_mode_interrupt_vector(8 + sb_irq, &oldirq_rm);
    _go32_dpmi_set_real_mode_interrupt_vector(8 + sb_irq, &rm_si);
    enable();
}

/* Remove our real mode interrupt handler. */
void sb_cleanup_rm_interrupt()
{
    disable();
    _go32_dpmi_set_real_mode_interrupt_vector(8 + sb_irq, &oldirq_rm);
/* gk : added safety check */
    if(rm_si.size != -1)
        _go32_dpmi_free_real_mode_callback(&rm_si);
    rm_si.size = -1;
    enable();
}

/* Install our interrupt as the protected mode interrupt handler for 
 * the IRQ the soundblaster is on. */
void sb_install_pm_interrupt(void (*sb_intr)(_go32_dpmi_registers *))
{
    int ret;
    disable();

    pm_si.pm_offset = (int) sb_intr;
/* changes to wrap by grzegorz */
    ret = _go32_dpmi_allocate_iret_wrapper(&pm_si);
    if (ret != 0) {
        printf("cannot allocate protected mode wrapper, error=%04x\n",ret);
        exit(1);
    }

    pm_si.pm_selector = _go32_my_cs();
    _go32_dpmi_get_protected_mode_interrupt_vector(8 + sb_irq, &oldirq_pm);
    _go32_dpmi_set_protected_mode_interrupt_vector(8 + sb_irq, &pm_si);
    enable();
}

/* Remove our protected mode interrupt handler. */
void sb_cleanup_pm_interrupt()
{
    disable();
/* changes to wrap by grzegorz, safety chek by gk */
    if(pm_si.size != -1)
        _go32_dpmi_free_iret_wrapper(&pm_si);
    pm_si.size = -1;

    _go32_dpmi_set_protected_mode_interrupt_vector(8 + sb_irq, &oldirq_pm);
    enable();
}

/* Allocate conventional memory for our DMA buffers.
 * Each DMA buffer must be aligned on a 64K boundary in physical memory. */
int sb_init_buffers()
{
    dosmem.size = 65536*3/16;
    if (_go32_dpmi_allocate_dos_memory(&dosmem)) {
        printf("Unable to allocate dos memory - max size is %lu\n", dosmem.size);
		dosmem.size = -1;
        return(0);
    }

    (unsigned long) sb_buf[0] = dosmem.rm_segment * 16;
    (unsigned long) sb_buf[0] += 0x0FFFFL;
    (unsigned long) sb_buf[0] &= 0xFFFF0000L;
    (unsigned long) sb_buf[1] = (unsigned long) sb_buf[0] + 0x10000;
    return(1);
}

/* Initliaze our internal buffers and the card itself to prepare
 * for sample playing.
 *
 * Call this once per program, not once per sample. */
int sb_init()
{
    memset(&rm_regs,0,sizeof(_go32_dpmi_registers));
                        /* undefined registers cause trouble */
    rm_si.size = -1;    /* to allow safety check before free */
    pm_si.size = -1;
	dosmem.size = -1;

    sb_getparams();     /* Card card params and initialize card. */
    sb_ioaddr = sb_initcard();
    
    if(sb_ioaddr)
        /* Allocate buffers in conventional memory for double-buffering */
        if(!sb_init_buffers())
			return 0;
    return(sb_ioaddr);
}


/* Remove interrupt handlers */
void sb_cleanup_ints()
{
    /*  Remove our interrupt handlers */
    sb_cleanup_rm_interrupt();
    sb_cleanup_pm_interrupt();
}

/* leave no traces on exiting module
 * Call this at end of program or if you won't need sb functions anymore
 */
int sb_cleanup()
{
    if(dosmem.size == -1)   /* There is nothing to free */
	return(1);
    if (!_go32_dpmi_free_dos_memory(&dosmem)) {
        printf("Unable to free dos memory");
        return(0);
    }
    return(1);
}

int sb_read_counter(void)
/* tells you how many bytes DMA play/recording have still to be done */
{
   outportb(SB_DMA_FF,0);
   return(inportb(SB_DMA + 2 * sb_dmachan + 1) +
          256*inportb(SB_DMA + 2 * sb_dmachan + 1));
}


void sb_reset(void)
{
    int j;
    outportb(sb_ioaddr + SB_DSP_RESET,1);

    inportb(sb_ioaddr + SB_DSP_RESET);              /* Kill some time */
    inportb(sb_ioaddr + SB_DSP_RESET);
    inportb(sb_ioaddr + SB_DSP_RESET);
    inportb(sb_ioaddr + SB_DSP_RESET);
    
    outportb(sb_ioaddr + SB_DSP_RESET, 0);
    for(j=0;j<100;j++) {
        if(sb_read_dac(sb_ioaddr) == 0xAA) {
            break;
        }
    }
}

void kbclear(void)
{
    short buffer;

    dosmemget(0x41a,sizeof(short),&buffer);
    dosmemput(&buffer,2,0x41c);
}


/*
 * Convenient wrappers for the sound functions
 */
void SoundPlay(int Rate, char *data, unsigned long length)
{
    sb_voice(1);
    sb_set_sample_rate(Rate);
    sb_play(data, length);
    sb_voice(0);
}


unsigned long SoundRec(int Rate, char *data, unsigned long length)
{
    sb_set_sample_rate(Rate);
    return(sb_rec(data, length));
}

