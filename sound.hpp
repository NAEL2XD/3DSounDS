// sound.hpp
#ifndef SOUND_HPP
#define SOUND_HPP

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <tremor/ivorbisfile.h>
#include <string>
#pragma once

class Sound {
private:
    /* data */
public:
    Sound(std::string path);
    ~Sound();

    void play(int ms = 0);
    void stop();

    int channel;
    int length;
    int time;
    bool loop;
    std::string soundPath;
};

#endif