/******************************************************************************
 **  Copyright (c) 2007-2017, Calaos. All Rights Reserved.
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
#ifndef CALAOS_TIMER_H
#define CALAOS_TIMER_H

#include "Utils.h"
#include <sigc++/sigc++.h>
#include <uv.h>

using namespace Utils;

class Timer
{
private:
    uv_timer_t timer;

    sigc::signal<void, void *> event_signal_data;
    sigc::signal<void> event_signal;
    sigc::connection connection_data;
    sigc::connection connection;

    double time;
    bool timer_data;

    void *data;

public:
    Timer(double time, sigc::slot<void, void *> slot, void *data);
    Timer(double time, sigc::slot<void> slot);
    ~Timer();

    static void singleShot(double time, sigc::slot<void> slot);

    void Reset();
    void Reset(double time);

    //private, used by ecore
    void Tick();

    double getTime() { return time; }
};

class Idler
{
 private:
  uv_idle_t idler;

  void createIdler();
  friend void Idler_idler_cb(uv_idle_t *handle);

 public:
  Idler(sigc::slot<void> slot);
  Idler();
  ~Idler();

  static void singleIdler(sigc::slot<void> slot);

  sigc::signal<void> idlerCallback;

};


#endif
