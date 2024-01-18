/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "ColorUtils.h"
#include "Calaos.h"

using namespace Calaos;

#define OUT_OF_RANGE(val, min, max) \
    ((val < min) || (val > max))

#define CLAMP_VALUE(val, min, max) \
    val = val < min?min:val > max?max:val

ColorValue::ColorValue():
    alpha(0)
{
    color.rgb.r = color.rgb.g = color.rgb.b = 0;
}

ColorValue::ColorValue(int r, int g, int b)
{
    setRgb(r, g, b);
}

ColorValue::ColorValue(string str_color)
{
    setString(str_color);
}

string ColorValue::toString() const
{
    stringstream s;
    if (!isValid()) return "#000000";

    if (type == ColorRGB)
    {
        s << "#";
        s << std::hex << std::setfill('0') << std::uppercase;
        s << std::setw(2) << getRed() <<
             std::setw(2) << getGreen() <<
             std::setw(2) << getBlue();
    }
    else if (type == ColorHSL)
    {
        s << "hsla(" << getHSLHue() <<
             ", " << getHSLSaturation() << "%" <<
             ", " << getHSLLightness() << "%" <<
             ", " << double(getAlpha()) / 255. <<
             ")";
    }
    else if (type == ColorHSV)
    {
        s << "hsva(" << getHSVHue() <<
             ", " << getHSVSaturation() << "%" <<
             ", " << getHSVValue() << "%" <<
             ", " << double(getAlpha()) / 255. <<
             ")";
    }

    return s.str();
}

void ColorValue::setString(const string &str)
{
    string s = Utils::str_to_lower(Utils::trim(str));

    /* Color format supported:
     * 1234   decimal value for the color with 3 RGB components
     * #RGB (each of R, G, B are a single hex digit
     * #RRGGBB
     * rgb(r, g, b)          Ex: rgb(0, 255, 0)
     * rgba(r, g, b, a)      Ex: rgba(0, 255, 0, 0.3)
     * hsl(h, s%, l%)        Ex: hsl(120, 100%, 50%)
     * hsla(h, s%, l%, a)    Ex: hsla(120, 100%, 50%, 0.3)
     * hsv(h, s%, v%)        Ex: hsv(120, 100%, 50%)
     * hsva(h, s%, v%, a)    Ex: hsva(120, 100%, 50%, 0.3)
     */

    auto parseHex = [](char c)
    {
        int h = -1;
        if (c >= '0' && c <= '9') h = c - '0';
        if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
        return h;
    };

    type = ColorInvalid;

    if (Utils::strStartsWith(s, "#"))
    {
        //Parse hex values
        if (s.length() == 4)
        {
            int r = parseHex(s[1]);
            int g = parseHex(s[2]);
            int b = parseHex(s[3]);
            setRgb((r << 4) | r,
                   (g << 4) | g,
                   (b << 4) | b);
        }
        else if (s.length() == 7)
        {
            setRgb((parseHex(s[1]) << 4) | uint8_t(parseHex(s[2])),
                   (parseHex(s[3]) << 4) | uint8_t(parseHex(s[4])),
                   (parseHex(s[5]) << 4) | uint8_t(parseHex(s[6])));
        }
    }
    else if (Utils::strStartsWith(s, "rgb(") ||
             Utils::strStartsWith(s, "rgba("))
    {
        //rgb() or rgba()
        bool needalpha = s[3] == 'a';
        Utils::replace_str(s, "rgb(", "");
        Utils::replace_str(s, "rgba(", "");
        s.erase(s.find_last_of(')'));
        Utils::replace_str(s, " ", ""); //remove any spaces

        vector<string> values;
        Utils::split(s, values, ",");

        if (values.size() == 3 ||
            values.size() == 4)
        {
            int r, g, b;
            double a = 1.0;
            Utils::from_string(values[0], r);
            Utils::from_string(values[1], g);
            Utils::from_string(values[2], b);

            if (values.size() == 4)
                Utils::from_string(values[3], a);

            if ((values.size() == 3 && !needalpha) ||
                (values.size() == 4 && needalpha))
                setRgb(r, g, b, a * 255.);
        }
    }
    else if (Utils::strStartsWith(s, "hsl(") ||
             Utils::strStartsWith(s, "hsla("))
    {
        //hsl() or hsla()
        bool needalpha = s[3] == 'a';
        Utils::replace_str(s, "hsl(", "");
        Utils::replace_str(s, "hsla(", "");
        s.erase(s.find_last_of(')'));
        Utils::replace_str(s, " ", ""); //remove any spaces
        Utils::replace_str(s, "%", ""); //remove any %

        vector<string> values;
        Utils::split(s, values, ",");

        if (values.size() == 3 ||
            values.size() == 4)
        {
            int h, ss, l;
            double a = 1.0;
            Utils::from_string(values[0], h);
            Utils::from_string(values[1], ss);
            Utils::from_string(values[2], l);

            if (values.size() == 4)
                Utils::from_string(values[3], a);

            if ((values.size() == 3 && !needalpha) ||
                (values.size() == 4 && needalpha))
                setHsl(h, ss, l, a * 255.);
        }
    }
    else if (Utils::strStartsWith(s, "hsv") ||
             Utils::strStartsWith(s, "hsva("))
    {
        //hsv() or hsva()
        bool needalpha = s[3] == 'a';
        Utils::replace_str(s, "hsv(", "");
        Utils::replace_str(s, "hsva(", "");
        s.erase(s.find_last_of(')'));
        Utils::replace_str(s, " ", ""); //remove any spaces
        Utils::replace_str(s, "%", ""); //remove any %

        vector<string> values;
        Utils::split(s, values, ",");

        if (values.size() == 3 ||
            values.size() == 4)
        {
            int h, ss, v;
            double a = 1.0;
            Utils::from_string(values[0], h);
            Utils::from_string(values[1], ss);
            Utils::from_string(values[2], v);

            if (values.size() == 4)
                Utils::from_string(values[3], a);

            if ((values.size() == 3 && !needalpha) ||
                (values.size() == 4 && needalpha))
                setHsv(h, ss, v, a * 255.);
        }
    }
    else if (Utils::is_of_type<int>(s))
    {
        //decimal number
        int dec;
        Utils::from_string(s, dec);
        setRgb(dec >> 16,
               (dec >> 8) & 0x0000FF,
               dec & 0x0000FF);
    }

    if (!isValid())
        cDebug() << "Invalid color string: " << str;
}

