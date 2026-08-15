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
#ifndef S_WagoIOBase_H
#define S_WagoIOBase_H

#include <IODoc.h>
#include <WagoMap.h>
#include <WagoConfigParse.h>

namespace Calaos
{

/* T3.2e — Shared skeletons for the thin Wago IO subclasses.
 *
 * WIDigitalBase<Base> carries the whole digital-input motif once:
 * WIDigitalBP/WIDigitalLong/WIDigitalTriple were three copies of the
 * same class where only the parent (InputSwitch / InputSwitchLongPress /
 * InputSwitchTriple — i.e. the debounce/timing behavior) and the ioDoc
 * words differed. The debounce/timing behavior therefore stays entirely
 * in the Base template parameter, unchanged.
 *
 * WOVoletBase<Base> does the same for WOVolet/WOVoletSmart (parent
 * OutputShutter / OutputShutterSmart).
 *
 * The _() literals unique to one subclass (description, wiki link, var
 * doc) stay in the subclass .cpp files, which are the ones listed in
 * po/POTFILES.in; the host/port literals below are also extracted from
 * the other Wago .cpp files that still carry them.
 *
 * T3.10 deliberately did NOT rebase these two on the shared ThinIo
 * template (IO/ThinIo.h), unlike Mqtt/Web/Gpio:
 *  - WIDigitalBase documents itself as friendlyName, aliasAdd..., then
 *    description, then linkAdd; ThinIo's contract is friendlyName then
 *    description, so adopting it would reorder the ioDoc calls of the
 *    only Wago IOs that carry aliases, to save two lines,
 *  - WOVoletBase performs no ioDoc setup at all (WOVolet/WOVoletSmart
 *    document themselves), so there is no preamble to factor out. */

//Common host/port/var ioDoc entries shared by every Wago digital input
inline void wagoDocCommon(IODoc *doc, const std::string &varDesc)
{
    doc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    doc->paramAddInt("port", _("Wago ethernet port, default to 502"), 0, 65535, false, 502);
    doc->paramAddInt("var", varDesc, 0, 65535, true);
}

template<class Base>
class WIDigitalBase : public Base, public sigc::trackable
{
protected:
    Utils::type_signal_wago::iterator iter;

    int address = WagoConfigParse::ADDRESS_DEFAULT;
    std::string host;
    int port = WagoConfigParse::PORT_DEFAULT;

    bool udp_value = false;
    bool initial;

    //true for WIDigitalBP: read the initial state at startup and
    //re-issue a modbus read on readValue() (forces a reconnection in
    //case of disconnection). WIDigitalLong/WIDigitalTriple never did
    //either (they only care about live edges).
    const bool readsInitialState;

    void parseWagoConfig()
    {
        host = this->get_param("host");

        bool ok = true;
        address = WagoConfigParse::parseAddress(this->get_param("var"), &ok);
        if (!ok)
            cWarningDom("input") << this->get_param("id") << ": invalid var \""
                                 << this->get_param("var") << "\", using " << address;

        port = WagoConfigParse::PORT_DEFAULT;
        if (this->get_params().Exists("port"))
        {
            port = WagoConfigParse::parsePort(this->get_param("port"), &ok);
            if (!ok)
                cWarningDom("input") << this->get_param("id") << ": invalid port \""
                                     << this->get_param("port") << "\", using " << port;
        }
    }

    void WagoReadCallback(bool status, UWord addr, int count, std::vector<bool> &values)
    {
        if (!status)
        {
            cErrorDom("input") << this->get_param("id") << ": Failed to read value";
            if (initial)
            {
                Calaos::StartReadRules::Instance().ioRead();
                initial = false;
            }

            return;
        }

        if (initial)
        {
            if (!values.empty())
                this->value = values[0];

            cInfoDom("input") << this->get_param("id") << ": Reading initial state: "
                              << (this->value? "true": "false");
            initial = false;

            Calaos::StartReadRules::Instance().ioRead();
        }
    }

    virtual bool readValue()
    {
        parseWagoConfig();

        if (readsInitialState && this->get_param("knx") != "true")
        {
            //Force to reconnect in case of disconnection
            WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBase::WagoReadCallback));
        }

        return udp_value;
    }

