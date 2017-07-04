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

#ifndef XPL_SCHEMAOBJECT_H
#define XPL_SCHEMAOBJECT_H

#include <sstream>
#include <string>
#include <vector>
#include "../Address.h"

namespace xPL
{

class ISchema
{
	public:
        enum MsgType {cmnd, stat, trig};
		virtual std::string ToMessage(MsgType type, const std::string& source, const std::string& target) = 0;
		virtual std::string ToMessage(const std::string& source, const std::string& target) = 0;
		virtual void Check() = 0;
};

class SchemaObject : public ISchema
{
    public:
        class SchemaItem;
        class Exception;
        SchemaObject();
        SchemaObject(ISchema::MsgType type, const std::string& schemaClass, const std::string& schemaType);
        virtual ~SchemaObject();

        void swap(SchemaObject& other);
        SchemaObject(SchemaObject const& other);
        SchemaObject& operator=(SchemaObject const& other);
        SchemaObject(SchemaObject &&other);
        SchemaObject& operator=(SchemaObject&& other) noexcept;

        void ClearSchema();
        void ClearValues();

        void SetMsgType(ISchema::MsgType type);
        void SetMsgType(const std::string& type);
        void SetHop(int hop);
        void SetSource(const std::string& source);
        void SetTarget(const std::string& target);
        void SetClass(const std::string& schemaClass);
        void SetType(const std::string& schemaType);

        ISchema::MsgType GetMsgType() const;
        int GetHop() const;
        const std::string& GetMsgTypeStr() const;
        const std::string& GetSource() const;
        const std::string& GetClass() const;
        const std::string& GetType() const;

        /// \brief    Get a string value
        /// \details  Get the value as string for a key.
        /// \param    key           Key to search
        /// \return   The value.
        std::string GetValue(const std::string& key, std::size_t index=0);

        /// \brief    Get a generic value
        /// \details  Get the value generic for a key.
        /// \param    key           Key to search
        /// \return   The value.
		template <class T> T GetValue(const std::string& key, std::size_t index=0)
        {
            std::string value = GetValue(key, index);

            std::istringstream iss(value);
            T val;
            iss >> val;
            return val;
        }

        /// \brief    Add a string value
        /// \details  Add the value as string for a key.
        /// \param    key           Key to add
        /// \param    value         Value to add
        void AddValue(const std::string& key, const std::string& value);

        /// \brief    Add a generic value
        /// \details  Add the value generic for a key.
        /// \param    key           Key to add
        /// \param    value         Value to add
		template <class T> void AddValue(const std::string& key, const T& value)
        {
            std::ostringstream oss;
            std::string str;

            oss << value;
            str = oss.str();
            AddValue(key, str);
        }

        /// \brief    Modify or add a string value
        /// \details  Modify the value as string for a key, or add if the key does not exist.
        /// \param    key           Key to add or modify
        /// \param    value         Value to set
        void SetValue(const std::string& key, const std::string& value, std::size_t index=0);

        /// \brief    Modify a generic value
        /// \details  Modify the value generic for a key, or add if the key does not exist.
        /// \param    key           Key to add or modify
        /// \param    value         Value to set
		template <class T> void SetValue(const std::string& key, const T& value, std::size_t index=0)
        {
            std::ostringstream oss;
            std::string str;

            oss << value;
            str = oss.str();
            SetValue(key, str, index);
        }
        SchemaObject::SchemaItem* GetItem(std::string key);
        std::vector<SchemaItem>::iterator begin();
        std::vector<SchemaItem>::iterator end();

        std::string Parse(std::string msgRaw);
        virtual void Check();
        std::string ToMessage(ISchema::MsgType type, const std::string& source, const std::string& target);
        std::string ToMessage(const std::string& source, const std::string& target);
        std::string ToMessage();
        Address TargetAddress;

    protected:
    private:
        bool Split(std::string const& source, char const delimiter, std::string* key, std::string* value);
        std::string MsgTypeToString(ISchema::MsgType type);
        ISchema::MsgType MsgTypeToEnum(const std::string& type);

        ISchema::MsgType m_MsgType;
        std::string m_MsgTypeStr;
        int m_Hop;
        std::string m_Source;
        std::string m_SchemaClass;
        std::string m_SchemaType;
        std::vector<SchemaObject::SchemaItem> m_Items;
};

class SchemaObject::SchemaItem
{
    public:
        class Iterator;
        SchemaItem(std::string key);
        SchemaItem(std::string key, std::string value);
        ~SchemaItem();

        std::string const& GetKey();
        std::size_t Count();
        SchemaItem::Iterator begin();
        SchemaItem::Iterator end();
        void AddValue(std::string const& value);
        void SetValue(std::string const& value, std::size_t index=0);
        std::string const GetValue(std::size_t index=0);
        void ClearValues();

    private:
        std::string m_Key;
        std::vector<std::string> m_Values;
};

/// \brief    Iterator for SchemaItem class
/// \details  Class to browse values of a SchemaItem class.
class SchemaObject::SchemaItem::Iterator
{
    public:
        /// \brief    Constructor of a iterator
        /// \details  Constructor to declare a iterator
        Iterator();
        /// \brief    Constructor of a iterator
        /// \details  Constructor to declare a iterator, call by SchemaItem::Begin
        Iterator(std::vector<std::string>::iterator vectorIterator);
        /// \brief    Overloading dereference operator
        /// \details  Overloading the dereference operator to get the values
        const std::string& operator*();
        /// \brief    Overloading pre-increment operator
        /// \details  Overloading the pre-increment operator to get the next value
        Iterator operator++();
        /// \brief    Overloading comparison operator ==
        /// \details  Overloading the comparison operator == to control the browse
        bool operator==(Iterator const& a);
        /// \brief    Overloading comparison operator !=
        /// \details  Overloading the comparison operator != to control the browse
        bool operator!=(Iterator const& a);

    private:
        std::vector<std::string>::iterator m_vectorIterator;
};

class SchemaObject::Exception: public std::exception
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
#endif // XPL_SCHEMAOBJECT_H