void ColorValue::setAlpha(int a)
{
    if (OUT_OF_RANGE(a, 0, 255))
    {
        cWarning() << "alpha out of range";
        CLAMP_VALUE(a, 0, 255);
    }
    alpha = a;
}

int ColorValue::getRed() const
{
    if (type == ColorRGB || type == ColorInvalid)
        return color.rgb.r;

    return toRgb().getRed();
}

int ColorValue::getGreen() const
{
    if (type == ColorRGB || type == ColorInvalid)
        return color.rgb.g;

    return toRgb().getGreen();
}

int ColorValue::getBlue() const
{
    if (type == ColorRGB || type == ColorInvalid)
        return color.rgb.b;

    return toRgb().getBlue();
}

void ColorValue::setRed(int red)
{
    if (OUT_OF_RANGE(red, 0, 255))
    {
        cWarning() << "red color out of range";
        CLAMP_VALUE(red, 0, 255);
    }

    if (type == ColorRGB)
        color.rgb.r = red;
    else
        setRgb(red, getGreen(), getBlue(), getAlpha());
}

void ColorValue::setGreen(int green)
{
    if (OUT_OF_RANGE(green, 0, 255))
    {
        cWarning() << "green color out of range";
        CLAMP_VALUE(green, 0, 255);
    }

    if (type == ColorRGB)
        color.rgb.g = green;
    else
        setRgb(getRed(), green, getBlue(), getAlpha());
}

void ColorValue::setBlue(int blue)
{
    if (OUT_OF_RANGE(blue, 0, 255))
    {
        cWarning() << "blue color out of range";
        CLAMP_VALUE(blue, 0, 255);
    }

    if (type == ColorRGB)
        color.rgb.b = blue;
    else
        setRgb(getRed(), getGreen(), blue, getAlpha());
}

