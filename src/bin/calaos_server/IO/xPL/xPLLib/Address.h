/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL or xAP protocol

  This file is part of xPPLib.

    xPPLib is free software : you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    xPPLib is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with xPPLib.  If not, see <http://www.gnu.org/licenses/>.
*/
/***************************************************************************************************/
#ifndef XPL_ADDRESS_H
#define XPL_ADDRESS_H

#include <string>

namespace xPL
{

class Address
{
    public:
        class Exception;
        Address();
        Address(const std::string& vendor, const std::string& device, const std::string& instance);
        Address(const std::string& address);
        ~Address();

        void swap(Address& other);
        Address(Address const& other);
        Address& operator=(Address const& other);
        Address(Address &&other);
        Address& operator=(Address&& other) noexcept;

        std::string GetVendor() const;
        std::string GetDevice() const;
        std::string GetInstance() const;

        void SetVendor(const std::string& vendor);
        void SetDevice(const std::string& device);
        void SetInstance(const std::string& instance);

        void SetAddress(const std::string& address);
        void SetAddress(const std::string& vendor, const std::string& device, const std::string& instance);
        std::string ToString();
    protected:
    private:
        std::string m_Vendor;
        std::string m_Device;
        std::string m_Instance;
        std::string m_TempAddress;
        bool m_All;

        inline void SourceTestChar(const std::string& sourcePart, const std::string& errMsg, int errCode);
        inline void SourceTestLen(const std::string& sourcePart, unsigned int maxLen, const std::string& errMsg, int errCode);
};

class Address::Exception: public std::exception
{
public:
    Exception(int number, std::string const& message) throw();
    ~Exception() throw();
    const char* what() const throw();
    int GetNumber() const throw();

    private:
        int m_number;
        std::string m_message;
};
}
#endif // XPL_ADDRESS_H
