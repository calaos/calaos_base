/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 ******************************************************************************/

// E4.4b -- WebCtrl::getValueXml(): TinyXPath -> pugixml XPath 1.0.
//
// getValueXml() is the only untrusted XML parse in calaos: getValue() hands it
// a document that WebCtrl downloaded from a user-configured URL. It is also
// the only site where the XPath expression itself is user config -- WebDocBase
// documents the `path` parameter as a full XPath expression, w3schools link
// included -- so predicates, functions and axes are an advertised contract,
// not an implementation detail.
//
// What these tests pin:
//  1. the expression shapes a real config uses (text(), @attr, predicates,
//     count()/string()/sum(), //, axes, unions) keep returning exactly what
//     TinyXPath::S_compute_xpath() returned. Every expected value below was
//     produced by running both engines side by side on this same document;
//     the ones that differ are called out in their own test with the reason.
//  2. a missing node / missing attribute still fails the same way: empty
//     string, no log-and-die, no exception.
//  3. an invalid expression cannot escape as an exception. pugi::xpath_query
//     *throws* xpath_exception where TinyXPath returned an error code, and
//     getValueXml() runs inside the download callback, so a leak here would
//     take the server down on a typo in the config.
//  4. malformed / truncated input is survivable. This is the reason the site
//     was migrated first: TinyXML 2.5.3 carries an assert()-turned-abort and
//     an infinite loop on truncated UTF-8.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "WebCtrl.h"

using namespace Calaos;

namespace
{

//Shaped like the weather/sensor feeds Web IOs are pointed at: values carried
//both as text nodes and as attributes, a repeated element (so predicates and
//"multiple results" mean something), and mixed content.
const char *const kDoc =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<weatherdata>\n"
    "  <location name=\"Paris\" id=\"7\">\n"
    "    <temperature unit=\"celsius\" value=\"21.5\">21.5</temperature>\n"
    "    <temperature unit=\"celsius\" value=\"18.0\">18.0</temperature>\n"
    "  </location>\n"
    "  <humidity value=\"55\">55</humidity>\n"
    "  <nested>outer<inner>in</inner>tail</nested>\n"
    "</weatherdata>\n";

class WebCtrlXPathTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        filename = "/tmp/webctrl_e44b_test.xml";
        std::ofstream f(filename);
        f << kDoc;
        f.close();
    }

    void TearDown() override
    {
        ::remove(filename.c_str());
    }

    //Helper: never let a throw out of the call under test reach gtest as a
    //crash -- the tests assert on the returned value, and a leaked exception
    //must show up as a failure, not as an aborted binary.
    string eval(const string &path)
    {
        string v;
        EXPECT_NO_THROW(v = ctrl.getValueXml(path, filename));
        return v;
    }

    string evalOn(const string &path, const string &file)
    {
        string v;
        EXPECT_NO_THROW(v = ctrl.getValueXml(path, file));
        return v;
    }

    //Default constructed: getValueXml() only needs the path and the file, it
    //never touches the download side (same trick as IOControllers_test).
    WebCtrl ctrl;
    string filename;
};

//--------------------------------------------------------------------------
// 1. The shapes real configs use. Identical under TinyXPath and pugixml.
//--------------------------------------------------------------------------

TEST_F(WebCtrlXPathTest, AbsolutePathToTextNode)
{
    EXPECT_EQ("55", eval("/weatherdata/humidity/text()"));
}

TEST_F(WebCtrlXPathTest, AttributeSelection)
{
    EXPECT_EQ("55", eval("/weatherdata/humidity/@value"));
    EXPECT_EQ("Paris", eval("/weatherdata/location/@name"));
    EXPECT_EQ("7", eval("/weatherdata/location/@id"));
}

TEST_F(WebCtrlXPathTest, DescendantSearch)
{
    EXPECT_EQ("55", eval("//humidity/text()"));
}

//"Multiple results": a node-set of more than one node stringifies to the
//first node in document order, both before and after the port.
TEST_F(WebCtrlXPathTest, MultipleResultsTakeFirstInDocumentOrder)
{
    EXPECT_EQ("21.5", eval("//temperature/@value"));
    EXPECT_EQ("21.5", eval("/weatherdata/location/temperature/text()"));
    EXPECT_EQ("21.5", eval("//humidity/text() | //temperature/text()"));
}

TEST_F(WebCtrlXPathTest, PositionalPredicate)
{
    EXPECT_EQ("21.5", eval("/weatherdata/location/temperature[1]/@value"));
    EXPECT_EQ("18.0", eval("/weatherdata/location/temperature[2]/text()"));
    EXPECT_EQ("18.0", eval("//temperature[position()=2]/@value"));
    EXPECT_EQ("18.0", eval("/weatherdata/location/temperature[last()]/@value"));
    EXPECT_EQ("55", eval("/weatherdata/*[2]/text()"));
}