void ColorValue::setRgb(int r, int g, int b, int a)
{
    if (OUT_OF_RANGE(r, 0, 255) ||
        OUT_OF_RANGE(g, 0, 255) ||
        OUT_OF_RANGE(b, 0, 255) ||
        OUT_OF_RANGE(a, 0, 255))
    {
        cWarning() << "rgb values out of range";
        return;
    }

    setAlpha(a);
    type = ColorRGB;
    color.rgb.r = r;
    color.rgb.g = g;
    color.rgb.b = b;
}

int ColorValue::getHSVHue() const
{
    if (type == ColorHSV || type == ColorInvalid)
        return color.hsv.h;

    return toHsv().getHSVHue();
}

int ColorValue::getHSVSaturation() const
{
    if (type == ColorHSV || type == ColorInvalid)
        return color.hsv.s;

    return toHsv().getHSVSaturation();
}

int ColorValue::getHSVValue() const
{
    if (type == ColorHSV || type == ColorInvalid)
        return color.hsv.v;

    return toHsv().getHSVValue();
}

void ColorValue::setHSVHue(int h)
{
    if (OUT_OF_RANGE(h, 0, 359))
    {
        cWarning() << "hue color out of range";
        CLAMP_VALUE(h, 0, 359);
    }

    if (type == ColorHSV)
        color.hsv.h = h;
    else
        setHsv(h, getHSVSaturation(), getHSVValue(), getAlpha());
}

void ColorValue::setHSVSaturation(int s)
{
    if (OUT_OF_RANGE(s, 0, 100))
    {
        cWarning() << "saturation color out of range";
        CLAMP_VALUE(s, 0, 100);
    }

    if (type == ColorHSV)
        color.hsv.s = s;
    else
        setHsv(getHSVHue(), s, getHSVValue(), getAlpha());
}

void ColorValue::setHSVValue(int v)
{
    if (OUT_OF_RANGE(v, 0, 100))
    {
        cWarning() << "HSV value color out of range";
        CLAMP_VALUE(v, 0, 100);
    }

    if (type == ColorHSV)
        color.hsv.v = v;
    else
        setHsv(getHSVHue(), getHSVSaturation(), v, getAlpha());
}

void ColorValue::setHsv(int h, int s, int v, int a)
{
    if (OUT_OF_RANGE(h, 0, 359) ||
        OUT_OF_RANGE(s, 0, 100) ||
        OUT_OF_RANGE(v, 0, 100) ||
        OUT_OF_RANGE(a, 0, 255))
    {
        cWarning() << "hsv values out of range";
        CLAMP_VALUE(h, 0, 359);
        CLAMP_VALUE(s, 0, 100);
        CLAMP_VALUE(v, 0, 100);
        CLAMP_VALUE(a, 0, 255);
    }

    setAlpha(a);
    type = ColorHSV;
    color.hsv.h = h;
    color.hsv.s = s;
    color.hsv.v = v;
}

int ColorValue::getHSLHue() const
{
    if (type == ColorHSL || type == ColorInvalid)
        return color.hsl.h;

    return toHsl().getHSLHue();
}

int ColorValue::getHSLSaturation() const
{
    if (type == ColorHSL || type == ColorInvalid)
        return color.hsl.s;

    return toHsl().getHSLSaturation();
}

int ColorValue::getHSLLightness() const
{
    if (type == ColorHSL || type == ColorInvalid)
        return color.hsl.l;

    return toHsl().getHSLLightness();
}

void ColorValue::setHSLHue(int h)
{
    if (OUT_OF_RANGE(h, 0, 359))
    {
        cWarning() << "hue color out of range";
        CLAMP_VALUE(h, 0, 359);
    }

    if (type == ColorHSL)
        color.hsl.h = h;
    else
        setHsl(h, getHSLSaturation(), getHSLLightness(), getAlpha());
}

void ColorValue::setHSLSaturation(int s)
{
    if (OUT_OF_RANGE(s, 0, 100))
    {
        cWarning() << "saturation color out of range";
        CLAMP_VALUE(s, 0, 100);
    }

    if (type == ColorHSL)
        color.hsl.s = s;
    else
        setHsl(getHSLHue(), s, getHSLLightness(), getAlpha());
}

void ColorValue::setHSLLightness(int l)
{
    if (OUT_OF_RANGE(l, 0, 100))
    {
        cWarning() << "lightness color out of range";
        CLAMP_VALUE(l, 0, 100);
    }

    if (type == ColorHSL)
        color.hsl.l = l;
    else
        setHsl(getHSLHue(), getHSLSaturation(), l, getAlpha());
}

