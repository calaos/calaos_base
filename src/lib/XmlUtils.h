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
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * E4.4cd — the four behaviours of the old TinyXML 1 reader that pugixml does
 * not have out of the box.
 *
 * The port of the config reader/writer is otherwise a 1:1 rewrite; everything
 * that is NOT a mechanical rename lives here, so the semantics that used to be
 * implicit in TinyXML are visible and shared instead of being re-derived (or
 * forgotten) at each of the ~30 call sites. The TiXml* names quoted below are
 * that old API: the vendored TinyXML 2.5.3 + TinyXPath tree is gone since
 * E4.4e, and they are kept only to say WHY each helper exists.
 *
 * Only pugixml >= 1.10 API is used (see configure.ac): no set_value(ptr, len).
 ******************************************************************************/

#ifndef CALAOS_XMLUTILS_H
#define CALAOS_XMLUTILS_H

#include <string>
#include <pugixml.hpp>

namespace Calaos
{

namespace XmlUtils
{

/*
 * 1. Element-only traversal.
 * TiXmlNode::FirstChildElement()/NextSiblingElement() skip everything that is
 * not an element; pugixml's first_child()/next_sibling() do not. Most loops of
 * the config reader dispatch on the node name and would not notice, but two
 * would: InPlageHoraire::LoadRange() reads attributes off EVERY child without
 * looking at its name (a stray text node would become an empty TimeRange), and
 * ConditionScript::LoadFromXml() uses the null-ness of the first child as its
 * "no script here" test. The whole port goes through these two helpers so no
 * call site has to know which case it is.
 */
inline pugi::xml_node firstChildElement(const pugi::xml_node &node)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling())
    {
        if (c.type() == pugi::node_element)
            return c;
    }
    return pugi::xml_node();
}

inline pugi::xml_node nextSiblingElement(const pugi::xml_node &node)
{
    for (pugi::xml_node s = node.next_sibling(); s; s = s.next_sibling())
    {
        if (s.type() == pugi::node_element)
            return s;
    }
    return pugi::xml_node();
}

/*
 * 2. Set, not append.
 * TiXmlElement::SetAttribute() REPLACES an attribute that already exists (in
 * place, keeping its position); pugi::xml_node::append_attribute() would add a
 * second attribute with the same name. This is not theoretical:
 * InPlageHoraire::SaveToXml() writes the whole IO parameter map first and then
 * writes "months" again — and "months" IS in the parameter map, because
 * IOFactory::readParams() puts every attribute of the node there when loading.
 * A naive append would emit months= twice in every schedule IO of every config.
 */
inline void setAttribute(pugi::xml_node node, const std::string &name, const std::string &value)
{
    pugi::xml_attribute attr = node.attribute(name.c_str());
    if (!attr)
        attr = node.append_attribute(name.c_str());
    attr.set_value(value.c_str());
}

inline void setAttribute(pugi::xml_node node, const std::string &name, int value)
{
    setAttribute(node, name, std::to_string(value));
}

/*
 * 2b. What an attribute is allowed to carry, asked BEFORE the write.
 * XML 1.0 (Fifth Edition) §2.2:
 *   Char ::= #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD]
 *            | [#x10000-#x10FFFF]
 * so every C0 control but tab, line feed and carriage return is outside the
 * grammar. setAttribute() above does not refuse them: pugixml emits `&#01;`,
 * a reference to a character no conforming parser has to accept, and reads its
 * own back - the file round trips through pugixml and through nothing else.
 *
 * ⛔ This predicate does NOT belong inside setAttribute(). The writer also
 * re-records what an older Calaos wrote, and a configuration that already
 * carries such a byte must stay saveable; callers put it in front of the
 * values they are about to ACCEPT, never in front of the ones they merely
 * carry back out.
 *
 * The scan is byte wise on purpose: a C0 byte never occurs inside a multi byte
 * UTF-8 sequence, so no decoding is needed to be exact here.
 */
