/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "WebCtrl.h"

//E4.4b: the XML branch runs on pugixml (DOM + native XPath 1.0). This is the
//only place in calaos that parses *untrusted* XML -- getValue() feeds it a
//document downloaded from a user-configured URL -- so it is the site that had
//to leave TinyXML 2.5.3 first (assert()-on-malformed-input, and an infinite
//loop on truncated UTF-8). TinyXML/TinyXPath were then removed from the tree
//entirely in E4.4e; the names below describe the engine this code replaced.
#include <pugixml.hpp>

//E4.4a pinned the floor at pugixml >= 1.10 (Debian 12 ships 1.13, the vendored
//copy is 1.14). Everything used below -- load_file(), document_element(),
//xpath_query, xpath_query::result(), evaluate_string(), xpath_exception -- is
//original 1.0 API, but the guard makes a too-old system package a compile
//error instead of a link-time surprise on the non-vendored build.
#if defined(PUGIXML_VERSION) && PUGIXML_VERSION < 1100
#error "pugixml >= 1.10 is required (see configure.ac / docs/refactoring/E4.4b.md)"
#endif


using namespace Calaos;

unordered_map<string, WebCtrl> WebCtrl::hash;

WebCtrl::WebCtrl()
{
    //Keep members sane: a default constructed WebCtrl (unordered_map
    //value type) would otherwise delete an uninitialized timer pointer.
    timer = NULL;
    frequency = 0.0;
    file_type = UNKNOWN;
}

WebCtrl::WebCtrl(Params &p, int _file_type)
{
    timer = NULL;
    param = p;
    frequency = 0.0;
    file_type = _file_type;
}

WebCtrl::~WebCtrl()
{
    if (timer)
        delete timer;
}


WebCtrl &WebCtrl::Instance(Params &p)
{
    string url = p.get_param("url");

    if (hash.find(url) == hash.end())
    {
        string str_file_type = p.get_param("file_type");
        int file_type;

        if (str_file_type == "xml" || str_file_type == "XML")
            file_type = WebCtrl::XML;
        else if (str_file_type == "json" || str_file_type == "JSON")
            file_type = WebCtrl::JSON;
        else if (str_file_type == "text" || str_file_type == "TEXT")
            file_type = WebCtrl::TEXT;
        else
            file_type = WebCtrl::UNKNOWN;

        hash[url] = WebCtrl(p, file_type);
    }

    return hash[url];
}


void WebCtrl::Add(string path,
                  double _frequency,
                  std::function<void()> fileDownloaded_cb)
{

    if (!_frequency)
    {
       cError() << "The frequency parameter is NULL, please set a real value.";
       return;
    }

    fileDownloadedCallbacks.push_back(std::make_pair(path, fileDownloaded_cb));
    if (!frequency || frequency > _frequency )
        frequency = _frequency;

    if (!timer)
        timer = new Timer(frequency, [=]() {
            launchDownload();
        });
    else
        timer->Reset(frequency);

    launchDownload();
}