void ColorValue::setHsl(int h, int s, int l, int a)
{
    if (OUT_OF_RANGE(h, 0, 359) ||
        OUT_OF_RANGE(s, 0, 100) ||
        OUT_OF_RANGE(l, 0, 100) ||
        OUT_OF_RANGE(a, 0, 255))
    {
        cWarning() << "hsl values out of range";
        CLAMP_VALUE(h, 0, 359);
        CLAMP_VALUE(s, 0, 100);
        CLAMP_VALUE(l, 0, 100);
        CLAMP_VALUE(a, 0, 255);
    }

    setAlpha(a);
    type = ColorHSL;
    color.hsl.h = h;
    color.hsl.s = s;
    color.hsl.l = l;
}

// This is a subfunction of HSLtoRGB
static void HSLtoRGB_Subfunction(unsigned int& c, const float& temp1, const float& temp2, const float& temp3)
{
    if((temp3 * 6) < 1)
        c = (unsigned int)((temp2 + (temp1 - temp2)*6*temp3)*100);
    else
        if((temp3 * 2) < 1)
            c = (unsigned int)(temp1*100);
        else
            if((temp3 * 3) < 2)
                c = (unsigned int)((temp2 + (temp1 - temp2)*(.66666 - temp3)*6)*100);
            else
                c = (unsigned int)(temp2*100);
    return;
}

ColorValue ColorValue::toRgb() const
{
    if (!isValid() || type == ColorRGB)
        return *this;

    ColorValue c;
    c.type = ColorRGB;
    c.setAlpha(getAlpha());

    if (type == ColorHSV)
    {
        int i;
        double f, p, q, t;
        double h = double(color.hsv.h);
        double s = double(color.hsv.s) / 100.;
        double v = double(color.hsv.v) / 100.;
        double r = 0, g = 0, b = 0;

        if (s == 0)
        {
            // achromatic (grey)
            int vv = (double(color.hsv.v) * 255. / 100.);
            c.setRgb(vv, vv, vv);
            return c;
        }

        h /= 60;            // sector 0 to 5
        i = floor( h );
        f = h - i;          // factorial part of h
        p = v * ( 1 - s );
        q = v * ( 1 - s * f );
        t = v * ( 1 - s * ( 1 - f ) );
        switch( i )
        {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:        // case 5:
            r = v;
            g = p;
            b = q;
            break;
        }

        r *= 100.;
        g *= 100.;
        b *= 100.;

        c.setRgb(r * 255. / 100., g * 255. / 100., b * 255. / 100.);
    }
    else if (type == ColorHSL)
    {
        unsigned int r = 0;
        unsigned int g = 0;
        unsigned int b = 0;

        float L = ((float)color.hsl.l)/100;
        float S = ((float)color.hsl.s)/100;
        float H = ((float)color.hsl.h)/360;

        if(color.hsl.s == 0)
        {
            r = color.hsl.l;
            g = color.hsl.l;
            b = color.hsl.l;
        }
        else
        {
            float temp1 = 0;
            if(L < .50)
            {
                temp1 = L*(1 + S);
            }
            else
            {
                temp1 = L + S - (L*S);
            }

            float temp2 = 2*L - temp1;

            float temp3 = 0;
            for(int i = 0 ; i < 3 ; i++)
            {
                switch(i)
                {
                case 0: // red
                    {
                        temp3 = H + .33333f;
                        if(temp3 > 1)
                            temp3 -= 1;
                        HSLtoRGB_Subfunction(r,temp1,temp2,temp3);
                        break;
                    }
                case 1: // green
                    {
                        temp3 = H;
                        HSLtoRGB_Subfunction(g,temp1,temp2,temp3);
                        break;
                    }
                case 2: // blue
                    {
                        temp3 = H - .33333f;
                        if(temp3 < 0)
                            temp3 += 1;
                        HSLtoRGB_Subfunction(b,temp1,temp2,temp3);
                        break;
                    }
                default:
                    {

                    }
                }
            }
        }
        r = (unsigned int)((((float)r)/100)*255);
        g = (unsigned int)((((float)g)/100)*255);
        b = (unsigned int)((((float)b)/100)*255);
        c.setRgb(r, g, b);
    }

    return c;
}

