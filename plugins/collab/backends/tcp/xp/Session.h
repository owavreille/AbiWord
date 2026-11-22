/* Copyright (C) 2007,2008 by Marc Maurer <uwog@uwog.net>
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

#ifndef __SESSION__
#define __SESSION__

#include <deque>
#include <functional>
#include <memory>

#include <boost/bind/bind.hpp>
#include <sync/xp/lock.h>
#include <sync/xp/Synchronizer.h>

// 64MB seems reasonable enough for now...
#define MAX_PACKET_DATA_SIZE 64*1024*1024

class TCPAccountHandler;

class Session : public Synchronizer, public std::enable_shared_from_this<Session>
{
public:
	Session(boost::asio::io_service& io_service, std::function<void (std::shared_ptr<Session>)> ef)
		: Synchronizer(std::bind(&Session::_signal, this)),
		socket(io_service),
		queue_protector(),
		packet_size(0),
		packet_data(nullptr),
		packet_size_write(0),
		packet_data_write(nullptr),
		write_active(false),
		m_ef(ef)
	{
	}

	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;

	void connect(boost::asio::ip::tcp::resolver::iterator& iterator)
	{
		socket.connect(*iterator);
	}

	// TODO: don't expose this
	boost::asio::ip::tcp::socket& getSocket()
	{
		return socket;
	}

	std::string getRemoteAddress()
	{
		return socket.remote_endpoint().address().to_string();
	}

	unsigned short getRemotePort()
	{
		return socket.remote_endpoint().port();
	}

	void push(int size, char* data)
	{
		bool overflow = false;
		{
			abicollab::scoped_lock lock(queue_protector);
			size_t queued = 0;
			for (std::deque< std::pair<int, char*> >::const_iterator it = incoming.begin(); it != incoming.end(); ++it)
				queued += static_cast<size_t>(it->first > 0 ? it->first : 0);
			if (incoming.size() >= 32 || queued > MAX_PACKET_DATA_SIZE)
				overflow = true;
			else
				incoming.push_back( std::pair<int, char*>(size, data) );
		}
		if (overflow)
		{
			g_free(data);
			disconnect();
			return;
		}
		Synchronizer::signal();
	}

	/*
		Only called fron the abiword main loop
	*/
	bool pop(int& size, char** data)
	{
		abicollab::scoped_lock lock(queue_protector);
		if (incoming.empty())
			return false;
		std::pair<int, char*> p = incoming.front();
		size = p.first;
		*data = p.second;
		incoming.pop_front();
		return true;
	}

	void asyncReadHeader()
	{
		UT_DEBUGMSG(("Session::asyncReadHeader()\n"));
		packet_data = nullptr; // just to be sure we'll never touch a datablock we might have read before
		boost::asio::async_read(socket,
			boost::asio::buffer(&packet_size, 4),
			boost::bind(&Session::asyncReadHeaderHandler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	void asyncWrite(int size, const char* data)
	{
		if (size < 0 || (size > 0 && !data))
			return;
		char* store_data = reinterpret_cast<char*>(g_malloc(size ? size : 1));
		if (!store_data)
			return;
		if (size)
			memcpy(store_data, data, size);

		bool start = false;
		{
			abicollab::scoped_lock lock(queue_protector);
			start = !write_active;
			outgoing.push_back(std::pair<int, char*>(size, store_data));
			if (start)
			{
				write_active = true;
				packet_size_write = size;
				packet_data_write = store_data;
			}
		}
		if (!start)
			return;

		UT_DEBUGMSG(("sending datablock of length: %d\n", size));
		boost::asio::async_write(socket,
			boost::asio::buffer(&packet_size_write, 4),
			boost::bind(&Session::asyncWriteHeaderHandler, shared_from_this(), boost::asio::placeholders::error));
	}

	/*
		Only called fron the abiword main loop
	*/
	bool isConnected()
	{
		return socket.is_open();
	}

	void disconnect()
	{
		UT_DEBUGMSG(("Session::disconnect()\n"));
		if (socket.is_open())
		{
			boost::system::error_code ecs;
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ecs);
			if (ecs) {
				UT_DEBUGMSG(("Error shutting down socket: %s\n", ecs.message().c_str()));
			}
			boost::system::error_code ecc;
			socket.close(ecc);
			if (ecc) {
				UT_DEBUGMSG(("Error closing socket: %s\n", ecc.message().c_str()));
			}
		}
		{
			abicollab::scoped_lock lock(queue_protector);
			for (std::deque< std::pair<int, char*> >::iterator it = incoming.begin(); it != incoming.end(); ++it)
				g_free(it->second);
			incoming.clear();
			if (!write_active)
			{
				for (std::deque< std::pair<int, char*> >::iterator it = outgoing.begin(); it != outgoing.end(); ++it)
					g_free(it->second);
				outgoing.clear();
				packet_data_write = nullptr;
			}
		}
		UT_DEBUGMSG(("Socket closed, signalling mainloop\n"));
		signal();
	}

private:
	void _signal()
	{
		UT_DEBUGMSG(("Session::_signal()\n"));
		m_ef(shared_from_this());
	}

	void asyncReadHeaderHandler(const boost::system::error_code& error,
		std::size_t bytes_transferred)
	{
		if (error)
		{
			UT_DEBUGMSG(("asyncReadHeaderHandler error: %s\n", error.message().c_str()));
			disconnect();
			return;
		}

		if (bytes_transferred != 4)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			disconnect(); // TODO: should not happen, handle this
			return;
		}

		if (packet_size < 0 || packet_size > MAX_PACKET_DATA_SIZE)
		{
			UT_DEBUGMSG(("Packet size (%d bytes) error - min size: 0, max size %d\n", packet_size, MAX_PACKET_DATA_SIZE));
			disconnect();
			return;
		}

		UT_DEBUGMSG(("going to read datablock of length: %d\n", packet_size));
		// now continue reading the packet data
		packet_data = reinterpret_cast<char*>(g_malloc(packet_size ? packet_size : 1));
		if (!packet_data)
		{
			disconnect();
			return;
		}
		boost::asio::async_read(socket,
			boost::asio::buffer(packet_data, packet_size),
			boost::bind(&Session::asyncReadHandler, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	void asyncReadHandler(const boost::system::error_code& error,
		std::size_t bytes_transferred)
	{
		if (error)
		{
			UT_DEBUGMSG(("asyncReadHandler generic error\n"));
			g_free(packet_data);
			packet_data = nullptr;
			disconnect();
			return;
		}

		if (bytes_transferred != std::size_t(packet_size))
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			g_free(packet_data);
			packet_data = nullptr;
			disconnect(); // TODO: should not happen, handle this
			return;
		}

		push(packet_size, packet_data);
		// start over for a new packet
		asyncReadHeader();
	}

	void asyncWriteHeaderHandler(const boost::system::error_code& ec)
	{
		UT_DEBUGMSG(("Session::asyncWriteHeaderHandler()\n"));
		if (ec)
		{
			UT_DEBUGMSG(("asyncWriteHeaderHandler generic error\n"));
			{
				abicollab::scoped_lock lock(queue_protector);
				write_active = false;
			}
			disconnect();
			return;
		}

		// write the packet body
		boost::asio::async_write(socket,
			boost::asio::buffer(packet_data_write, packet_size_write),
			boost::bind(&Session::asyncWriteHandler, shared_from_this(), boost::asio::placeholders::error));
	}

	void asyncWriteHandler(const boost::system::error_code& ec)
	{
		UT_DEBUGMSG(("Session::asyncWriteHandler()\n"));
		char* finished = nullptr;
		std::deque< std::pair<int, char*> > drop;
		bool more = false;
		{
			abicollab::scoped_lock lock(queue_protector);
			finished = packet_data_write;
			if (!outgoing.empty() && outgoing.front().second == finished)
				outgoing.pop_front();
			if (ec)
			{
				drop.swap(outgoing);
				packet_data_write = nullptr;
				write_active = false;
			}
			else if (!outgoing.empty())
			{
				more = true;
				packet_size_write = outgoing.front().first;
				packet_data_write = outgoing.front().second;
			}
			else
			{
				packet_data_write = nullptr;
				write_active = false;
			}
		}
		g_free(finished);
		for (std::deque< std::pair<int, char*> >::iterator it = drop.begin(); it != drop.end(); ++it)
			g_free(it->second);
		if (ec)
		{
			UT_DEBUGMSG(("asyncWriteHandler generic error\n"));
			disconnect();
			return;
		}
		if (!more)
			return;

		UT_DEBUGMSG(("sending datablock of length: %d\n", packet_size_write));
		boost::asio::async_write(socket,
			boost::asio::buffer(&packet_size_write, 4),
			boost::bind(&Session::asyncWriteHeaderHandler, shared_from_this(), boost::asio::placeholders::error));
	}

	boost::asio::ip::tcp::socket					socket;
	abicollab::mutex 						queue_protector;
	std::deque< std::pair<int, char*> >		incoming;
	std::deque< std::pair<int, char*> >		outgoing;

	int										packet_size; // state needed for async reads
	char*									packet_data; // state needed for async reads

	int										packet_size_write; // state needed for async writes
	char*									packet_data_write; // state needed for async writes
	bool									write_active;

	std::function<void (std::shared_ptr<Session>)>		m_ef;
};

#endif /* __SESSION__ */
