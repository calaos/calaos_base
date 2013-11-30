#include "GpioSwitch.h"

using namespace Calaos;

GpioOutputSwitch::GpioOutputSwitch(Params &p):
        InputSwitch(p)
{
        int gpio_nb;
        Utils::from_string(get_param("gpio_nb"), gpio_nb);

        gpioctrl = new GpioCtrl(gpio_nb);
        gpioctrl->setDirection("out");

}

GpioOutputSwitch::~GpioOutputSwitch()
{
        delete gpioctrl;
}


bool GpioOutputSwitch::set_value_real(bool val)
{
        gpioctrl->setval(val);
        return true;
}