public:
    WIDigitalBase(Params &p,
                  const std::string &friendlyName,
                  const std::vector<std::string> &aliases,
                  const std::string &description,
                  const std::string &wikiLink,
                  const std::string &varDesc,
                  bool _readsInitialState):
        Base(p),
        initial(_readsInitialState),
        readsInitialState(_readsInitialState)
    {
        // Define IO documentation
        this->ioDoc->friendlyNameSet(friendlyName);
        for (const auto &alias: aliases)
            this->ioDoc->aliasAdd(alias);
        this->ioDoc->descriptionSet(description);
        this->ioDoc->linkAdd("Calaos Wiki", wikiLink);
        wagoDocCommon(this->ioDoc, varDesc);

        parseWagoConfig();

        WagoMap::Instance(host, port);

        iter = Utils::signal_wago.connect( sigc::mem_fun(this, &WIDigitalBase::ReceiveFromWago) );

        if (readsInitialState)
        {
            if (this->get_param("knx") != "true")
            {
                WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBase::WagoReadCallback));

                Calaos::StartReadRules::Instance().addIO();
            }
            else
            {
                cInfoDom("input") << this->get_param("id") << ": Not reading initial state for KNX inputs";
            }
        }

        cDebugDom("input") << this->get_param("id") << ": Ok";
    }

    virtual ~WIDigitalBase()
    {
        iter->disconnect();
    }

    virtual void ReceiveFromWago(std::string ip, int addr, bool val, std::string intype)
    {
        if (ip == host && addr == address)
        {
            if ((intype == "std" && this->get_param("knx") != "true") ||
                (intype == "knx" && this->get_param("knx") == "true"))
            {
                cInfoDom("input") << "Got " << Utils::to_string(val) << " on " << intype << " input " << addr;

                udp_value = val;
                this->hasChanged();
            }
        }
    }
};

template<class Base>
class WOVoletBase : public Base
{
protected:
    std::string host;
    int port = WagoConfigParse::PORT_DEFAULT;
    int up_address = WagoConfigParse::ADDRESS_DEFAULT;
    int down_address = WagoConfigParse::ADDRESS_DEFAULT;

    //Hook run at the end of readConfig(); WOVoletSmart chains up to
    //OutputShutterSmart::readConfig() there.
    virtual void readConfigExtra() {}

    //Call at the end of the subclass constructor, after the ioDoc setup
    void voletInit()
    {
        readConfig();
        WagoMap::Instance(host, port);

        cDebugDom("output") << this->get_param("id") << ": Ok";
    }

    int parseVoletAddress(const std::string &param)
    {
        bool ok = true;
        int addr = WagoConfigParse::parseAddress(this->get_param(param), &ok);
        if (!ok)
            cWarningDom("output") << this->get_param("id") << ": invalid " << param << " \""
                                  << this->get_param(param) << "\", using " << addr;
        return addr;
    }

public:
    WOVoletBase(Params &p): Base(p) {}
    virtual ~WOVoletBase() {}

    //For WOVoletSmart this overrides the virtual OutputShutterSmart::readConfig()
    virtual void readConfig()
    {
        host = this->get_param("host");
        port = WagoConfigParse::PORT_DEFAULT;
        if (this->get_params().Exists("port"))
        {
            bool ok = true;
            port = WagoConfigParse::parsePort(this->get_param("port"), &ok);
            if (!ok)
                cWarningDom("output") << this->get_param("id") << ": invalid port \""
                                      << this->get_param("port") << "\", using " << port;
        }
        up_address = parseVoletAddress("var_up");
        down_address = parseVoletAddress("var_down");

        //handle knx and 841/849
        if (this->get_param("knx") == "true")
        {
            up_address += WAGO_KNX_START_ADDRESS;
            down_address += WAGO_KNX_START_ADDRESS;
        }
        if (this->get_param("wago_841") == "true" && this->get_param("knx") != "true")
        {
            up_address += WAGO_841_START_ADDRESS;
            down_address += WAGO_841_START_ADDRESS;
        }

        readConfigExtra();
    }

    virtual void setOutputUp(bool enable)
    {
        readConfig();
        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, enable, sigc::mem_fun(*this, &WOVoletBase::WagoWriteCallback));
    }

    virtual void setOutputDown(bool enable)
    {
        readConfig();
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, enable, sigc::mem_fun(*this, &WOVoletBase::WagoWriteCallback));
    }

    void WagoWriteCallback(bool status, UWord address, bool value)
    {
        if (!status)
        {
            cErrorDom("output") << this->get_param("id") << ": Failed to write value";
            return;
        }
    }
};

}
#endif
