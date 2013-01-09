/*
 * Copyright (c) 2013, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef WEBSOCKETPP_TRANSPORT_SECURITY_TLS_HPP
#define WEBSOCKETPP_TRANSPORT_SECURITY_TLS_HPP

#include <websocketpp/transport/asio/security/base.hpp>
#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/common/functional.hpp>
#include <websocketpp/common/memory.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>

#include <iostream>
#include <string>

namespace websocketpp {
namespace transport {
namespace asio {
namespace tls_socket {

typedef lib::function<void(connection_hdl,boost::asio::ssl::stream<
    boost::asio::ip::tcp::socket>::lowest_layer_type&)> socket_init_handler;
typedef lib::function<lib::shared_ptr<boost::asio::ssl::context>(connection_hdl)>
    tls_init_handler;

/// TLS enabled Boost ASIO connection socket component
/**
 * transport::asio::tls_socket::connection impliments a secure connection socket
 * component that uses Boost ASIO's ssl::stream to wrap an ip::tcp::socket.
 */
class connection {
public:
    /// Type of this connection socket component
	typedef connection type;
	/// Type of a shared pointer to this connection socket component
    typedef lib::shared_ptr<type> ptr;
    
    /// Type of the ASIO socket being used
	typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_type;
    /// Type of a shared pointer to the ASIO socket being used
    typedef lib::shared_ptr<socket_type> socket_ptr;
    /// Type of a pointer to the ASIO io_service being used
	typedef boost::asio::io_service* io_service_ptr;
    /// Type of a shared pointer to the ASIO TLS context being used
	typedef lib::shared_ptr<boost::asio::ssl::context> context_ptr;
    /// Type of a shared pointer to the ASIO timer being used
	typedef lib::shared_ptr<boost::asio::deadline_timer> timer_ptr;
	
	typedef boost::system::error_code boost_error;
	
	explicit connection() {
		std::cout << "transport::asio::tls_socket::connection constructor" 
                  << std::endl; 
	}
	
    /// Check whether or not this connection is secure
    /**
     * @return Wether or not this connection is secure
     */
	bool is_secure() const {
	    return true;
	}
	
	/// Retrieve a pointer to the underlying socket
	/**
	 * This is used internally. It can also be used to set socket options, etc
	 */
	socket_type::lowest_layer_type& get_raw_socket() {
		return m_socket->lowest_layer();
	}
	
	/// Retrieve a pointer to the wrapped socket
	/**
	 * This is used internally.
	 */
	socket_type& get_socket() {
		return *m_socket;
	}

    /// Set the socket initialization handler
    /**
     * The socket initialization handler is called after the socket object is
     * created but before it is used. This gives the application a chance to
     * set any ASIO socket options it needs.
     *
     * @param h The new socket_init_handler
     */
    void set_socket_init_handler(socket_init_handler h) {
        m_socket_init_handler = h;
    }

    /// Set TLS init handler
    /**
     * The tls init handler is called when needed to request a TLS context for
     * the library to use. A TLS init handler must be set and it must return a
     * valid TLS context in order for this endpoint to be able to initialize 
     * TLS connections
     *
     * @param h The new tls_init_handler
     */
    void set_tls_init_handler(tls_init_handler h) {
        m_tls_init_handler = h;
    }
protected:
	/// Perform one time initializations
	/**
	 * init_asio is called once immediately after construction to initialize
	 * boost::asio components to the io_service
	 *
	 * @param service A pointer to the endpoint's io_service
	 * @param strand A shared pointer to the connection's asio strand
	 * @param is_server Whether or not the endpoint is a server or not.
	 */
    lib::error_code init_asio (io_service_ptr service, bool is_server) {
        std::cout << "transport::security::tls_socket::init_asio" << std::endl;
        if (!m_tls_init_handler) {
            std::cout << "missing_tls_init_handler" << std::endl;
			return socket::make_error(socket::error::missing_tls_init_handler);
        }
        m_context = m_tls_init_handler(m_hdl);
    	
    	if (!m_context) {
			return socket::make_error(socket::error::invalid_tls_context);
		}
    	
    	m_socket.reset(new socket_type(*service,*m_context));
    	
    	m_timer.reset(new boost::asio::deadline_timer(
    		*service,
    		boost::posix_time::seconds(0))
    	);
    	
    	m_io_service = service;
    	m_is_server = is_server;
    	
    	return lib::error_code();
    }
	
