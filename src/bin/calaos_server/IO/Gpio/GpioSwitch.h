#ifndef GPIOSWITCH_H
#define GPIOSWITCH_H

#include "InputSwitch.h"
#include "Params.h"

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