ColorValue ColorValue::toHsv() const
{
    if (!isValid() || type == ColorHSV)
        return *this;

    if (type != ColorRGB)
        return toRgb().toHsv();

    ColorValue c;
    c.type = ColorHSV;
    c.setAlpha(getAlpha());

    double min, max, delta;
    double r = double(color.rgb.r * 100. / 255.);
    double g = double(color.rgb.g * 100. / 255.);
    double b = double(color.rgb.b * 100. / 255.);
    double h, s, v;

    min = r < g ? r : g;
    min = min < b ? min : b;

    max = r > g ? r : g;
    max = max > b ? max : b;

    v = max;
    delta = max - min;
    if (max > 0.)
    {
        s = (delta / max) * 100.;
    }
    else
    {
        s = 0.;
        h = 0.; //undefined hue

        c.setHsv(0xFFFF, 0, v);
        return c;
    }

    if (r >= max)
        h = (g - b) / delta;
    else if(g >= max)
        h = 2.0 + (b - r) / delta;
    else
        h = 4.0 + (r - g) / delta;

    h *= 60.;

    if (h < 0.0)
        h += 360.0;

    c.setHsv(h, s, v);

    return c;
}

ColorValue ColorValue::toHsl() const
{
    if (!isValid() || type == ColorHSL)
        return *this;

    if (type != ColorRGB)
        return toRgb().toHsl();

    ColorValue c;
    c.type = ColorHSL;
    c.setAlpha(getAlpha());


    float r_percent = ((float)color.rgb.r) / 255;
    float g_percent = ((float)color.rgb.g) / 255;
    float b_percent = ((float)color.rgb.b) / 255;

    float max_color = 0;
    if((r_percent >= g_percent) && (r_percent >= b_percent))
    {
        max_color = r_percent;
    }
    if((g_percent >= r_percent) && (g_percent >= b_percent))
        max_color = g_percent;
    if((b_percent >= r_percent) && (b_percent >= g_percent))
        max_color = b_percent;

    float min_color = 0;
    if((r_percent <= g_percent) && (r_percent <= b_percent))
        min_color = r_percent;
    if((g_percent <= r_percent) && (g_percent <= b_percent))
        min_color = g_percent;
    if((b_percent <= r_percent) && (b_percent <= g_percent))
        min_color = b_percent;

    float L = 0;
    float S = 0;
    float H = 0;

    L = (max_color + min_color)/2;

    if(max_color == min_color)
    {
        S = 0;
        H = 0;
    }
    else
    {
        if(L < .50)
        {
            S = (max_color - min_color)/(max_color + min_color);
        }
        else
        {
            S = (max_color - min_color)/(2 - max_color - min_color);
        }
        if(max_color == r_percent)
        {
            H = (g_percent - b_percent)/(max_color - min_color);
        }
        if(max_color == g_percent)
        {
            H = 2 + (b_percent - r_percent)/(max_color - min_color);
        }
        if(max_color == b_percent)
        {
            H = 4 + (r_percent - g_percent)/(max_color - min_color);
        }
    }
    c.color.hsl.s = (unsigned int)(S*100);
    c.color.hsl.l = (unsigned int)(L*100);
    H = H*60;
    if(H < 0)
        H += 360;
    c.color.hsl.h = (unsigned int)H;

    return c;
}

ColorValue ColorValue::fromRgb(int r, int g, int b, int a)
{
    ColorValue c;
    c.setRgb(r, g, b, a);
    return c;
}

ColorValue ColorValue::fromHsv(int h, int s, int v, int a)
{
    ColorValue c;
    c.setHsv(h, s, v, a);
    return c;
}

ColorValue ColorValue::fromHsl(int h, int s, int l, int a)
{
    ColorValue c;
    c.setHsl(h, s, l, a);
    return c;
}

