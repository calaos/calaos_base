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

#ifndef WEBDOCBASE_H
#define WEBDOCBASE_H

#include "Calaos.h"
#include "IODoc.h"
#include "ThinIo.h"
#include "WebCtrl.h"
#include "AnalogIO.h"

namespace Calaos
{

class WebDocBase
{
public:
    void initDoc(IODoc *ioDoc, bool docPost = false);
};

//Which flavor of the shared WebDocBase parameters an IO documents:
//Get  -> url/file_type/path (read a web document)
//Post -> url/data/data_type (write to a web document or URL)
enum class WebDoc { Get, Post, GetAndPost };

//Shared skeleton of the thin Web IO subclasses: ioDoc friendlyName,
//description, the common WebDocBase parameters, and the usual
//construction log line. The friendlyName/description preamble is the shared
//ThinIo one (IO/ThinIo.h, T3.10); only the Web-specific part stays here.
template<class IOBaseT>
class WebIOBase : public ThinIo<IOBaseT>
{
protected:
    WebDocBase docBase;

    WebIOBase(Params &p, const char *name, const string &desc,
              WebDoc doc, const char *logDomain):
        ThinIo<IOBaseT>(p, name, desc)
    {
        if (doc != WebDoc::Post)
            docBase.initDoc(this->ioDoc);
        if (doc != WebDoc::Get)
            docBase.initDoc(this->ioDoc, true);

        cInfoDom(logDomain) << name << "::" << name << "()";
    }
};

//Web inputs additionally take part in the start-read accounting and poll
//their value through WebCtrl.
//
//WARNING: WebCtrl::Add() fires the downloaded-callback *synchronously* when
//the url points to a local file (url beginning with / or file://). The
//callback calls the virtual readValue(), so the registration must not run
//from a base class constructor (pure virtual call). Every concrete subclass
//constructor must therefore call webRegisterPolling() as its *last*
//statement, once the object is fully constructed.
template<class InputBaseT>
class WebInputBase : public WebIOBase<InputBaseT>
{
protected:
    WebInputBase(Params &p, const char *name, const string &desc):
        WebIOBase<InputBaseT>(p, name, desc, WebDoc::Get, "input")
    {
        StartReadRules::Instance().addIO();
    }

    ~WebInputBase()
    {
        if (webCtrl)
            webCtrl->Del(webCallbackId);
    }

    //To be called at the end of the concrete subclass constructor (see the
    //warning above): registers readValue() as WebCtrl polling callback.
    void webRegisterPolling()
    {
        if (this->get_param("path").empty())
            return;

        webCtrl = &WebCtrl::Instance(this->get_params());
        webCallbackId = webCtrl->Add(this->frequency, [this](bool fetched)
                                     {
            received = fetched;
            this->readValue();
            received = false;
            StartReadRules::Instance().ioRead();
        });
    }

    //A local file is read again on each period, whether rewritten or not.
    bool datesEveryReading() override
    {
        const string url = this->get_param("url");
        return !Utils::strStartsWith(url, "/") && !Utils::strStartsWith(url, "file://");
    }

    //Inside the polling callback: the document was just downloaded. The
    //input poll also calls readValue(), on the same old document.
    bool received = false;

    //Shared readValue() body of the numeric web inputs (analog, temp)
    void readValueDouble()
    {
        if (this->get_param("path").empty())
            return;

        const string raw = WebCtrl::Instance(this->get_params()).getValue(this->get_param("path"));
        double v = 0;
        if (Utils::is_of_type<double>(raw) && !raw.empty())
        {
            Utils::from_string(raw, v);
            //An unchanged value is not signalled, but the sensor is alive.
            if (received)
                this->markReading();
        }
        //Compared once converted: a document read again must not be signalled.
        const double converted = AnalogIO::convertValue(this->get_params(), v);
        if (converted != this->value)
        {
            this->value = converted;
            this->emitChange();
        }
    }

private:
    //The controller registered on, not the one the params name now: the
    //entries of WebCtrl::hash are never dropped and do not move.
    WebCtrl *webCtrl = nullptr;
    uint64_t webCallbackId = 0;
};

}

#endif // WEBDOCBASE_H
