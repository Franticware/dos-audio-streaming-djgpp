/* Example program how to use the routines of the soundblaster library
 * for streamed audio playback
 */

#include "sb.h"
#include <dos.h>
#include <gppconio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    const unsigned long Rate = 22050;
    if (!sb_init())
    {
        printf("Cannot init card\n");
        exit(1);
    }
    StreamStart(Rate);
    FILE* fin = fopen("brahms5.wav", "rb");
    if (fin)
    {
        // skip wav header
        fseek(fin, 44, SEEK_SET);
    }
    for (;;)
    {
        size_t len;
        unsigned char* stream = StreamBuf(&len);
        if (stream)
        {
            if (fin)
            {
                size_t bytesRead = fread(stream, 1, len, fin);
                if (bytesRead < len)
                {
                    memset(stream + bytesRead, 128, len - bytesRead);
                    fclose(fin);
                    fin = 0;
                }
            }
            else
            {
                memset(stream, 128, len);
            }
            StreamReady();
        }
        const int delay_values[4] = {25, 50, 100, 1000};
        if (kbhit())
        {
            int key = getch();
            if (key == 27)
            {
                break;
            }
            else if (key >= '1' && key <= '4')
            {
                int delay_value = delay_values[key - '1'];
                printf("not updating stream for ~%d ms\n", delay_value);
                delay(delay_value);
            }
            else
            {
                kbclear();
            }
        }
    }
    if (fin)
    {
        fclose(fin);
    }
    StreamStop();
    sb_cleanup();
    return 1;
}