bool ColorValue::operator ==(const ColorValue &c) const
{
    if (type == c.type)
    {
        if (type == ColorHSL)
        {
            return color.hsl.h == c.color.hsl.h &&
                   color.hsl.s == c.color.hsl.s &&
                   color.hsl.l == c.color.hsl.l;
        }
        else if (type == ColorHSV)
        {
            return color.hsv.h == c.color.hsv.h &&
                   color.hsv.s == c.color.hsv.s &&
                   color.hsv.v == c.color.hsv.v;
        }
        else if (type == ColorRGB)
        {
            return color.rgb.r == c.color.rgb.r &&
                   color.rgb.g == c.color.rgb.g &&
                   color.rgb.b == c.color.rgb.b;
        }
    }

    return getRed() == c.getRed() &&
           getGreen() == c.getGreen() &&
           getBlue() == c.getBlue();
}

bool ColorValue::operator !=(const ColorValue &c) const
{
    return !operator ==(c);
}

//https://github.com/johnciech/PhilipsHueSDK/blob/master/ApplicationDesignNotes/RGB%20to%20xy%20Color%20conversion.md
//https://viereck.ch/hue-xy-rgb/

void ColorValue::toXYBrightness(double &x, double &y, double &brightness) const
{
    if (!isValid())
    {
        x = y = brightness = 0;
        return;
    }

    if (type != ColorRGB)
        return toRgb().toXYBrightness(x, y, brightness);

    if (color.rgb.r == 0 && color.rgb.g == 0 && color.rgb.b == 0)
    {
        x = y = brightness = 0;
        return;
    }

    // convert to 0-1 range
    double rr = color.rgb.r / 255.0;
    double gg = color.rgb.g / 255.0;
    double bb = color.rgb.b / 255.0;

    // Apply gamma correction
    float r = (rr > 0.04045f) ? pow((rr + 0.055f) / (1.0f + 0.055f), 2.4f) : (rr / 12.92f);
    float g = (gg > 0.04045f) ? pow((gg + 0.055f) / (1.0f + 0.055f), 2.4f) : (gg / 12.92f);
    float b = (bb > 0.04045f) ? pow((bb + 0.055f) / (1.0f + 0.055f), 2.4f) : (bb / 12.92f);

    // Wide gamut conversion D65
    float X = r * 0.649926f + g * 0.103455f + b * 0.197109f;
    float Y = r * 0.234327f + g * 0.743075f + b * 0.022598f;
    float Z = r * 0.0000000f + g * 0.053077f + b * 1.035763f;

    float cx = X / (X + Y + Z);
    float cy = Y / (X + Y + Z);

    if (std::isnan(cx)) {
        cx = 0.0f;
    }

    if (std::isnan(cy)) {
        cy = 0.0f;
    }

    x = cx;
    y = cy;

    // Brightness is the Y of XYZ
    brightness = Y;
}

ColorValue ColorValue::fromXYBrightness(double x, double y, double brightness)
{
    // Conversion de xyY vers XYZ
    double Y = brightness;
    double X = (Y / y) * x;
    double Z = (Y / y) * (1.0 - x - y);

    // sRGB D65 conversion
    double r = X  * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    double g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    double b = X  * 0.0557f - Y * 0.2040f + Z * 1.0570f;

    if (r > b && r > g && r > 1.0f) {
        // red is too big
        g = g / r;
        b = b / r;
        r = 1.0f;
    }
    else if (g > b && g > r && g > 1.0f) {
        // green is too big
        r = r / g;
        b = b / g;
        g = 1.0f;
    }
    else if (b > r && b > g && b > 1.0f) {
        // blue is too big
        r = r / b;
        g = g / b;
        b = 1.0f;
    }

    // Apply gamma correction
    r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
    g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
    b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;

    if (r > b && r > g) {
        // red is biggest
        if (r > 1.0f) {
            g = g / r;
            b = b / r;
            r = 1.0f;
        }
    }
    else if (g > b && g > r) {
        // green is biggest
        if (g > 1.0f) {
            r = r / g;
            b = b / g;
            g = 1.0f;
        }
    }
    else if (b > r && b > g) {
        // blue is biggest
        if (b > 1.0f) {
            r = r / b;
            g = g / b;
            b = 1.0f;
        }
    }

    // Conversion from linear to gamma-corrected
    int rr = static_cast<int>(r * 255.0);
    int gg = static_cast<int>(g * 255.0);
    int bb = static_cast<int>(b * 255.0);

    r = std::clamp(rr, 0, 255);
    g = std::clamp(gg, 0, 255);
    b = std::clamp(bb, 0, 255);

    return ColorValue(r, g, b);
}
