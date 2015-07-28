/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#ifndef S_IOFactory_H
#define S_IOFactory_H

#include <Calaos.h>
#include <Input.h>
#include <Output.h>
#include <AudioPlayer.h>
#include <IPCam.h>

namespace Calaos
{

class Registrar
{
public:
    Registrar(string type, function<Input *(Params &)> classFunc);
    Registrar(string type, function<Output *(Params &)> classFunc);
    Registrar(string type, function<AudioPlayer *(Params &)> classFunc);
    Registrar(string type, function<IPCam *(Params &)> classFunc);
};

#define REGISTER_FACTORY(NAME, TYPE, RETURNCLASS) \
static Registrar NAME##_reg_(#NAME, \
    function<RETURNCLASS *(Params &)>( \
        [](Params &_p) -> RETURNCLASS * { return new TYPE(_p); } ));

#define REGISTER_INPUT_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, Input)
#define REGISTER_INPUT(TYPE) REGISTER_INPUT_USERTYPE(TYPE, TYPE)

#define REGISTER_OUTPUT_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, Output)
#define REGISTER_OUTPUT(TYPE) REGISTER_OUTPUT_USERTYPE(TYPE, TYPE)

#define REGISTER_AUDIO_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, AudioPlayer)
#define REGISTER_AUDIO(TYPE) REGISTER_AUDIO_USERTYPE(TYPE, TYPE)

#define REGISTER_CAMERA_USERTYPE(NAME, TYPE) REGISTER_FACTORY(NAME, TYPE, IPCam)
#define REGISTER_CAMERA(TYPE) REGISTER_CAMERA_USERTYPE(TYPE, TYPE)

class IOFactory
{
private:
    IOFactory() {}

    unordered_map<string, function<Output *(Params &)>> outputFunctionRegistry;
    unordered_map<string, function<AudioPlayer *(Params &)>> audioFunctionRegistry;
    unordered_map<string, function<IPCam *(Params &)>> camFunctionRegistry;
    unordered_map<string, function<Input *(Params &)>> inputFunctionRegistry;

public:

    void readParams(TiXmlElement *node, Params &p);

    Input *CreateInput(string type, Params &params);
    Input *CreateInput(TiXmlElement *node);

    Output *CreateOutput(string type, Params &params);
    Output *CreateOutput(TiXmlElement *node);

    AudioPlayer *CreateAudio(string type, Params &params);
    AudioPlayer *CreateAudio(TiXmlElement *node);

    IPCam *CreateIPCamera(string type, Params &params);
    IPCam *CreateIPCamera(TiXmlElement *node);

    void RegisterClass(string type, function<Input *(Params &)> classFunc)
    {
        inputFunctionRegistry[type] = classFunc;
    }

    void RegisterClass(string type, function<Output *(Params &)> classFunc)
    {
        outputFunctionRegistry[type] = classFunc;
    }

    void RegisterClass(string type, function<AudioPlayer *(Params &)> classFunc)
    {
        audioFunctionRegistry[type] = classFunc;
    }

    void RegisterClass(string type, function<IPCam *(Params &)> classFunc)
    {
        camFunctionRegistry[type] = classFunc;
    }

    void genDoc(string path);

    static IOFactory &Instance()
    {
        static IOFactory inst;
        return inst;
    }
};

}

#endif