	/// Initialize security policy for reading
	void init(init_handler callback) {
		if (m_socket_init_handler) {
            m_socket_init_handler(m_hdl,get_raw_socket());
        }

		// register timeout
		m_timer->expires_from_now(boost::posix_time::milliseconds(5000));
		// TEMP
		m_timer->async_wait(
            lib::bind(
                &type::handle_timeout,
                this,
                callback,
                lib::placeholders::_1
            )
        );
		
		// TLS handshake
		m_socket->async_handshake(
			get_handshake_type(),
			lib::bind(
				&type::handle_init,
				this,
				callback,
				lib::placeholders::_1
			)
		);
	}
	
    /// Sets the connection handle
    /**
     * The connection handle is passed to any handlers to identify the 
     * connection
     *
     * @param hdl The new handle
     */
    void set_handle(connection_hdl hdl) {
        m_hdl = hdl;
    }

	void handle_timeout(init_handler callback, const 
		boost::system::error_code& error)
	{
		if (error) {
			if (error.value() == boost::asio::error::operation_aborted) {
				// The timer was cancelled because the handshake succeeded.
				return;
			}
			
			// Some other ASIO error, pass it through
			// TODO: make this error pass through better
    		callback(socket::make_error(socket::error::pass_through));
    		return;
    	}
    	
    	callback(socket::make_error(socket::error::tls_handshake_timeout));
	}
	
	void handle_init(init_handler callback, const 
		boost::system::error_code& error)
	{
		/// stop waiting for our handshake timer.
		m_timer->cancel();
		
		if (error) {
			// TODO: make this error pass through better
    		callback(socket::make_error(socket::error::pass_through));
    		return;
    	}
		
		callback(lib::error_code());
	}
	
    void shutdown() {
        boost::system::error_code ec;
        m_socket->shutdown(ec);

        // TODO: error handling
    }
private:
	socket_type::handshake_type get_handshake_type() {
        if (m_is_server) {
            return boost::asio::ssl::stream_base::server;
        } else {
            return boost::asio::ssl::stream_base::client;
        }
    }
	
	io_service_ptr	    m_io_service;
	context_ptr		    m_context;
	socket_ptr		    m_socket;
	timer_ptr		    m_timer;
	bool			    m_is_server;
    
    connection_hdl      m_hdl;
    socket_init_handler m_socket_init_handler;
    tls_init_handler    m_tls_init_handler;
};

/// TLS enabled Boost ASIO endpoint socket component
/**
 * transport::asio::tls_socket::endpoint impliments a secure endpoint socket
 * component that uses Boost ASIO's ssl::stream to wrap an ip::tcp::socket.
 */
class endpoint {
public:
    /// The type of this endpoint socket component
    typedef endpoint type;

    /// The type of the corresponding connection socket component
    typedef connection socket_con_type;
    /// The type of a shared pointer to the corresponding connection socket
    /// component.
    typedef typename socket_con_type::ptr socket_con_ptr;

    explicit endpoint() {
        std::cout << "transport::asio::tls_socket::endpoint constructor"
                  << std::endl;
    }

    /// Checks whether the endpoint creates secure connections
    /**
     * @return Wether or not the endpoint creates secure connections
     */
    bool is_secure() const {
        return true;
    }

    /// Set socket init handler
    /**
     * The socket init handler is called after a connection's socket is created
     * but before it is used. This gives the end application an opportunity to 
     * set asio socket specific parameters.
     *
     * @param h The new socket_init_handler
     */
    void set_socket_init_handler(socket_init_handler h) {
        m_socket_init_handler = h;
    }
    
    /// Set TLS init handler
    /**
     * The tls init handler is called when needed to request a TLS context for
     * the library to use. A TLS init handler must be set and it must return a
     * valid TLS context in order for this endpoint to be able to initialize 
     * TLS connections
     *
     * @param h The new tls_init_handler
     */
    void set_tls_init_handler(tls_init_handler h) {
        m_tls_init_handler = h;
    }
protected:
    /// Initialize a connection
    /**
     * Called by the transport after a new connection is created to initialize
     * the socket component of the connection.
     */
    void init(socket_con_ptr scon) {
        std::cout << "transport::asio::tls_socket::init" << std::endl;
        scon->set_socket_init_handler(m_socket_init_handler);
        scon->set_tls_init_handler(m_tls_init_handler);
    }

private:
    socket_init_handler m_socket_init_handler;
    tls_init_handler m_tls_init_handler;
};

} // namespace tls_socket
} // namespace asio
} // namespace transport
} // namespace websocketpp

#endif // WEBSOCKETPP_TRANSPORT_SECURITY_TLS_HPP
