#ifndef GPIOSWITCH_H
#define GPIOSWITCH_H

#include "OutputLight.h"
#include "Params.h"
#include "GpioCtrl.h"

namespace Calaos
{

class GpioOutputSwitch : public OutputLight
{
    GpioCtrl *gpioctrl;

public:
    GpioOutputSwitch(Params &p);
    virtual ~GpioOutputSwitch();

protected:
    bool set_value_real(bool val);

};

}

#endif // GPIOSWITCH_H
