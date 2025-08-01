//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_WEBSOCKET_AUTOSOCKET_AUTOSOCKET_H_INCLUDED
#define RIPPLE_WEBSOCKET_AUTOSOCKET_AUTOSOCKET_H_INCLUDED

#include <xrpl/basics/Log.h>
#include <xrpl/beast/net/IPAddressConversion.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/bind_handler.hpp>

// Socket wrapper that supports both SSL and non-SSL connections.
// Generally, handle it as you would an SSL connection.
// To force a non-SSL connection, just don't call async_handshake.
// To force SSL only inbound, call setSSLOnly.

class AutoSocket
{
public:
    using ssl_socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    using endpoint_type = boost::asio::ip::tcp::socket::endpoint_type;
    using socket_ptr = std::unique_ptr<ssl_socket>;
    using plain_socket = ssl_socket::next_layer_type;
    using lowest_layer_type = ssl_socket::lowest_layer_type;
    using handshake_type = ssl_socket::handshake_type;
    using error_code = boost::system::error_code;
    using callback = std::function<void(error_code)>;

public:
    AutoSocket(
        boost::asio::io_service& s,
        boost::asio::ssl::context& c,
        bool secureOnly,
        bool plainOnly)
        : mSecure(secureOnly)
        , mBuffer((plainOnly || secureOnly) ? 0 : 4)
        , j_{beast::Journal::getNullSink()}
    {
        mSocket = std::make_unique<ssl_socket>(s, c);
    }

    AutoSocket(boost::asio::io_service& s, boost::asio::ssl::context& c)
        : AutoSocket(s, c, false, false)
    {
    }

    bool
    isSecure()
    {
        return mSecure;
    }
    ssl_socket&
    SSLSocket()
    {
        return *mSocket;
    }
    plain_socket&
    PlainSocket()
    {
        return mSocket->next_layer();
    }

    beast::IP::Endpoint
    local_endpoint()
    {
        return beast::IP::from_asio(lowest_layer().local_endpoint());
    }

    beast::IP::Endpoint
    remote_endpoint()
    {
        return beast::IP::from_asio(lowest_layer().remote_endpoint());
    }

    lowest_layer_type&
    lowest_layer()
    {
        return mSocket->lowest_layer();
    }

    void
    swap(AutoSocket& s) noexcept
    {
        mBuffer.swap(s.mBuffer);
        mSocket.swap(s.mSocket);
        std::swap(mSecure, s.mSecure);
    }

    boost::system::error_code
    cancel(boost::system::error_code& ec)
    {
        return lowest_layer().cancel(ec);
    }

    void
    async_handshake(handshake_type type, callback cbFunc)
    {
        if ((type == ssl_socket::client) || (mSecure))
        {
            // must be ssl
            mSecure = true;
            mSocket->async_handshake(type, cbFunc);
        }
        else if (mBuffer.empty())
        {
            // must be plain
            mSecure = false;
            post(
                mSocket->get_executor(),
                boost::beast::bind_handler(cbFunc, error_code()));
        }
        else
        {
            // autodetect
            mSocket->next_layer().async_receive(
                boost::asio::buffer(mBuffer),
                boost::asio::socket_base::message_peek,
                std::bind(
                    &AutoSocket::handle_autodetect,
                    this,
                    cbFunc,
                    std::placeholders::_1,
                    std::placeholders::_2));
        }
    }

    template <typename ShutdownHandler>
    void
    async_shutdown(ShutdownHandler handler)
    {
        if (isSecure())
            mSocket->async_shutdown(handler);
        else
        {
            error_code ec;
            try
            {
                lowest_layer().shutdown(plain_socket::shutdown_both);
            }
            catch (boost::system::system_error& e)
            {
                ec = e.code();
            }
            post(
                mSocket->get_executor(),
                boost::beast::bind_handler(handler, ec));
        }
    }

    template <typename Seq, typename Handler>
    void
    async_read_some(Seq const& buffers, Handler handler)
    {
        if (isSecure())
            mSocket->async_read_some(buffers, handler);
        else
            PlainSocket().async_read_some(buffers, handler);
    }

