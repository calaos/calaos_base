/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL & xAP protocol

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
#include <algorithm>
#include "SchemaObject.h"

namespace xPL
{

using namespace std;

/****************************************************************************************************/
/*** Class SchemaObject                                                                             */
SchemaObject::SchemaObject()
{
    SetHop(1);
    SetMsgType(SchemaObject::stat);
}

SchemaObject::SchemaObject(ISchema::MsgType type, const string& schemaClass, const string& schemaType)
{
    SetHop(1);
    SetMsgType(type);
    SetClass(schemaClass);
    SetType(schemaType);
}

SchemaObject::~SchemaObject()
{
	SchemaObject::ClearSchema();
}


void SchemaObject::swap(SchemaObject& other)
{
    TargetAddress.swap(other.TargetAddress);
    m_MsgType = other.m_MsgType;
    std::swap(m_MsgTypeStr, other.m_MsgTypeStr);
    m_Hop = other.m_Hop;
    std::swap(m_Source, other.m_Source);
    std::swap(m_SchemaClass, other.m_SchemaClass);
    std::swap(m_SchemaType, other.m_SchemaType);
    std::swap(m_Items, other.m_Items);
}

SchemaObject::SchemaObject(SchemaObject const& other):
    TargetAddress{other.TargetAddress},
    m_MsgType{other.m_MsgType},
    m_MsgTypeStr{other.m_MsgTypeStr},
    m_Hop{other.m_Hop},
    m_Source{other.m_Source},
    m_SchemaClass{other.m_SchemaClass},
    m_SchemaType{other.m_SchemaType},
    m_Items{other.m_Items}
{
}

SchemaObject& SchemaObject::operator=(SchemaObject const& other)
{
    SchemaObject{other}.swap(*this);
    return *this;
}

SchemaObject::SchemaObject(SchemaObject &&other)
{
    TargetAddress = move(other.TargetAddress);
    m_MsgType = other.m_MsgType;
    m_MsgTypeStr = move(other.m_MsgTypeStr);
    m_Hop = other.m_Hop;
    m_Source = move(other.m_Source);
    m_SchemaClass = move(other.m_SchemaClass);
    m_SchemaType = move(other.m_SchemaType);
    m_Items = move(other.m_Items);
}

SchemaObject& SchemaObject::operator=(SchemaObject&& other) noexcept
{
   SchemaObject(move(other)).swap(*this);
   return *this;
}

void SchemaObject::ClearSchema()
{
    SetHop(1);
    SetMsgType(SchemaObject::stat);
    SetClass("");
    SetType("");
	ClearValues();
}

void SchemaObject::ClearValues()
{
	m_Items.clear();
}

void SchemaObject::SetHop(int hop)
{
    m_Hop = hop;
}

int SchemaObject::GetHop() const
{
    return m_Hop;
}

void SchemaObject::SetMsgType(ISchema::MsgType type)
{
    m_MsgType = type;
    m_MsgTypeStr = MsgTypeToString(type);
}

void SchemaObject::SetMsgType(const string& type)
{
    m_MsgType = MsgTypeToEnum(type);
    m_MsgTypeStr = type;
}

ISchema::MsgType SchemaObject::GetMsgType() const
{
    return m_MsgType;
}

const string& SchemaObject::GetMsgTypeStr() const
{
    return m_MsgTypeStr;
}

void SchemaObject::SetSource(const string& source)
{
    m_Source = source;
}

const string& SchemaObject::GetSource() const
{
    return m_Source;
}

void SchemaObject::SetTarget(const string& target)
{
    TargetAddress.SetAddress(target);
}

void SchemaObject::SetClass(const string& schemaClass)
{
    m_SchemaClass = schemaClass;
}

const string& SchemaObject::GetClass() const
{
    return m_SchemaClass;
}

void SchemaObject::SetType(const string& schemaType)
{
    m_SchemaType = schemaType;
}

const string& SchemaObject::GetType() const
{
    return m_SchemaType;
}

string SchemaObject::MsgTypeToString(ISchema::MsgType type)
{
    switch(type)
    {
        case ISchema::cmnd : return "xpl-cmnd";
        case ISchema::stat : return "xpl-stat";
        case ISchema::trig : return "xpl-trig";
        default :
            throw SchemaObject::Exception(0x0007, "SchemaObject::MsgTypeToString: Unknown message type (known types cmnd|stat|trig)");
    }
}

ISchema::MsgType SchemaObject::MsgTypeToEnum(const string& type)
{
    if(type == "xpl-cmnd") return ISchema::cmnd;
    if(type == "xpl-stat") return ISchema::stat;
    if(type == "xpl-trig") return ISchema::trig;

    throw SchemaObject::Exception(0x0008, "SchemaObject::MsgTypeToEnum: Unknown message type (known types cmnd|stat|trig)");
}

vector<SchemaObject::SchemaItem>::iterator SchemaObject::begin()
{
    return m_Items.begin();
}

vector<SchemaObject::SchemaItem>::iterator SchemaObject::end()
{
    return m_Items.end();
}

SchemaObject::SchemaItem* SchemaObject::GetItem(string key)
{
  	vector<SchemaItem>::iterator itItem;


	for(itItem=m_Items.begin(); itItem!=m_Items.end(); ++itItem)
	{
		if(key == itItem->GetKey()) break;
	}

    if(itItem == m_Items.end()) return nullptr;

    return &(*itItem);
}

string SchemaObject::GetValue(const string& key, size_t index)
{
	SchemaItem* pItem;

	pItem = GetItem(key);

    //if(pItem == nullptr) throw SchemaObject::Exception(0x0006, "SchemaObject::GetValue() unknown key");
    if(pItem == nullptr) return "";

    return pItem->GetValue(index);
}

void SchemaObject::AddValue(const string& key, const string& value)
{
	SchemaItem* pItem;

	pItem = GetItem(key);

    if(pItem == nullptr)
		m_Items.push_back(SchemaObject::SchemaItem(key, value));
	else
		pItem->AddValue(value);
}

void SchemaObject::SetValue(const string& key, const string& value, size_t index)
{
	SchemaItem* pItem;

	pItem = GetItem(key);

    if(pItem == nullptr)
    {
        if(index==0)
        {
            AddValue(key, value);
            return;
        }
        throw SchemaObject::Exception(0x0005, "SchemaObject::SetValue() unknown key");
    }

    pItem->SetValue(value, index);
}

string SchemaObject::ToMessage()
{
    return ToMessage(m_MsgType, "", "");
}

string SchemaObject::ToMessage(const string& source, const string& target)
{
    return ToMessage(m_MsgType, source, target);
}

string SchemaObject::ToMessage(MsgType type, const string& source, const string& target)
{
    ostringstream message;
    SchemaItem::Iterator itValue;
    std::vector<SchemaObject::SchemaItem>::iterator itItem;

    if(source!="") SetSource(source);
    if(target!="") SetTarget(target);

    message << MsgTypeToString(type) <<endl;
    message << "{" <<endl;
    message << "hop=" << m_Hop <<endl;
    message << "source=" << m_Source <<endl;
    message << "target=" << TargetAddress.ToString() <<endl;
    message << "}" <<endl;

    message << m_SchemaClass << "." << m_SchemaType << endl;
    message << "{" << endl;
  	for(itItem = m_Items.begin(); itItem != m_Items.end(); ++itItem)
    {
        for(itValue = itItem->begin(); itValue != itItem->end(); ++itValue)
        {
            message << itItem->GetKey() << "=" << *itValue << endl;
        }
    }

    message << "}";

    return message.str();
}

void SchemaObject::Check()
{
    //throw SchemaObject::Exception(0x0001, "SchemaObject::Check() must be implemented in derived sub class");
}

string SchemaObject::Parse(string msgRaw)
{
    ostringstream message;
    istringstream f(msgRaw);
    string line, key, value;
    bool bEnd;

    ClearSchema();
    transform(msgRaw.begin(), msgRaw.end(), msgRaw.begin(), ::tolower);

    /** Lecture du type de message  **/
    if(!getline(f, line))
        throw SchemaObject::Exception(0x5001, "SchemaObject::Parse() unable to get the first line");
    if(line=="xpl-cmnd")
        SetMsgType(ISchema::cmnd);
    else if(line=="xpl-stat")
        SetMsgType(ISchema::stat);
    else if(line=="xpl-trig")
        SetMsgType(ISchema::trig);
    else throw SchemaObject::Exception(0x5002, "SchemaObject::Parse() message type unknown");

    /** Controle présence {          **/
    if(!getline(f, line)) throw SchemaObject::Exception(0x5003, "SchemaObject::Parse() not more line");
    if(line!="{") throw SchemaObject::Exception(0x5004, "SchemaObject::Parse() '{' for begin head block not found");

    /** Lecture de l'entête          **/
    bEnd = false;
    while (getline(f, line))
    {
        if(line=="}") { bEnd = true; break; }
        if(!Split(line, '=', &key, &value)) throw SchemaObject::Exception(0x5005, "SchemaObject::Parse() value line without '='");

        if(key=="hop")
            SetHop(atoi(value.c_str()));
        else if(key=="source")
            SetSource(value);
        else if(key=="target")
            SetTarget(value);
    }
    /** Controle présence }         **/
    if(!bEnd) throw SchemaObject::Exception(0x5006, "SchemaObject::Parse() '}' for head block end not found");

    /** Lecture de la classe        **/
    if(!getline(f, line)) throw SchemaObject::Exception(0x5003, "SchemaObject::Parse() not more line");
    if(!Split(line, '.', &key, &value)) throw SchemaObject::Exception(0x5007, "SchemaObject::Parse() class without '.'");
    SetClass(key);
    SetType(value);

    /** Controle présence {         **/
    if(!getline(f, line)) throw SchemaObject::Exception(0x5003, "SchemaObject::Parse() not more line");
    if(line!="{") throw SchemaObject::Exception(0x5008, "SchemaObject::Parse() '{' for begin body block not found");

    /** Lecture du corps            **/
    bEnd = false;
    while (getline(f, line))
    {
        if(line.at(0)=='}') { bEnd = true; break; }
        if(!Split(line, '=', &key, &value)) throw SchemaObject::Exception(0x5005, "SchemaObject::Parse() value line without '='");
        AddValue(key, value);
    }

    /** Controle présence }         **/
    if(!bEnd) throw SchemaObject::Exception(0x5009, "SchemaObject::Parse() '}' for body block end not found");

    /** Message suivant         **/
    if(line.size()>1) message << line.substr(1, line.size()-1) << endl;
    while (getline(f, line))
        message << line << endl;

    return message.str();
}

bool SchemaObject::Split(string const& source, char const delimiter, string* key, string* value)
{
	size_t pos;
	size_t posEnd = source.size();

	pos = source.find(delimiter);
	if(pos==string::npos) return false;

    while((source[pos]==' ')&&(pos>0)) pos--;
    *key = source.substr(0, pos);

    while((source[posEnd]==' ')&&(posEnd>0)) posEnd--;
    *value = source.substr(pos+1, posEnd-pos);
    return true;
}

/****************************************************************************************************/
/*** Class SchemaItem                                                                               */
SchemaObject::SchemaItem::SchemaItem(std::string key)
{
    m_Key = key;
}

SchemaObject::SchemaItem::SchemaItem(std::string key, std::string value)
{
    m_Key = key;
    AddValue(value);
}

SchemaObject::SchemaItem::~SchemaItem()
{
    ClearValues();
}

void SchemaObject::SchemaItem::ClearValues()
{
    m_Values.clear();
}

string const& SchemaObject::SchemaItem::GetKey()
{
    return m_Key;
}

size_t SchemaObject::SchemaItem::Count()
{
    return m_Values.size();
}

void SchemaObject::SchemaItem::AddValue(string const& value)
{
    if(value.size() > 128) throw SchemaObject::Exception(0x0002, "SchemaItem::AddValue() value is too long (128 characters max)");
    m_Values.push_back(value);
}

void SchemaObject::SchemaItem::SetValue(string const& value, size_t index)
{
	if(index>=Count()) throw SchemaObject::Exception(0x0003, "SchemaItem::SetValue() index out of range");
	m_Values[index] = value;
}

string const SchemaObject::SchemaItem::GetValue(size_t index)
{
	if(index>=Count()) throw SchemaObject::Exception(0x0004, "SchemaItem::GetValue() index out of range");
	return m_Values[index];
}

SchemaObject::SchemaItem::Iterator SchemaObject::SchemaItem::begin()
{
    return SchemaItem::Iterator(m_Values.begin());
}

SchemaObject::SchemaItem::Iterator SchemaObject::SchemaItem::end()
{
    return SchemaItem::Iterator(m_Values.end());
}

/****************************************************************************************************/
/*** Class SchemaItem::Iterator                                                                                 */
SchemaObject::SchemaItem::Iterator::Iterator()
{
}

SchemaObject::SchemaItem::Iterator::Iterator(vector<string>::iterator vectorIterator)
{
    m_vectorIterator = vectorIterator;
}

const std::string& SchemaObject::SchemaItem::Iterator::operator*()
{
    return *m_vectorIterator;
}

SchemaObject::SchemaItem::Iterator SchemaObject::SchemaItem::Iterator::operator++()
{
    ++m_vectorIterator;
    return *this;
}

bool SchemaObject::SchemaItem::Iterator::operator==(Iterator const& a)
{
    return a.m_vectorIterator==m_vectorIterator;
}

bool SchemaObject::SchemaItem::Iterator::operator!=(Iterator const& a)
{
    return a.m_vectorIterator!=m_vectorIterator;
}

/****************************************************************************************************/
/*** Class Exception                                                                                */
SchemaObject::Exception::Exception(int number, string const& message) throw()
{
    m_number = number;
    m_message = message;
}

SchemaObject::Exception::~Exception() throw()
{
}

const char* SchemaObject::Exception::what() const throw()
{
    return m_message.c_str();
}

int SchemaObject::Exception::GetNumber() const throw()
{
    return m_number;
}

}
