#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(linux)
#   include <sys/io.h>
#else
#   include <io.h>
#endif

#include "fm_radio.h"
#include "audio.h"


int main(int argc, char **argv)
{
    static unsigned char IQ[SAMPLES*4];
    static int left_audio[AUDIO_SAMPLES];
    static int right_audio[AUDIO_SAMPLES];

    if ( argc < 2 )
    {
        printf("Missing input file.\n");
        return -1;
    }
    
    // initialize the audio output
    //int audio_fd = audio_init( AUDIO_RATE );
    int audio_fd = open("test/usrp.out", O_WRONLY);
    if ( audio_fd < 0 )
    {
        printf("Failed to initialize audio!\n");
        return -1;
    }

    FILE * usrp_file = fopen(argv[1], "rb");
    if ( usrp_file == NULL )
    {
        printf("Unable to open file.\n");
        return -1;
    }    
    
    // run the FM receiver 
    //while( !feof(usrp_file) )
    //{
        // get I/Q from data file
        fread( IQ, sizeof(char), SAMPLES*4, usrp_file );
        int input_fd = open("in.dat", O_WRONLY);
        write(input_fd, IQ, SAMPLES*4);

        // fm radio in mono
        fm_radio_stereo( IQ, left_audio, right_audio );

        // write to audio output
        audio_tx( audio_fd, AUDIO_RATE, left_audio, right_audio, AUDIO_SAMPLES );
    //}

    fclose( usrp_file );
    close( audio_fd );

    return 0;
}

