#include "sb.h"
#include <io.h>
#include <std.h>
#include <stdio.h>
#include <unistd.h>

#define memeql(s1, s2, n) (!memcmp(s1, s2, n))

char* get_wav_data(int* len, unsigned long* Rate, char* Name)
{
    int handle;
    char buffer[25];
    short shortbuf;
    char* Data = NULL;
    int i;

    if ((handle = open(Name, O_RDONLY | O_BINARY)) == -1)
        return (NULL);
    /* is it a file in "RIFF WAVE fmt" format ? */
    /*      there's 4 bytes of rLen in between, not used here */
    read(handle, buffer, 16);
    if (!(memeql(buffer, "RIFF", 4) && memeql(&(buffer[8]), "WAVEfmt ", 8)))
    {
        goto EndFunc;
    }
    read(handle, &i, 4); /* start of data from here */
    read(handle, &shortbuf, 2);
    if (shortbuf != 1)
    { /* ID for PCM-files        */
        goto EndFunc;
    }
    read(handle, &shortbuf, 2);
    if (shortbuf != 1 && shortbuf != 2)
    { /* Neither mono nor stereo */
        goto EndFunc;
    }
    read(handle, Rate, 4);
    lseek(handle, i - 8, SEEK_CUR); /* goto start of Data      */
    read(handle, buffer, 4);
    if (!memeql(buffer, "data", 4))
    { /* must be data block      */
        goto EndFunc;
    }
    read(handle, len, 4); /* actual data length      */
    Data = (char*)malloc(*len);
    if (Data)
        read(handle, Data, *len);
EndFunc:
    close(handle);
    return (Data);
}


int put_wav_data(char* data, unsigned long data_bytes, unsigned long samples, char* Name)
{
    char* riff_string;
    char* wave_string;
    char* data_string;
    char* fmt_string;
    unsigned long data_plus_header_bytes;
    unsigned long fmt_block_len;
    unsigned short Format;
    unsigned short channels;
    unsigned long bytes_per_second;
    unsigned short bytes_per_sample;
    unsigned short bits_per_sample;
    int handle;

    if ((handle = open(Name, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY)) == -1)
        return (0);

    fmt_block_len = 16;                                       /* header length                */
    Format = 1;                                               /* must be 1 (PCM) for sounds   */
    channels = 1;                                             /* only 1 (MONO) supported      */
    bytes_per_sample = 1;                                     /* as only 8-bit mono supported */
    bits_per_sample = 8;                                      /*      "                       */
    bytes_per_second = samples;                               /*      "                       */
    data_plus_header_bytes = 20 + fmt_block_len + data_bytes; /* total filelength */
    riff_string = "RIFF";
    wave_string = "WAVE";
    fmt_string = "fmt ";
    data_string = "data";
    write(handle, riff_string, 4);
    write(handle, &data_plus_header_bytes, 4);
    write(handle, wave_string, 4);
    write(handle, fmt_string, 4);
    write(handle, &fmt_block_len, 4);
    write(handle, &Format, 2);
    write(handle, &channels, 2);
    write(handle, &samples, 4);
    write(handle, &bytes_per_second, 4);
    write(handle, &bytes_per_sample, 2);
    write(handle, &bytes_per_sample, 2);
    write(handle, data_string, 4);
    write(handle, &data_bytes, 4);
    write(handle, data, data_bytes);
    close(handle);
    return (1);
}
