/*
 * E4.4a -- pugixml availability smoke test.
 *
 * This is a build/link contract test, not a behaviour test of calaos code. It
 * proves three things at once, and each of them fails loudly if the wiring
 * added in configure.ac / src/lib/Makefile.am regresses:
 *
 *  1. the header is on the include path (system package or vendored copy),
 *  2. the library actually links (the DOM calls pull in real symbols),
 *  3. XPath 1.0 is compiled in.
 *
 * Point 3 is the one that matters for the TinyXPath replacement: pugixml can be
 * built with PUGIXML_NO_XPATH, and in that configuration select_node(),
 * select_nodes(), xpath_query and evaluate_*() are not even declared. So the
 * XPath test below cannot compile at all against an XPath-less build -- it is a
 * compile-time proof, not a runtime one. The #error right after the include
 * turns that into a readable diagnostic instead of a wall of "no member named
 * select_node" errors.
 */

#include <gtest/gtest.h>
#include <pugixml.hpp>

#ifdef PUGIXML_NO_XPATH
#error "pugixml is built without XPath support; E4.4 needs XPath 1.0 (see pugiconfig.hpp)"
#endif

namespace
{

const char *const kDoc =
    "<?xml version=\"1.0\"?>"
    "<root name=\"calaos\">"
    "  <a><b x=\"1\" id=\"first\"/><b x=\"2\" id=\"second\"/></a>"
    "  <a><b x=\"1\" id=\"third\"/></a>"
    "</root>";

//Takes the document by reference: xml_document is a heavy, non-copyable
//handle, no reason to lean on its move constructor here.
void loadOrDie(pugi::xml_document &doc)
{
    const pugi::xml_parse_result res = doc.load_string(kDoc);
    EXPECT_TRUE(res) << "pugixml parse error: " << res.description();
}

}

//DOM: parse a string and read an attribute.
TEST(PugiXml, ParsesStringAndReadsAttribute)
{
    pugi::xml_document doc;
    loadOrDie(doc);

    const pugi::xml_node root = doc.child("root");
    ASSERT_TRUE(root) << "root element not found";
    EXPECT_STREQ("root", root.name());
    EXPECT_STREQ("calaos", root.attribute("name").value());

    //Typed attribute access, the TinyXML QueryIntAttribute() equivalent.
    const pugi::xml_node firstB = root.child("a").child("b");
    ASSERT_TRUE(firstB);
    EXPECT_EQ(1, firstB.attribute("x").as_int());
    EXPECT_STREQ("first", firstB.attribute("id").value());

    //A missing attribute must be falsy, not a crash: this is the contract the
    //TinyXML call sites rely on today.
    EXPECT_FALSE(firstB.attribute("nope"));
    EXPECT_EQ(42, firstB.attribute("nope").as_int(42));
}

//XPath: this whole test fails to COMPILE when PUGIXML_NO_XPATH is set.
TEST(PugiXml, XPathIsEnabled)
{
    pugi::xml_document doc;
    loadOrDie(doc);

    //Single node: predicate on an attribute value.
    const pugi::xpath_node hit = doc.select_node("//b[@x='1']");
    ASSERT_TRUE(hit) << "//b[@x='1'] matched nothing";
    ASSERT_TRUE(hit.node());
    EXPECT_STREQ("first", hit.node().attribute("id").value());

    //Node set: the same predicate matches two elements across two parents,
    //in document order.
    const pugi::xpath_node_set all = doc.select_nodes("//b[@x='1']");
    ASSERT_EQ(2u, all.size());
    EXPECT_STREQ("first", all[0].node().attribute("id").value());
    EXPECT_STREQ("third", all[1].node().attribute("id").value());

    //A precompiled query returning a non-node type exercises the XPath engine
    //itself (functions + number conversion), not just the node-set path.
    const pugi::xpath_query countQuery("count(//b)");
    EXPECT_DOUBLE_EQ(3.0, countQuery.evaluate_number(doc));

    //No match must yield an empty set, not an error.
    EXPECT_EQ(0u, doc.select_nodes("//b[@x='9']").size());
}
