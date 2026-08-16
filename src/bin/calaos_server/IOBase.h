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
#ifndef S_IOBASE_H
#define S_IOBASE_H

#include "Calaos.h"
#include "EventManager.h"
#include "IODoc.h"

namespace Calaos
{

class AutoScenario;



class IOBase
{
public:
    enum class StatusConnected
    {
        STATUS_NONE = 0,
        STATUS_DISCONNECTED = 1,
        STATUS_CONNECTED = 2,
    };

protected:
    //we store all params here
    Params param;

    IODoc *ioDoc = nullptr;

    struct StatusInfo
    {
        double battery_level = 0.0; // Battery level in percentage
        bool battery_level_set = false; // True once a battery level has been reported (0% is a valid reading)
        StatusConnected connected = StatusConnected::STATUS_NONE; // True if the device is connected
        double wireless_signal = 0.0; // Wireless signal strength in percentage
        uint64_t uptime = 0; // Uptime in seconds
        string ip_address; // IP address of the device
        string wifi_ssid; // WiFi SSID if connected to WiFi
    };

    StatusInfo status_info;

private:
    bool auto_sc_mark;
    AutoScenario *ascenario = nullptr;

    int io_type = IO_UNKNOWN;

    //True when the constructor registered this IO into ListeRoom's io_table:
    //only then may the destructor (or renameId()) touch the table.
    bool hashRegistered = false;

    //When true, newly constructed IOBase objects are NOT registered into
    //ListeRoom's io_table. Only ever set through ScopedDocGen (single
    //threaded, not reentrant), used by IOFactory doc generation.
    static bool docGenerationMode;

public:

    enum
    {
        IO_UNKNOWN = 0,
        IO_INPUT,
        IO_OUTPUT,
        IO_INOUT,
    };

    static double const TimerChangedWarning;

    IOBase(Params &p, int iotype);
    virtual ~IOBase();

    virtual DATA_TYPE get_type() = 0;

    virtual bool get_value_bool() { return false; }
    virtual map<string, bool> get_all_values_bool() { map<string, bool> m; return m; }

    virtual double get_value_double() { return 0.0; }
    virtual map<string, double> get_all_values_double() { map<string, double> m; return m; }

    virtual std::string get_value_string() { return ""; }
    virtual map<string, string> get_all_values_string() { map<string, string> m; return m; }

    virtual map<string, string> query_param(string key) { map<string, string> m; return m; }

    /* "id" is the key of ListeRoom's io_table hash. Changing it through the
     * generic setter would leave the table keyed on the old id (stale lookup,
     * dangling entry on delete -> potential use-after-free), so set_param()
     * refuses to change an existing "id" and del_param() refuses to delete
     * it. Renaming an IO must go through renameId(), which keeps the
     * io_table consistent.
     * Note: get_params() still hands out a mutable Params reference, callers
     * must not use it to change "id" (a full read-only API is out of scope
     * of T1.11). */
    virtual void set_param(std::string opt, std::string val);
    virtual std::string get_param(std::string opt) { return param[opt]; }
    virtual Params &get_params() { return param; }
    virtual bool param_exists(std::string opt) { return param.Exists(opt); }
    virtual void del_param(std::string opt);

    /* Controlled rename hook: atomically changes the "id" param AND the
     * io_table key (unregister/re-register through ListeRoom). Fails (and
     * changes nothing) when newId is empty or already taken by another IO.
     * This is the only supported way to change an IO id after creation. */
    bool renameId(const std::string &newId);

    /* RAII guard used by IOFactory::genDocIO(): while alive, constructed
     * IOBase objects stay out of the live io_table (they are documentation
     * throwaways sharing id="doc", they must neither shadow nor collide
     * with real IOs). Not reentrant, single threaded use only. */
    class ScopedDocGen
    {
    public:
        ScopedDocGen();
        ~ScopedDocGen();
        ScopedDocGen(const ScopedDocGen &) = delete;
        ScopedDocGen &operator=(const ScopedDocGen &) = delete;
    };

    virtual bool LoadFromXml(pugi::xml_node node);
    virtual bool SaveToXml(pugi::xml_node node);

    bool isAutoScenario() { return auto_sc_mark; }
    void setAutoScenario(bool m) { auto_sc_mark = m; }

    void setAutoScenarioPtr(AutoScenario *sc) { ascenario = sc; }
    AutoScenario *getAutoScenarioPtr() { return ascenario; }

    bool isEnabled() { return param["enabled"] == "true"; }

    IODoc *getDoc() const {return ioDoc; }

    bool isInput() { return io_type == IO_INPUT || io_type == IO_INOUT; }
    bool isOutput() { return io_type == IO_OUTPUT || io_type == IO_INOUT; }
    bool isInOut() { return io_type == IO_INOUT; }

    virtual void EmitSignalIO();

    virtual void hasChanged() { }

    //Output specific functions
    virtual bool set_value(bool val) { return false; }
    virtual bool set_value(double val)  { return false; }
    virtual bool set_value(std::string val)  { return false; }

    //used to retreive the last state command of the output
    virtual std::string get_command_string() { return ""; }
    virtual bool get_command_bool() { return false; }
    virtual double get_command_double() { return 0.0; }

    //used to get a better condition value in ConditionOutput rules
    //like if shutter is open or is light on
    //Note: this is only used for TSTRING outputs
    virtual bool check_condition_value(std::string cvalue, bool equal) { return false; }

    enum class StatusType
    {
        BatteryLevel,
        Connected,
        WirelessSignal,
        Uptime,
        IpAddress,
        WifiSSID
    };

    void setStatusInfo(StatusType type, double value);
    void setStatusInfo(StatusType type, const string &value);
    void setStatusInfo(StatusType type, StatusConnected value);
    void setStatusInfo(StatusType type, uint64_t value);

    bool hasStatusInfo() const
    {
        return status_info.battery_level_set ||
               status_info.connected != StatusConnected::STATUS_NONE ||
               status_info.wireless_signal != 0.0 ||
               status_info.uptime != 0 ||
               !status_info.ip_address.empty() ||
               !status_info.wifi_ssid.empty();
    }

    Params getStatusInfo() const;
};

}

#endif