void WebCtrl::Del(string path)
{
    for(unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
    {
        if (fileDownloadedCallbacks[i].first == path)
            fileDownloadedCallbacks.erase(fileDownloadedCallbacks.begin() + i);
    }
}

void WebCtrl::launchDownload()
{
    string filename = "/tmp/calaos_" + param.get_param("id") + ".part";
    string u = param.get_param("url");

    if (Utils::strStartsWith(u, "http://") ||
        Utils::strStartsWith(u, "https://"))
    {
        UrlDownloader *dl = new UrlDownloader(param.get_param("url"), true);
        //T2.19: user-configured URL — insecure unless the IO opts in to
        //hardening with insecure="false"
        dl->setInsecureFromParam(param.get_param("insecure"));
        dl->httpGet(filename);
        dl->m_signalComplete.connect([=](int status)
        {
            string dest =  "/tmp/calaos_" + param.get_param("id");
            string src = dest + ".part";
            FileUtils::rename(src, dest);
            for (unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
            {
                fileDownloadedCallbacks[i].second();
            }
        });
    }
    else
    {
        for (unsigned int i = 0; i < fileDownloadedCallbacks.size(); i++)
        {
            fileDownloadedCallbacks[i].second();
        }
    }
}

string WebCtrl::getValueJson(string path, string filename)
{
    string value;

    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        cWarning() << "Failed to open WebCtrl file: " << filename;
        return string();
    }

    Json root;
    try
    {
        root = Json::parse(ifs);
    }
    catch (const std::exception &e)
    {
        cWarning() << "Error parsing " << filename << ":" << e.what();
        return string();
    }

    vector<string> tokens;
    Utils::split(path, tokens, "/");

    if (!tokens.empty())
    {
        Json parent = root;
        for (auto it = tokens.begin();it != tokens.end();it++)
        {
            string val = *it;

            // Test if the token is an array index
            // if it's the case, it must be something like [x]
            if (val[0] == '[')
            {
                /* T3.35, corrected by T3.35b. A well formed index token is
                 * "[n]" - an opening bracket AND a closing one. erase() and
                 * pop_back() below strip the first and the last character
                 * UNCONDITIONALLY, so what has to be checked here is the FORM.
                 * A guard on the LENGTH alone (the T3.35 shape, `val.size() <
                 * 2`) covered only half of it and the message it printed was
                 * not true of the code that printed it:
                 *
                 *  - a lone '[' was EMPTIED by erase(), pop_back() then
                 *    underflowed the size_t length of the string, and the read
                 *    that followed escaped getValueJson() as a std::bad_alloc.
                 *    Nothing caught it anywhere up to main() and calaos_server
                 *    terminated. There is no remote vector - a `path` is only
                 *    ever written by calaos_installer - but a typo was enough
                 *    to bring the server down. A length guard does stop that.
                 *
                 *  - "[5" and "[12" are two characters or more, so a length
                 *    guard let them straight through; pop_back() then ate a
                 *    DIGIT and the parser answered element 0 and element 1,
                 *    SILENTLY, with a value nothing distinguishes from a
                 *    correct reading. That is worse than the empty string the
                 *    same typo produces everywhere else in this parser.
                 */
                if (val.size() < 2 || val.back() != ']')
                {
                    //T3.35b. No `err` flag on this side: WebCtrl::getValue()
                    //returns a string and nothing more, and none of its three
                    //callers asks for an error. The MQTT copy carries one
                    //because MqttCtrl::getValue() already promised one and was
                    //lying about it. Deliberate divergence, see T3.37.
                    cWarning() << "Error in path " << path << ", malformed array index " << *it
                               << " : an array index must be written [n], as in weather/[0]/description";
                    return string();
                }

                // Remove first and last char
                val.erase(0, 1);
                val.pop_back();

                int idx = 0;

                try
                {
                    /* T3.35b. The index is DECIDED here, on both branches,
                     * instead of being left to whatever Utils::from_string()
                     * happens to leave behind. The two failing shapes do not
                     * behave the same way and that asymmetry was the trap:
                     * on a BLANK string - the token "[]" - the stream sentry
                     * fails before num_get ever runs, so the destination is
                     * NOT written and the index was read UNINITIALISED; on a
                     * non blank string that does not parse - "[zz]" - the
                     * sentry succeeds and C++11 num_get stores 0. from_string()
                     * cannot even be interrogated about it: it returns
                     * iss.eof(), which is TRUE for the blank string.
                     *
                     * The two cases are made to agree, deliberately, on
                     * element 0: T3.29 froze that value and a real
                     * configuration may lean on it. What does not stay is the
                     * SILENCE - reading element 0 because the index was
                     * unreadable is precisely the case a user cannot diagnose.
                     *
                     * Everything that touches the index sits inside this try,
                     * so it can only ever fail through the one error path this
                     * branch already has.
                     *
                     * T3.35c. THE TEST IS "DOES IT CARRY A DIGIT", NOT "DOES
                     * from_string() COMPLAIN". from_string() returns
                     * iss.eof(), and a stream that consumed only whitespace -
                     * or only a sign - DID reach its end, so it reports
                     * SUCCESS on "[ ]", "[\t]", "[+]" and "[-]". On the two
                     * blank ones it does not write the destination either,
                     * which is how the index was still being read UNASSIGNED
                     * after T3.35b: `val.empty()` catches "[]" and nothing
                     * else. find_first_of() closes the whole family in one
                     * test, and it subsumes val.empty() - an empty string has
                     * no digit - so no sub-condition here is dead.
                     *
                     * WHY NOT "every character must be a digit"
                     * (find_first_not_of): it would test the WRONG thing
                     * three ways. An empty string has no NON-digit either, so
                     * "[]" would walk back through unguarded; "[+2]" would
                     * stop resolving; and "[-1]" would be answered "is not a
                     * number", which is false about -1. The sign is NOT
                     * rejected here, deliberately: a signed or padded token
                     * carries a number and goes on to at(), which refuses a
                     * negative index as out of range - a different message
                     * for a different mistake.
                     *
                     * INVARIANT this establishes, and the reason the
                     * `int idx = 0` above is now GENUINELY dead - it is kept
                     * as a belt, it is no longer the value anything reads:
                     * when the guard passes, val holds at least one digit, so the
                     * stream sentry succeeds, so num_get RUNS - and C++11
                     * num_get always stores something (the value, 0 on a
                     * failed parse, or the clamped limit on overflow). When
                     * the guard trips, idx = 0 is assigned. Every path into
                     * parent.at(idx) therefore writes idx first.
                     */
                    if (val.find_first_of("0123456789") == string::npos ||
                        !Utils::from_string(val, idx))
                    {
                        idx = 0;
                        cWarning() << "Error in path " << path << ", array index " << *it
                                   << " is not a number : reading element 0";
                    }

                    parent = parent.at(idx);
                }
                catch (const std::exception &e)
                {
                    cWarning() << "Error in path " << path << ", index not found " << *it << " : " << e.what();
                    return string();
                }
            }
            else
            {
                // Toke is a normal object name
                try
                {
                    parent = parent.at(val);
                }
                catch (const std::exception &e)
                {
                    cWarning() << "Error in path " << path << ", subpath not found " << *it << " : " << e.what();

                    /* T3.35 - the "option C" of T3.29 section 5.6. NO FALSE
                     * POSITIVE IS POSSIBLE HERE, by construction and not by
                     * heuristic: this catch is only ever entered when
                     * parent.at(val) has ALREADY thrown. A payload whose key
                     * really is spelled "action[0]" - a real, measured
                     * Zigbee2MQTT shape - has RESOLVED and never reaches this
                     * line. That is exactly what separates it from teaching
                     * the parser to also split a glued index, which T3.29
                     * implemented, measured and rejected because it shadowed
                     * such a key silently.
                     *
                     * Honest about its reach: cWarning() is not a filtered
                     * domain, so this does go to the calaos_server log by
                     * default - but the person who made the typo is sitting in
                     * calaos_installer.
                     */
                    if (val.find('[') != string::npos)
                    {
                        string suggestion = val;
                        suggestion.insert(suggestion.find('['), "/");
                        cWarning() << "Error in path " << path << ", did you mean " << suggestion
                                   << " ? array indices are their own path segment, not glued to "
                                      "the key that precedes them";
                    }

                    return string();
                }
            }
        }

        if (parent.is_null())
            value = "null";
        else if (parent.is_boolean())
            value = parent.get<bool>()?"true":"false";
        else if (parent.is_number())
            value = Utils::to_string(parent.get<double>());
        else if (parent.is_string())
            value = parent.get<string>();
        else if (parent.is_object())
        {
            cWarning() << "Error, path returns an object, not a value";
            value = "object{}";
        }
        else if (parent.is_array())
        {
            cWarning() << "Error, path returns an array, not a value";
            value = "array[]";
        }
    }
    else
    {
        cWarning() << "Error emtpy path not allowed";
    }

    return value;
}

/*
 * `path` is a full XPath expression straight out of the user config
 * (WebDocBase.cpp advertises it as such, w3schools link included), so
 * predicates, functions and axes are part of the contract and are all
 * evaluated natively by pugixml.
 *
 * Result stringification follows XPath 1.0 string(): a node-set becomes the
 * string-value of its first node in document order (empty set -> empty
 * string), a number/boolean its canonical lexical form. That is what
 * evaluate_string() does, and it is what TinyXPath's S_compute_xpath()
 * claimed to do. The three places where TinyXPath actually deviated from the
 * spec are fixed rather than replicated, because every one of them used to
 * yield an unusable value:
 *  - string-value of an *element* returned the tag name ("temperature")
 *    instead of its text; getValueDouble() turned that into 0,
 *  - string(), number(), boolean(), local-name() and round() were not
 *    implemented and silently produced an empty string,
 *  - a bare relative path resolved to nothing at all.
 * Text nodes, attributes, predicates, axes, count()/sum()/concat()/
 * substring()/translate()/normalize-space()/starts-with()/contains(), unions
 * and comparisons were verified to return byte-identical strings under both
 * engines, so an expression that used to work keeps working.
 */
string WebCtrl::getValueXml(string path, string filename)
{
    pugi::xml_document document;

    //Default parse options: no DTD, no entity expansion, no network -- the
    //document is remote and untrusted.
    const pugi::xml_parse_result parsed = document.load_file(filename.c_str());
    if (!parsed)
    {
        cError() << "Error loading file " << filename << " : " << parsed.description();
        // Error loading file
        return "";
    }

    //Same context node TinyXPath was handed (TiXmlDocument::RootElement()), so
    //user expressions keep their exact meaning: absolute paths still walk from
    //the document root, relative ones from the document element.
    const pugi::xml_node context = document.document_element();
    if (!context)
    {
        //TinyXPath dereferenced this NULL; a document made only of comments is
        //enough to reach it.
        cError() << "Error, no root element in file " << filename;
        return "";
    }

    //pugi::xpath_query throws xpath_exception on an invalid expression where
    //TinyXPath returned an error code and an empty string. Nothing may escape
    //here: the expression is user config, and getValue() is called from the
    //download callback. Non-throwing parse that logs and fails clean, same
    //house pattern as parseGridDimension()/parseDebounceTime().
    try
    {
        const pugi::xpath_query query(path.c_str());

        //PUGIXML_NO_EXCEPTIONS builds report the parse error here instead of
        //throwing, so check it too rather than evaluating a broken query.
        if (!query)
        {
            cError() << "Invalid XPath expression \"" << path << "\" : "
                     << query.result().description();
            return "";
        }

        return query.evaluate_string(context);
    }
    catch (const pugi::xpath_exception &e)
    {
        cError() << "Invalid XPath expression \"" << path << "\" : " << e.what();
        return "";
    }
    catch (const std::exception &e)
    {
        cError() << "Error evaluating XPath expression \"" << path
                 << "\" on " << filename << " : " << e.what();
        return "";
    }
}

/*
 * Read plain text file and return value find in path Path is of type:
 * line/pos/separator line is the line number in the file. This
 * function read the correspondig line. If sepearator exists, the line
 * is split with separator as delimiter. The value returned is the pos
 * value in the list. In case separator doesn't exist the value
 * returned is the whole line found.
 * Example : the file contains :
 * 10.0,10.1,10.2,10.3
 * 20.0,20.1,20.2,20.3
 *
 * If The path is "2/4/,"
 * The value returned will be the 4th token of the second line : 20.3
 *
 */
string WebCtrl::getValueText(string path, string filename)
{
    string value;
    vector<string> tokens;
    vector<string> items;
    int line_nb = 0;
    unsigned int item_nb = 0;
    ifstream file(filename);
    string line;
    int i;

    if (!file.is_open())
    {
        cError() << "Error reading file " << filename;
        return "";
    }

    Utils::split(path, tokens, "/");
    //split() with max=0 does not pad: an empty or delimiter-only path
    //yields an empty vector, and tokens[0] would be out of range.
    if (tokens.empty())
    {
        cError() << "Error, empty path not allowed";
        return "";
    }
    Utils::from_string(tokens[0], line_nb);
    for (i = 0; i < line_nb; i++)
    {
        getline(file, line);
    }

    if (tokens.size() == 3 && tokens[2] != "")
      {
        Utils::split(line, items, tokens[2]);
        Utils::from_string(tokens[1], item_nb);
        if (item_nb <= 0)
          item_nb = 1;
        if (items.size() >= item_nb)
            value = items[item_nb - 1];
      }
    else
      {
        value = line;
      }

    return value;
}


double WebCtrl::getValueDouble(string path)
{
    double val = 0;
    string value;

    value = getValue(path);
    if (Utils::is_of_type<double>(value) && !value.empty())
        Utils::from_string(value, val);
    return val;
}

string WebCtrl::getValue(string path)
{
    string filename;
    string url =  param.get_param("url");

    if (Utils::strStartsWith(url, "/") ||
        Utils::strStartsWith(url, "file://"))
    {
        filename = url;
        if (Utils::strStartsWith(url, "file://"))
            filename.erase(0, 7);
    }
    else
    {
        filename = "/tmp/calaos_" + param.get_param("id");
    }

    cDebug() << "Filename : " << filename << " file type : " << file_type;

    if (file_type == JSON)
        return getValueJson(path, filename);
    else if (file_type == XML)
        return getValueXml(path, filename);
    else if (file_type == TEXT)
        return getValueText(path, filename);
    else
    {
        cWarning() << "WebIO: unknown file type " << file_type << ". Can't process data.";
        return "";
    }
}

void WebCtrl::setValue(string value)
{
    string url =  param.get_param("url");
    string data = param.get_param("data");
    string data_type = param.get_param("data_type");

    // Case where there is no data to send, we assume the value is in the url
    if (param.get_param("data") == "")
    {
    string filename;

        replace_str(url, "__##VALUE##__", url_encode(value));
    }
    else
    {
        replace_str(data, "__##VALUE##__", value);
    }

    UrlDownloader *fdownloader = new UrlDownloader(url, true);
    //T2.19: user-configured URL — insecure unless the IO opts in to
    //hardening with insecure="false"
    fdownloader->setInsecureFromParam(param.get_param("insecure"));
    fdownloader->setHeader("Content-Type", data_type);
    fdownloader->httpPost(string(), data);

    cDebug() << "Set value with param : "
             << url << " | "
             << param.get_param("request_type") << " | "
             << data << " | "
             << data_type;

}

