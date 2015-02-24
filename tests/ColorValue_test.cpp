#include "ColorUtils.h"
#include "gtest/gtest.h"

class ColorValueTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        c0 = new ColorValue();
        c1 = new ColorValue(255, 170, 80);
    }

    virtual void TearDown()
    {
        delete c0;
        delete c1;
    }

    ColorValue *c0;
    ColorValue *c1;

};

TEST_F(ColorValueTest, IsValid)
{
    EXPECT_EQ(false, c0->isValid());
    EXPECT_EQ(true, c1->isValid());
}

TEST_F(ColorValueTest, ToString)
{
    EXPECT_EQ("#000000", c0->toString());
    EXPECT_EQ("#FFAA50", c1->toString());
}

TEST_F(ColorValueTest, IsColorValid)
{
    EXPECT_EQ(0, c0->getRed());
    EXPECT_EQ(0, c0->getGreen());
    EXPECT_EQ(0, c0->getBlue());

    EXPECT_EQ(255, c1->getRed());
    EXPECT_EQ(170, c1->getGreen());
    EXPECT_EQ(80, c1->getBlue());
}

TEST_F(ColorValueTest, ChangeRGB)
{
    c0->setRed(500);
    EXPECT_EQ(false, c0->isValid());

    c0->setRed(255);
    EXPECT_EQ(255, c0->getRed());
    EXPECT_EQ(true, c0->isValid());
    c0->setGreen(170);
    EXPECT_EQ(170, c0->getGreen());
    EXPECT_EQ(true, c0->isValid());
    c0->setBlue(80);
    EXPECT_EQ(80, c0->getBlue());
    EXPECT_EQ(true, c0->isValid());

    EXPECT_EQ(true, *c0 == *c1);
}

TEST_F(ColorValueTest, SetRGB)
{
    c0->setRgb(255, 170, 80);
    EXPECT_EQ(true, c0->isValid());

    EXPECT_EQ(true, *c0 == *c1);
}

TEST_F(ColorValueTest, FromRGB)
{
    EXPECT_EQ(true, ColorValue::fromRgb(255, 170, 80) == *c1);
}

TEST_F(ColorValueTest, HSVConvert)
{
    EXPECT_EQ(30, c1->getHSVHue());
    EXPECT_EQ(68, c1->getHSVSaturation());
    EXPECT_EQ(100, c1->getHSVValue());
}

TEST_F(ColorValueTest, HSLConvert)
{
    EXPECT_EQ(30, c1->getHSLHue());
    EXPECT_EQ(100, c1->getHSLSaturation());
    EXPECT_EQ(65, c1->getHSLLightness());
}

TEST_F(ColorValueTest, RGBConvert)
{
    ColorValue hsv = ColorValue::fromHsv(30, 68, 100);
    ColorValue hsl = ColorValue::fromHsl(30, 100, 65);

    EXPECT_EQ(255, hsv.getRed());
    EXPECT_EQ(168, hsv.getGreen());
    EXPECT_EQ(81, hsv.getBlue());

    EXPECT_EQ(255, hsl.getRed());
    EXPECT_EQ(165, hsl.getGreen());
    EXPECT_EQ(73, hsl.getBlue());
}

TEST_F(ColorValueTest, SetString)
{
    ColorValue s1("#");
    ColorValue s2("#Ff0000");
    ColorValue s3("#f00");
    ColorValue s4("#FFAA50");
    ColorValue s5("16755280"); //0xFFAA50 in decimal
    ColorValue s6("rgb(4,5)");
    ColorValue s7("hsl(a-s)");
    ColorValue s8("hsv(test)");

    EXPECT_EQ(false, s1.isValid());
    s1.setString("abc");
    EXPECT_EQ(false, s1.isValid());
    s1.setString("#xyz");
    EXPECT_EQ(false, s1.isValid());
    EXPECT_EQ(true, s2.isValid());
    EXPECT_EQ(true, s3.isValid());
    EXPECT_EQ(true, s4.isValid());
    EXPECT_EQ(true, s5.isValid());

    EXPECT_EQ(255, s2.getRed());
    EXPECT_EQ(0, s2.getGreen());
    EXPECT_EQ(0, s2.getBlue());

    EXPECT_EQ(255, s3.getRed());
    EXPECT_EQ(0, s3.getGreen());
    EXPECT_EQ(0, s3.getBlue());

    EXPECT_EQ(255, s4.getRed());
    EXPECT_EQ(170, s4.getGreen());
    EXPECT_EQ(80, s4.getBlue());

    EXPECT_EQ(255, s5.getRed());
    EXPECT_EQ(170, s5.getGreen());
    EXPECT_EQ(80, s5.getBlue());

    EXPECT_EQ(true, s2 == s3);
    EXPECT_EQ(true, s4 == *c1);
    EXPECT_EQ(true, s4 == s5);

    EXPECT_EQ(false, s6.isValid());
    EXPECT_EQ(false, s7.isValid());
    EXPECT_EQ(false, s8.isValid());

    s6.setString("rgb(255, 170, 80)");
    EXPECT_EQ(true, s6.isValid());
    EXPECT_EQ(255, s6.getRed());
    EXPECT_EQ(170, s6.getGreen());
    EXPECT_EQ(80, s6.getBlue());
    EXPECT_EQ(255, s6.getAlpha());

    s6.setString("RgBa(45, 12, 180, 0.2)");
    EXPECT_EQ(true, s6.isValid());
    EXPECT_EQ(45, s6.getRed());
    EXPECT_EQ(12, s6.getGreen());
    EXPECT_EQ(180, s6.getBlue());
    EXPECT_EQ(51, s6.getAlpha());

    s6.setString("rgb(0,0,0,1.0)");
    EXPECT_EQ(false, s6.isValid());

    s7.setString("hsl(120, 60%, 70%);");
    EXPECT_EQ(true, s7.isValid());
    EXPECT_EQ(120, s7.getHSLHue());
    EXPECT_EQ(60, s7.getHSLSaturation());
    EXPECT_EQ(70, s7.getHSLLightness());
    EXPECT_EQ(255, s7.getAlpha());

    s7.setString("hsla(342, 12%, 84%, 0.2);");
    EXPECT_EQ(true, s7.isValid());
    EXPECT_EQ(342, s7.getHSLHue());
    EXPECT_EQ(12, s7.getHSLSaturation());
    EXPECT_EQ(84, s7.getHSLLightness());
    EXPECT_EQ(51, s7.getAlpha());

    s7.setString("hsl(0,0,0,1.0)");
    EXPECT_EQ(false, s7.isValid());

    s8.setString("hsv(120, 60%, 70%);");
    EXPECT_EQ(true, s8.isValid());
    EXPECT_EQ(120, s8.getHSVHue());
    EXPECT_EQ(60, s8.getHSVSaturation());
    EXPECT_EQ(70, s8.getHSVValue());
    EXPECT_EQ(255, s8.getAlpha());

    s8.setString("hsva(342, 12%, 84%, 0.2);");
    EXPECT_EQ(true, s8.isValid());
    EXPECT_EQ(342, s8.getHSVHue());
    EXPECT_EQ(12, s8.getHSVSaturation());
    EXPECT_EQ(84, s8.getHSVValue());
    EXPECT_EQ(51, s8.getAlpha());

    s8.setString("hsv(0,0,0,1.0)");
    EXPECT_EQ(false, s8.isValid());
}