    template <typename Seq, typename Condition, typename Handler>
    void
    async_read_until(Seq const& buffers, Condition condition, Handler handler)
    {
        if (isSecure())
            boost::asio::async_read_until(
                *mSocket, buffers, condition, handler);
        else
            boost::asio::async_read_until(
                PlainSocket(), buffers, condition, handler);
    }

    template <typename Allocator, typename Handler>
    void
    async_read_until(
        boost::asio::basic_streambuf<Allocator>& buffers,
        std::string const& delim,
        Handler handler)
    {
        if (isSecure())
            boost::asio::async_read_until(*mSocket, buffers, delim, handler);
        else
            boost::asio::async_read_until(
                PlainSocket(), buffers, delim, handler);
    }

    template <typename Allocator, typename MatchCondition, typename Handler>
    void
    async_read_until(
        boost::asio::basic_streambuf<Allocator>& buffers,
        MatchCondition cond,
        Handler handler)
    {
        if (isSecure())
            boost::asio::async_read_until(*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read_until(
                PlainSocket(), buffers, cond, handler);
    }

    template <typename Buf, typename Handler>
    void
    async_write(Buf const& buffers, Handler handler)
    {
        if (isSecure())
            boost::asio::async_write(*mSocket, buffers, handler);
        else
            boost::asio::async_write(PlainSocket(), buffers, handler);
    }

    template <typename Allocator, typename Handler>
    void
    async_write(
        boost::asio::basic_streambuf<Allocator>& buffers,
        Handler handler)
    {
        if (isSecure())
            boost::asio::async_write(*mSocket, buffers, handler);
        else
            boost::asio::async_write(PlainSocket(), buffers, handler);
    }

    template <typename Buf, typename Condition, typename Handler>
    void
    async_read(Buf const& buffers, Condition cond, Handler handler)
    {
        if (isSecure())
            boost::asio::async_read(*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read(PlainSocket(), buffers, cond, handler);
    }

    template <typename Allocator, typename Condition, typename Handler>
    void
    async_read(
        boost::asio::basic_streambuf<Allocator>& buffers,
        Condition cond,
        Handler handler)
    {
        if (isSecure())
            boost::asio::async_read(*mSocket, buffers, cond, handler);
        else
            boost::asio::async_read(PlainSocket(), buffers, cond, handler);
    }

    template <typename Buf, typename Handler>
    void
    async_read(Buf const& buffers, Handler handler)
    {
        if (isSecure())
            boost::asio::async_read(*mSocket, buffers, handler);
        else
            boost::asio::async_read(PlainSocket(), buffers, handler);
    }

    template <typename Seq, typename Handler>
    void
    async_write_some(Seq const& buffers, Handler handler)
    {
        if (isSecure())
            mSocket->async_write_some(buffers, handler);
        else
            PlainSocket().async_write_some(buffers, handler);
    }

protected:
    void
    handle_autodetect(
        callback cbFunc,
        error_code const& ec,
        size_t bytesTransferred)
    {
        using namespace ripple;

        if (ec)
        {
            JLOG(j_.warn()) << "Handle autodetect error: " << ec;
            cbFunc(ec);
        }
        else if (
            (mBuffer[0] < 127) && (mBuffer[0] > 31) &&
            ((bytesTransferred < 2) ||
             ((mBuffer[1] < 127) && (mBuffer[1] > 31))) &&
            ((bytesTransferred < 3) ||
             ((mBuffer[2] < 127) && (mBuffer[2] > 31))) &&
            ((bytesTransferred < 4) ||
             ((mBuffer[3] < 127) && (mBuffer[3] > 31))))
        {
            // not ssl
            JLOG(j_.trace()) << "non-SSL";
            mSecure = false;
            cbFunc(ec);
        }
        else
        {
            // ssl
            JLOG(j_.trace()) << "SSL";
            mSecure = true;
            mSocket->async_handshake(ssl_socket::server, cbFunc);
        }
    }

private:
    socket_ptr mSocket;
    bool mSecure;
    std::vector<char> mBuffer;
    beast::Journal j_;
};

#endif