TEST_F(WebCtrlXPathTest, AttributePredicate)
{
    EXPECT_EQ("21.5", eval("/weatherdata/location/temperature[@unit='celsius']/@value"));
    EXPECT_EQ("celsius", eval("/weatherdata/location/temperature[@value>20]/@unit"));
    EXPECT_EQ("18.0", eval("//temperature[@unit='celsius' and @value='18.0']/text()"));
}

TEST_F(WebCtrlXPathTest, Axes)
{
    EXPECT_EQ("21.5", eval("/weatherdata/location/child::temperature/@value"));
    EXPECT_EQ("Paris", eval("//temperature/parent::location/@name"));
    EXPECT_EQ("55", eval("//temperature/ancestor::weatherdata/humidity/text()"));
    EXPECT_EQ("18.0", eval("//temperature[1]/following-sibling::temperature/@value"));
}

//Numbers and booleans go through the XPath 1.0 number->string / boolean->
//string conversion. The values below are byte-identical to what TinyXPath's
//"%f then strip trailing zeroes" produced.
TEST_F(WebCtrlXPathTest, FunctionsReturningNumbersAndBooleans)
{
    EXPECT_EQ("2", eval("count(//temperature)"));
    EXPECT_EQ("0", eval("count(//missing)"));
    EXPECT_EQ("39.5", eval("sum(//temperature/@value)"));
    EXPECT_EQ("2", eval("string-length(/weatherdata/humidity/text())"));
    EXPECT_EQ("21", eval("floor(21.9)"));
    EXPECT_EQ("true", eval("not(//missing)"));
    EXPECT_EQ("true", eval("starts-with(//location/@name,'Par')"));
    EXPECT_EQ("true", eval("contains(//location/@name,'ari')"));
    EXPECT_EQ("true", eval("//location/@name = 'Paris'"));
}

TEST_F(WebCtrlXPathTest, StringFunctions)
{
    EXPECT_EQ("ab", eval("concat('a','b')"));
    EXPECT_EQ("5", eval("substring(/weatherdata/humidity/text(),1,1)"));
    EXPECT_EQ("zbc", eval("translate('abc','a','z')"));
    EXPECT_EQ("humidity", eval("name(/weatherdata/humidity)"));
    EXPECT_EQ("outer", eval("/weatherdata/nested/text()"));
}

//--------------------------------------------------------------------------
// 2. The deliberate behaviour changes: cases where TinyXPath was the one
//    breaking XPath 1.0 and produced an unusable value.
//--------------------------------------------------------------------------

//TinyXPath stringified an element node-set with TiXmlNode::Value(), i.e. the
//*tag name* ("humidity"), against XPath 1.0 section 4.2 which asks for the
//string-value. A config doing that got the literal "humidity" back, and
//getValueDouble() turned it into 0. pugixml returns the text, so the
//expression now does what the w3schools syntax the docs point at says.
TEST_F(WebCtrlXPathTest, ElementNodeStringifiesToItsTextNotItsTagName)
{
    EXPECT_EQ("55", eval("/weatherdata/humidity"));
    //Mixed content: string-value is the concatenation of all descendant text.
    EXPECT_EQ("outerintail", eval("/weatherdata/nested"));
    EXPECT_EQ("outerintail", eval("normalize-space(/weatherdata/nested)"));
}

//string(), number(), boolean(), local-name() and round() are core XPath 1.0
//functions that TinyXPath did not implement: it raised its internal
//execution error and returned "". They work now, which can only turn an
//empty value into a correct one.
TEST_F(WebCtrlXPathTest, CoreFunctionsTinyXPathNeverImplemented)
{
    EXPECT_EQ("55", eval("string(/weatherdata/humidity)"));
    EXPECT_EQ("55", eval("string(/weatherdata/humidity/text())"));
    EXPECT_EQ("55", eval("number(/weatherdata/humidity)"));
    EXPECT_EQ("56", eval("number(/weatherdata/humidity) + 1"));
    EXPECT_EQ("true", eval("boolean(//humidity)"));
    EXPECT_EQ("humidity", eval("local-name(/weatherdata/humidity)"));
    EXPECT_EQ("22", eval("round(21.5)"));
}

//The context node is the document element, exactly the node TinyXPath was
//handed (TiXmlDocument::RootElement()). Absolute paths are unaffected;
//relative ones, which TinyXPath resolved to nothing at all, now resolve.
TEST_F(WebCtrlXPathTest, RelativePathResolvesFromDocumentElement)
{
    EXPECT_EQ("55", eval("humidity/text()"));
    //Still empty: the document element is not its own child.
    EXPECT_EQ("", eval("weatherdata/humidity/text()"));
}

