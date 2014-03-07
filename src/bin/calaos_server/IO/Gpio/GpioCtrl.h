#ifndef GPIOCTRL_H
#define GPIOCTRL_H

#include <Utils.h>

namespace Calaos
{

class GpioCtrl
{
private:
    int gpionum; // GPIO Number
    string gpionum_str;
    int writeFile(string path, string value);
    int readFile(string path, string &value);
    int fd;
public:
    GpioCtrl(int _gpionum);
    ~GpioCtrl();
    bool exportGpio();
    bool unexportGpio();
    bool setDirection(string direction);
    bool setval(bool val);
    bool getVal(bool &val);
    int getFd(void);
    void closeFd(void);
    int getGpioNum(void);

};
}
#endif // GPIOCTRL_H
