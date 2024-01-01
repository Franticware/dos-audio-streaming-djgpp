/* Example program how to use the routines of the soundblaster library
 * for playing and recording files
 */

#include <gppconio.h>
#include <stdio.h>
#include <stdlib.h>
#include "sb.h"

#define PLAY     0
#define RECORD   1

int main(int argc, char **argv)
{
    unsigned long Rate = 12000;
    int time = 2000;
    char *Data;
    int Length;
    char *Name = "test.wav";
    int i = 0;
    int mode = PLAY;

    if(!sb_init()) {
        printf("Cannot init card\n");
        exit(1);
    }
    for(i = 1; i < argc; i++) {
        if(*argv[i] == '-')
            switch(argv[i][1]) {
            case 'r' :
            case 'R' :
                mode = RECORD;
                break;
            case 'f' : 
            case 'F' :
                if(!(Rate = atoi(&(argv[i][2])))) {
                    printf("illegal recording frequency %s, using 12000\n",&(argv[i][2]));
                    Rate = 12000;
                }
                break;
            case 't' :
            case 'T' :
                if(!(time = atoi(&(argv[i][2])))) {
                    printf("illegal recording time %s ms, using 2000 ms\n",&(argv[i][2]));
                    time = 2000;
                }
                break;
            default :
                printf("illegal option %s\n",argv[i]);
                break;
            }
        else
            Name = argv[i];
    }
    if(mode == PLAY) {
        Data = get_wav_data(&Length, &Rate, Name);
        if(!Data)
            printf("cannot load file %s\n",Name);
        SoundPlay(Rate, Data, Length);
    } else {
        Length = (time*Rate)/1000;
        Data = malloc(Length);
        printf("Recording %d ms with %d Hz, = %d byte\n",time, Rate, Length);
        SoundRec(Rate, Data, Length);
        printf("Ready\n");
        put_wav_data(Data, Length, Rate, Name);
    }
    if(Data)
        free(Data);
    sb_cleanup();
    argc = argc;    /* Just to shut up the warning gcc gives... */
    return(1);
}