inline bool isWritableAsAttribute(const std::string &text)
{
    for (unsigned char c: text)
    {
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            return false;
    }
    return true;
}

//The first byte the predicate above rejects, for a message that says which.
inline int firstUnwritableByte(const std::string &text)
{
    for (unsigned char c: text)
    {
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            return c;
    }
    return -1;
}

/*
 * 3. Text and CDATA are the same thing to the callers.
 * TinyXML stored both in a TiXmlText and the config reader fished it out with
 * dynamic_cast<TiXmlText *>(node->FirstChild()); the pugixml equivalent is a
 * first child of type node_pcdata or node_cdata. hasText() reproduces the
 * null-ness of that dynamic_cast, which is what decided whether the member was
 * overwritten at all.
 *
 * The continuation loop in text() only ever matters for a payload containing
 * "]]>": TinyXML wrote such a script out verbatim and produced a file it could
 * not read back, while pugixml splits it into two valid consecutive CDATA
 * sections. Gluing them back together makes that round trip lossless; for
 * every payload without "]]>" (i.e. every existing config) there is exactly
 * one child and the result is identical to before.
 */
inline bool isTextNode(const pugi::xml_node &node)
{
    return node && (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata);
}

inline bool hasText(const pugi::xml_node &node)
{
    return isTextNode(node.first_child());
}

inline std::string text(const pugi::xml_node &node)
{
    std::string out;
    pugi::xml_node t = node.first_child();
    if (!isTextNode(t))
        return out;

    for (; isTextNode(t); t = t.next_sibling())
        out += t.value();

    return out;
}

/*
 * 4. Writing a CDATA section.
 * Replaces `new TiXmlText(s); SetCDATA(true); LinkEndChild()`.
 */
inline void appendCData(pugi::xml_node node, const std::string &value)
{
    node.append_child(pugi::node_cdata).set_value(value.c_str());
}

/*
 * 5. Document scaffolding shared by SaveConfigIO()/SaveConfigRule().
 * TiXmlDeclaration("1.0", "UTF-8", "") printed `<?xml version="1.0"
 * encoding="UTF-8" ?>`; pugixml prints the same without the space before `?>`,
 * which is what the config files on disk already contain.
 */
inline void appendDeclaration(pugi::xml_document &document)
{
    pugi::xml_node decl = document.append_child(pugi::node_declaration);
    decl.append_attribute("version").set_value("1.0");
    decl.append_attribute("encoding").set_value("UTF-8");
}

/*
 * TinyXML indented with four hard-coded spaces and that is what io.xml and
 * rules.xml look like on every existing installation; pugixml defaults to a
 * tab. Keeping four spaces keeps the reformat of the first save down to
 * attribute order and self-closing-tag spacing.
 */
constexpr const char *CONFIG_INDENT = "    ";

/*
 * 6. Parse options for the one load-MODIFY-save document (E4.4d-bis).
 * io.xml and rules.xml are rebuilt from scratch on every save, so whatever
 * TinyXML did not put in its DOM was lost there already. local_config.xml is
 * different: ConfigStore loads it, edits the <calaos:option> it was asked to
 * edit and writes the SAME document back, so everything TinyXML kept in the
 * tree survived a set_config_option(). TinyXML parsed the declaration, the
 * comments, the processing instructions and the doctype into nodes and printed
 * them back; pugixml's parse_default drops all four. Without these flags the
 * first write of a hand-edited /etc/calaos/local_config.xml would silently eat
 * its `encoding="UTF-8"` declaration and its comments.
 *
 * All four flags exist since pugixml 1.0, well below the 1.10 floor. The
 * options that would have been tempting here and are NOT usable are
 * parse_merge_pcdata (1.11) and parse_trim_pcdata (1.13).
 */
constexpr unsigned int CONFIG_PARSE_OPTIONS =
        pugi::parse_default | pugi::parse_declaration | pugi::parse_comments |
        pugi::parse_pi | pugi::parse_doctype;

}

}

#endif
