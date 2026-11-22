/*
 * AbiCollab - Code to enable the modification of remote documents.
 * Copyright (C) 2007 by Marc Maurer <uwog@uwog.net>
 * Copyright (C) 2007 One Laptop Per Child
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#ifndef ABICOLLAB_SERIALIZATION_H
#define ABICOLLAB_SERIALIZATION_H

#include <map>
#include <vector>
#include <string>
#include <string.h>
#include <ut_types.h>
#include <ut_string_class.h>

//
// Quick n' compact serialization implementation
// 			by Marc 'Foddex' Oude Kotte (marc@solcon.nl)
//

// POD type traits
#define DEFINE_POD_OPERATOR_ONE(T)					\
	Archive& operator<<( T& Val )					\
	{												\
		Serialize( &Val, sizeof(T) );				\
		return *this;								\
	}
#define DEFINE_POD_OPERATOR_VECTOR(T)				\
	Archive& operator<<( std::vector<T>& Val )		\
	{												\
		unsigned int count;							\
		if (isLoading()) {							\
			*this << COMPACT_INT(count);			\
			if (failed() || count > (1u << 20) || (count && sizeof(T) > static_cast<size_t>(-1) / count) || !canSerialize(static_cast<unsigned int>(count * sizeof(T)))) { \
				fail();								\
				return *this;						\
			}										\
			Val.resize( count );					\
		} else {									\
			count = Val.size();						\
			*this << COMPACT_INT(count);			\
		}											\
		if (count)									\
			Serialize( &Val[0], count*sizeof(T) );	\
		return *this;								\
	}

#define DEFINE_POD_OPERATOR(T)						\
	DEFINE_POD_OPERATOR_ONE(T)						\
	DEFINE_POD_OPERATOR_VECTOR(T)

class Archive;

/** Compact serializer, can safely be used to compact int's and unsigned int's.
 *	Basically any 4-bytes value. Don't use it on size_t's or longs, 64-bits
 *	machines will crash.
 *	Use the COMPACT_INT define to easily serialize integer values:
 *		e.g.
 *			ar << COMPACT_INT(m_iRev);
 */
#define COMPACT_INT(v) (*(CompactInt*)&v)
struct  CompactInt {
public:
	friend Archive& operator<<( Archive& ar, CompactInt& c );
protected:
	int Val;
};

/** Base archive */
class Archive
{
public:
	virtual ~Archive() {}
	bool isLoading() const { return m_bLoading; }
	bool isSaving() const { return !m_bLoading; }
	virtual void Serialize( void* Buffer, unsigned int Count ) = 0;
	virtual unsigned int Size() const = 0;
	virtual void Skip( unsigned int Count ) = 0;
	virtual bool EndOfFile() const = 0;
	bool failed() const { return m_bFailed; }
	void fail() { m_bFailed = true; }
protected:
	Archive( bool bLoading )
	: m_bLoading( bLoading )
	, m_bFailed( false )
	{}
	virtual bool canSerialize(unsigned int count) const
	{
		(void)count;
		return !m_bFailed;
	}
private:
	bool	m_bLoading;
	bool	m_bFailed;

public:
	// pod types
	DEFINE_POD_OPERATOR(unsigned char);
	DEFINE_POD_OPERATOR(unsigned short);
	DEFINE_POD_OPERATOR(unsigned int);
	DEFINE_POD_OPERATOR(UT_uint64);
	DEFINE_POD_OPERATOR(char);
	DEFINE_POD_OPERATOR(short);
	DEFINE_POD_OPERATOR(int);
	DEFINE_POD_OPERATOR(float);
	DEFINE_POD_OPERATOR(double);
	DEFINE_POD_OPERATOR_ONE(bool);

