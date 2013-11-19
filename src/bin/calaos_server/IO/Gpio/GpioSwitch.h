#ifndef GPIOSWITCH_H
#define GPIOSWITCH_H

#include "InputSwitch.h"
#include "Params.h"

namespace Calaos
{

class GpioSwitch : public InputSwitch
{
public:
        GpioSwitch(Params &p);
        virtual ~GpioSwitch();
};

}

#endif // GPIOSWITCH_H
