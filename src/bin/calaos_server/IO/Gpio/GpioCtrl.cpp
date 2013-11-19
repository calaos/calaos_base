#include "GpioCtrl.h"

using namespace Calaos;

GpioCtrl::GpioCtrl(int _gpionum)
{
        gpionum = _gpionum;
        gpionum_str = Utils::to_string(gpionum);
}

GpioCtrl::~GpioCtrl()
{
}

int GpioCtrl::writeFile(string path, string value)
{
        ofstream ofspath(path.c_str());
        if (ofspath < 0)
        {
                // Unable to export GPIO
                return false;
        }
        ofspath << value;
        ofspath.close();

        return true;
}

int GpioCtrl::readFile(string path, string &value)
{
        ifstream ifspath(path.c_str());
        if (ifspath < 0)
        {
                return false;
        }

        ifspath >> value;
        ifspath.close();

        return true;
}

bool GpioCtrl::exportGpio()
{
        return writeFile("/sys/class/gpio/export", gpionum_str);
}

bool GpioCtrl::unexportGpio()
{
        return writeFile("/sys/class/gpio/unexport", gpionum_str);
}

// Set GPIO direction : "in" or "out"
bool GpioCtrl::setDirection(string direction)
{
        string path = "/sys/class/gpio/gpio" + gpionum;
        return writeFile(path, direction);
}

bool GpioCtrl::setval(bool val)
{
        string strval;
        string path = "/sys/class/gpio/gpio" + gpionum;
        strval = Utils::to_string(val);

        return writeFile(path, strval);
}

bool GpioCtrl::getVal(bool &val)
{
        string path = "/sys/class/gpio/gpio" + gpionum;
        string strval;
        if (!readFile(path, strval))
                return false;
        if (strval == "1")
                return true;
        else
                return false;
}

int GpioCtrl::getFd(void)
{
        string path = "/sys/class/gpio/gpio" + gpionum;
        int fd;
        fd = open(path.c_str(), O_RDONLY);
        return fd;

}

void GpioCtrl::closeFd(int fd)
{
        close(fd);
}

int GpioCtrl::getGpioNum(void)
{
        return gpionum;
}