//--------------------------------------------------------------------------
// 3. Failure modes: same observable outcome as before, empty string.
//--------------------------------------------------------------------------

TEST_F(WebCtrlXPathTest, MissingNodeReturnsEmpty)
{
    EXPECT_EQ("", eval("/weatherdata/missing/text()"));
    EXPECT_EQ("", eval("//missing"));
    EXPECT_EQ("", eval("/weatherdata/location/temperature[99]/@value"));
}

TEST_F(WebCtrlXPathTest, MissingAttributeReturnsEmpty)
{
    EXPECT_EQ("", eval("//temperature/@nosuch"));
    EXPECT_EQ("", eval("//*[@id='nosuchid']"));
}

//The one that has to hold: xpath_query throws, getValueXml must not.
TEST_F(WebCtrlXPathTest, InvalidExpressionReturnsEmptyAndNeverThrows)
{
    EXPECT_EQ("", eval("/weatherdata/@@bad["));
    EXPECT_EQ("", eval("count("));
    EXPECT_EQ("", eval(""));
    EXPECT_EQ("", eval("["));
    EXPECT_EQ("", eval("//temperature[@unit='unterminated]"));
    EXPECT_EQ("", eval("nosuchfunction(1,2,3)"));
    EXPECT_EQ("", eval("/weatherdata/location/"));
    EXPECT_EQ("", eval("1 +"));
    EXPECT_EQ("", eval("..///"));
}

TEST_F(WebCtrlXPathTest, MissingFileReturnsEmpty)
{
    EXPECT_EQ("", evalOn("/weatherdata/humidity/text()",
                         "/tmp/webctrl_e44b_does_not_exist.xml"));
}

//--------------------------------------------------------------------------
// 4. Untrusted input: the reason this site moved off TinyXML 2.5.3 first.
//--------------------------------------------------------------------------

class WebCtrlXPathHostileInputTest: public ::testing::Test
{
protected:
    void TearDown() override
    {
        ::remove(filename.c_str());
    }

    string evalOnPayload(const string &payload, const string &path)
    {
        filename = "/tmp/webctrl_e44b_hostile.xml";
        std::ofstream f(filename, std::ios::binary);
        f.write(payload.data(), (std::streamsize) payload.size());
        f.close();

        string v;
        EXPECT_NO_THROW(v = ctrl.getValueXml(path, filename));
        return v;
    }

    WebCtrl ctrl;
    string filename;
};

TEST_F(WebCtrlXPathHostileInputTest, MalformedDocumentReturnsEmpty)
{
    EXPECT_EQ("", evalOnPayload("<weatherdata><humidity>55</weatherdata>",
                                "/weatherdata/humidity/text()"));
    EXPECT_EQ("", evalOnPayload("not xml at all", "/weatherdata/humidity/text()"));
    EXPECT_EQ("", evalOnPayload("", "/weatherdata/humidity/text()"));
}

//A document that parses but has no element: TinyXPath was handed
//RootElement() == NULL and dereferenced it.
TEST_F(WebCtrlXPathHostileInputTest, DocumentWithoutRootElementReturnsEmpty)
{
    EXPECT_EQ("", evalOnPayload("<!-- nothing but a comment -->",
                                "/weatherdata/humidity/text()"));
}

//Truncated UTF-8 sequence at end of input: the shape TinyXML 2.5.3 loops on
//forever. Reaching the assertion at all proves the parse terminated.
TEST_F(WebCtrlXPathHostileInputTest, TruncatedUtf8Terminates)
{
    string payload = "<weatherdata><humidity>55";
    payload += (char) 0xE2; //start of a 3-byte sequence, the rest never comes
    EXPECT_EQ("", evalOnPayload(payload, "/weatherdata/humidity/text()"));

    string lead = "<weatherdata><humidity>";
    lead += (char) 0xC3;
    EXPECT_EQ("", evalOnPayload(lead, "/weatherdata/humidity/text()"));
}

//Deeply nested input used to be the assert()/abort() lever; it must simply
//parse or simply fail.
TEST_F(WebCtrlXPathHostileInputTest, DeeplyNestedDocumentDoesNotAbort)
{
    string payload;
    const int depth = 2000;
    for (int i = 0; i < depth; i++)
        payload += "<n>";
    payload += "leaf";
    for (int i = 0; i < depth; i++)
        payload += "</n>";

    //Whether pugixml accepts this depth or rejects it is its business; not
    //aborting the process is the assertion.
    EXPECT_NO_THROW(evalOnPayload(payload, "//n/text()"));
}

}
