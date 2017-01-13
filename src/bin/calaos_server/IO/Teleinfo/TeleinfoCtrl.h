/******************************************************************************
 **  Copyright (c) 2006-2016, Calaos. All Rights Reserved.
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

#ifndef TELEINFOCTRL_H
#define TELEINFOCTRL_H

#include <Utils.h>
#include <Timer.h>
#include <Params.h>
#include <unordered_map>
#include <termios.h>
#include <Ecore.h>
#include <Ecore_Con.h>


namespace Calaos
{

class TeleinfoField
{
public:
    TeleinfoField(string _name, int _size, string _value) : name(_name), size(_size), value(_value){};

    string name;
    int size;
    string value;

};

class TeleinfoCtrl
{

private:
    TeleinfoCtrl(const Params &p);
    Params param;


    int serialfd = 0;
    struct termios currentTermios;
    struct termios oldTermios;

    Timer *timer = nullptr;
    Ecore_Fd_Handler *serial_handler = nullptr;
    string dataBuffer;

    void openSerial();
    void closeSerial();
    void serialError();
    void openSerialLater(double time = 2.0);

    void readNewData(const string &data);
    void processMessage(string msg);

    vector<TeleinfoField> fields = {{"ADCO", 12, ""},
                                    {"OPTARIF", 4, ""},
                                    {"ISOUSC", 2, ""},
                                    {"BASE", 9, ""},
                                    {"HCHC", 9, ""},
                                    {"HCHP", 9, ""},
                                    {"EJPHN", 9, ""},
                                    {"EJPHPM", 9, ""},
                                    {"BBRHCJB", 9, ""},
                                    {"BBRHPJB", 9, ""},
                                    {"BBRHCJW", 9, ""},
                                    {"BBRHPJW", 9, ""},
                                    {"BBRHCJR", 9, ""},
                                    {"BBRHPJR", 9, ""},
                                    {"PEJP", 2, ""},
                                    {"PTEC", 4, ""},
                                    {"DEMAIN", 4, ""},
                                    {"IINST", 3, ""},
                                    {"ADPS", 3, ""},
                                    {"IMAX", 3, ""},
                                    {"PAPP", 5, ""},
                                    {"HHPHC", 1, ""}};

    std::unordered_map<string, sigc::signal<void>> valuesCb;

public:
    static TeleinfoCtrl &Instance(const Params &p)
    {
        static TeleinfoCtrl ctrl(p);
        return ctrl;
    }
    ~TeleinfoCtrl();

    Eina_Bool _serialHandler(Ecore_Fd_Handler *handler);
    void registerIO(string teleinfoValue, sigc::slot<void> callback);
    string getValue(string teleinfoValue);
};

}

#endif // TELEINFOCTRL_H
