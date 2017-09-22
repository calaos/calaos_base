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
#include <sstream>
#include "Address.h"

namespace xPL
{

using namespace std;

Address::Address()
{
    m_All = false;
}

Address::Address(const string& vendor, const string& device, const string& instance)
{
    Address::SetAddress(vendor, device, instance);
}

Address::Address(const string& address)
{
    Address::SetAddress(address);
}

Address::~Address()
{
}

void Address::swap(Address& other)
{
    std::swap(m_Vendor, other.m_Vendor);
    std::swap(m_Device, other.m_Device);
    std::swap(m_Instance, other.m_Instance);
    std::swap(m_TempAddress, other.m_TempAddress);
    m_All = other.m_All;
}

Address::Address(Address const& other) :
    m_Vendor{other.m_Vendor},
    m_Device{other.m_Device},
    m_Instance{other.m_Instance},
    m_TempAddress{other.m_TempAddress},
    m_All{other.m_All}
{
}

Address& Address::operator=(Address const& other)
{
    Address{other}.swap(*this);
    return *this;
}

Address::Address(Address &&other)
{
    m_Vendor    = move(other.m_Vendor);
    m_Device    = move(other.m_Device);
    m_Instance  = move(other.m_Instance);
    m_TempAddress = move(other.m_TempAddress);
    m_All       = other.m_All;
}

Address& Address::operator=(Address&& other) noexcept
{
   Address(move(other)).swap(*this);
   return *this;
}

string Address::GetVendor() const
{
    return m_Vendor;
}

void Address::SetVendor(const string& vendor)
{
    if(vendor.find_first_of(".-")!=string::npos)
        throw Address::Exception(0x0101, "Vendor ID, illegal character '.' or '-'");
    if(vendor.length() > 8)
        throw Address::Exception(0x0104, "Vendor ID too long, max 8 characters");
    m_Vendor = vendor;
    m_All = false;
}

string Address::GetDevice() const
{
    return m_Device;
}

void Address::SetDevice(const string& device)
{
    if(device.find_first_of(".-")!=string::npos)
        throw Address::Exception(0x0102, "Device ID, illegal character '.' or '-'");
    if(device.length() > 8)
        throw Address::Exception(0x0105, "Device ID too long, max 8 characters");
    m_Device = device;
    m_All = false;
}

string Address::GetInstance() const
{
    return m_Instance;
}

void Address::SetInstance(const string& instance)
{
    if(instance.find_first_of(".-")!=string::npos)
        throw Address::Exception(0x0103, "Instance ID, illegal character '.' or '-'");
    if(instance.length() > 16)
        throw Address::Exception(0x0106, "Instance ID too long, max 16 characters");
    m_Instance = instance;
    m_All = false;
}

void Address::SetAddress(const string& vendor, const string& device, const string& instance)
{
    SetVendor(vendor);
    SetDevice(device);
    SetInstance(instance);
}

void Address::SetAddress(const string& address)
{
    size_t posMinus;
    size_t posPoint;

    if(address=="*")
    {
        m_All = true;
        return;
    }

    posMinus = address.find('-');
    if(posMinus==string::npos) throw Address::Exception(0x0107, "No vendor id in the address, '-' not found");
    SetVendor (address.substr(0, posMinus));

    posPoint = address.find('.', posMinus+1);
    if(posPoint==string::npos) throw Address::Exception(0x0108, "No device id in the address, '.' not found");
    SetDevice(address.substr(posMinus+1, posPoint-posMinus-1));

    SetInstance(address.substr(posPoint+1));
}


string Address::ToString()
{
    ostringstream msg;

    if(m_All)
	{
		m_TempAddress = "*";
		return m_TempAddress;
	}

    if((m_Vendor=="")||(m_Device=="")||(m_Instance==""))
        throw Address::Exception(0x0109, "Address::ToString() : uncomplete address, ToString impossible");

    msg << m_Vendor <<"-"<< m_Device <<"."<< m_Instance;
	m_TempAddress = msg.str();
    return m_TempAddress;
}

Address::Exception::Exception(int number, string const& message) throw()
{
    m_number = number;
    m_message = message;
}

Address::Exception::~Exception() throw()
{
}

const char* Address::Exception::what() const throw()
{
    return m_message.c_str();
}

int Address::Exception::GetNumber() const throw()
{
    return m_number;
}

}
