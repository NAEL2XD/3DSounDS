# LIBRARY FOR PLAYING SOUNDS ON THE 3DS WITHOUT NEEDING BILLIONS OF CODE

usage:
1. in your makefile in the libs variable, add this:
```
`$(PREFIX)pkg-config vorbisidec --libs`
```
2. place your sound cpp and hpp in source
3. use this template
```cpp
#include <3ds.h>
#include "sound.hpp" // Include this library

int main() {
    gfxInitDefault(); // ditto
    romfsInit(); // init this
    ndspInit(); // this aswell
    consoleInit(GFX_TOP, NULL); // initialize the console to top so that it's ready to display logs

    Sound* sound = new Sound("output.ogg"); // NOTE!!! do not include romfs:/ cause it's already included in the sound folder
    sound->play(); // Play the sound, will run in a thread so that it won't stop for no reason.
    
    while(aptMainLoop()) {
        hidScanInput();
        if(hidKeysDown() & KEY_START) break;

        printf("Time: %d | Length: %d\n", sound->time, sound->length);
    }
    
    delete sound; // delete the sound if ONLY if you're deinitializing stuff.
    ndspExit(); // exit all of these services
    romfsExit();
    gfxExit();
    return 0;
}
```

YOU MUST HAVE THE TREMOR IVORBISCODEC LIBRARY!!!!
