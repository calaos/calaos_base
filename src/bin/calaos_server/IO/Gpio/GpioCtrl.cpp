#include "GpioCtrl.h"

using namespace Calaos;

GpioCtrl::GpioCtrl(int _gpionum)
{
        gpionum = _gpionum;
        gpionum_str = Utils::to_string(gpionum);
        exportGpio();
        fd = -1;
}

GpioCtrl::~GpioCtrl()
{
        unexportGpio();
        if (fd == -1)
                close(fd);
        fd = -1;
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
        string strval;
        char tmp[4096];
        snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/direction", gpionum);
        string path(tmp);
        return writeFile(path, direction);
}

bool GpioCtrl::setval(bool val)
{
        string strval;
        char tmp[4096];
        snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
        string path(tmp);

        strval = Utils::to_string(val);

        return writeFile(path, strval);
}

bool GpioCtrl::getVal(bool &val)
{
        string strval;
        char tmp[4096];
        snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
        string path(tmp);

        if (!readFile(path, strval))
                return false;
        if (strval == "1")
                return true;
        else
                return false;
}

int GpioCtrl::getFd(void)
{
        string strval;
        char tmp[4096];
        snprintf(tmp, sizeof(tmp) - 1, "/sys/class/gpio/gpio%d/value", gpionum);
        string path(tmp);

        if (fd == -1)
                fd = open(path.c_str(), O_RDONLY);
        return fd;

}

void GpioCtrl::closeFd(void)
{
        if (fd != -1)
                close(fd);
        fd = -1;
}

int GpioCtrl::getGpioNum(void)
{
        return gpionum;
}
