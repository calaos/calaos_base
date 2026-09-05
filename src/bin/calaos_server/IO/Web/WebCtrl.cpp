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
#include "JsonPath.h"

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
    bool err = false;
    return getValueJson(path, filename, err);
}

string WebCtrl::getValueJson(string path, string filename, bool &err)
{
    string value;

    err = false;

    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        err = true;
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
        err = true;
        cWarning() << "Error parsing " << filename << ":" << e.what();
        return string();
    }

    //T3.37. The ~55 line path parser that used to sit here, and again in
    //MqttCtrl::getValueJson(), now lives once in IO/JsonPath.h. What is left
    //is what is genuinely Web's: the document comes from a downloaded FILE,
    //so it is opened and parsed here and the log names the file. There is no
    //empty-path shortcut on this side - "" splits into no token and falls
    //into the parser's "no path segment" branch, which is the divergence
    //T3.29 AnEmptyPathReturnsEmpty freezes against the MQTT copy.
    if (!JsonPath::resolve(root, path, value))
    {
        //T3.37 closes the T3.35b sect. 6.8 divergence AT THE PARSER: resolve()
        //reports to everyone, there is no flagless variant to fall back on.
        //It is DISCARDED HERE, on purpose and in one place, because
        //WebCtrl::getValue() reports nothing to its four call sites and
        //making it report is a behaviour change that also has to cover the
        //XML and TEXT branches to mean anything - a flag honest on one branch
        //out of three is the very defect T3.35b sect. 6.1 had to undo. The
        //measurement of what those four sites do with a silent failure today,
        //and the follow-up it calls for, are in docs/refactoring/T3.37.md
        //sect. 4.
        err = true;
        return string();
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
             << Utils::urlForLog(url) << " | "
             << param.get_param("request_type") << " | "
             << data << " | "
             << data_type;

}

