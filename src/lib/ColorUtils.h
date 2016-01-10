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
#ifndef ColorValue_H
#define ColorValue_H

#include <iostream>

using namespace std;

class ColorValue
{
public:
    ColorValue();
    ColorValue(int r, int g, int b);
    ColorValue(string str_color);

    bool isValid() const { return type != ColorInvalid; }

    string toString() const;
    void setString(const string &str);

    int getAlpha() const { return alpha; }
    void setAlpha(int a);

    /* RGB functions */
    int getRed() const;
    int getGreen() const;
    int getBlue() const;

    void setRed(int red);
    void setGreen(int green);
    void setBlue(int blue);

    void setRgb(int r, int g, int b, int a = 255);

    /* HSV functions */
    int getHSVHue() const;
    int getHSVSaturation() const;
    int getHSVValue() const;

    void setHSVHue(int h);
    void setHSVSaturation(int s);
    void setHSVValue(int v);

    void setHsv(int h, int s, int v, int a = 255);

    /* HSL functions */
    int getHSLHue() const;
    int getHSLSaturation() const;
    int getHSLLightness() const;

    void setHSLHue(int h);
    void setHSLSaturation(int s);
    void setHSLLightness(int l);

    void setHsl(int h, int s, int l, int a = 255);

    ColorValue toRgb() const;
    ColorValue toHsv() const;
    ColorValue toHsl() const;

    static ColorValue fromRgb(int r, int g, int b, int a = 255);
    static ColorValue fromHsv(int h, int s, int v, int a = 255);
    static ColorValue fromHsl(int h, int s, int l, int a = 255);

    bool operator ==(const ColorValue &c) const;
    bool operator !=(const ColorValue &c) const;

private:

    enum ColorType
    {
        ColorInvalid,
        ColorRGB,
        ColorHSV,
        ColorHSL
    };

    ColorType type = ColorInvalid;

    union
    {
        struct
        {
            uint16_t r, g, b;
        } rgb;
        struct
        {
            uint16_t h, s, v;
        } hsv;
        struct
        {
            uint16_t h, s, l;
        } hsl;
        uint16_t array[3];
    } color;
    uint16_t alpha;

};

#endif