	// basic string
	Archive& operator<<( std::string& Val )
	{
		unsigned int s;	// NOTE: not size_t's !!
		if (isLoading()) {
			// get size, resize, and then get characters
			*this << COMPACT_INT(s);
			if (failed() || s > (16u << 20) || !canSerialize(s)) {
				fail();
				Val.clear();
				return *this;
			}
			Val.resize( s );
		} else {
			// save size & then characters
			s = static_cast<unsigned int>(Val.size());
			*this << COMPACT_INT(s);
		}
		if (s)
			Serialize( &Val[0], s );
		return *this;
	}
#if !defined(SERIALIZATION_TEST)
	// UT_UTF8String
	Archive& operator<<( UT_UTF8String& Val )
	{
		// easy, but not really the most efficient implementation :-)
		if (isLoading()) {
			std::string s;
			*this << s;
			Val = UT_UTF8String( s.c_str() );
		} else {
			std::string s = Val.utf8_str();
			*this << s;
		}
		return *this;
	}
#endif
	// Default, expects it's an object and will attempt to call serialize on it.
	// If compilation fails here, write a custom operator for the type you're trying
	// to serialize, or add a serialize member to your class.
	template<typename _T>
	Archive& operator<<( _T& Val )
	{
		Val.serialize( *this );
		return *this;
	}
#if !defined(SERIALIZATION_TEST)
	// Vector serialization (note: there are optimized version of this
	// for the various POD types)
	template<typename _K>
	Archive& operator<<( std::vector<_K>& Val )
	{
		unsigned int count;
		if (isLoading()) {
			Val.clear();
			*this << count;
			if (failed() || count > (1u << 20) || !canSerialize(count)) {
				fail();
				return *this;
			}
			Val.reserve(count < 4096u ? count : 4096u);
			for (unsigned int i = 0; i < count; ++i) {
				_K elem;
				*this << elem;
				if (failed()) {
					Val.clear();
					return *this;
				}
				Val.push_back(elem);
			}
		} else {
			count = static_cast<unsigned int>(Val.size());
			*this << count;
			for (unsigned int i=0; i<count; ++i) {
				*this << Val[i];
				if (failed())
					return *this;
			}
		}
		return *this;
	}
	// Map serialization
	template<typename _K, typename _V>
	Archive& operator<<( std::map<_K,_V>& Val )
	{
		unsigned int count;
		if (isLoading())
		{
			Val.clear();
			*this << count;
			if (failed() || count > (1u << 20) || !canSerialize(count))
			{
				fail();
				return *this;
			}
			for (unsigned int i=0; i<count; ++i)
			{
				_K k;
				_V v;
				*this << k << v;
				if (failed())
				{
					Val.clear();
					return *this;
				}
				Val.insert( typename std::map<_K,_V>::value_type( k, v ) );
			}
		}
		else
		{
			count = Val.size();
			*this << count;
			for (typename std::map<_K,_V>::iterator it=Val.begin(); it!=Val.end(); ++it) {
				*this << (_K&)(*it).first << (_V&)(*it).second;
			}
		}
		return *this;
	}
#endif
};

/** String based archive */
class StrArchive : public Archive
{
public:
	virtual unsigned int Size() const override { return m_sSource.size(); }
	const std::string& getData() const { return m_sSource; }
protected:
	StrArchive()
	: Archive( false )
	{}
	StrArchive( const std::string& sSource )
	: Archive( true )
	, m_sSource( sSource )
	{}
	std::string			m_sSource;
};

/** Input (loading) archive */
class IStrArchive : public StrArchive
{
public:
	IStrArchive( const std::string& sSource )
	: StrArchive( sSource )
	, m_uPosition( 0 )
	{}
	virtual bool canSerialize(unsigned int count) const override
	{
		if (failed())
			return false;
		if (!isLoading())
			return true;
		return count <= m_sSource.size() && m_uPosition <= m_sSource.size() - count;
	}
	virtual void Serialize(void* Buffer, unsigned int Count) override
	{
		if (!canSerialize(Count))
		{
			fail();
			if (Buffer && Count)
				memset(Buffer, 0, Count);
			return;
		}
		if (Count)
			memcpy( Buffer, &m_sSource[m_uPosition], Count );
		m_uPosition += Count;
	}
	virtual void Skip(unsigned int Count) override
	{
		if (!canSerialize(Count))
		{
			fail();
			return;
		}
		m_uPosition += Count;
	}
	virtual bool EndOfFile() const override
	{
		return failed() || m_uPosition >= m_sSource.size();
	}
protected:
	unsigned int				m_uPosition;
};

/** Output (saving) archive */
class OStrArchive : public StrArchive
{
public:
	OStrArchive()
	: StrArchive()
	{}
	virtual void Serialize(void* Buffer, unsigned int Count) override
	{
		unsigned int pos = m_sSource.size();
		m_sSource.resize( pos + Count );
		memcpy( &m_sSource[pos], Buffer, Count );
	}
	virtual void Skip(unsigned int /*Count*/) override
	{
#if !defined(SERIALIZATION_TEST)
		UT_ASSERT(UT_SHOULD_NOT_HAPPEN);
#endif
	}
	virtual bool EndOfFile() const override
	{
		return true;	// writing file is always at EOF ;)
	}
};

#endif /* ABICOLLAB_SERIALIZATION_H */
