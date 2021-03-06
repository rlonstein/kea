// Copyright (C) 2017 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <asiolink/asio_wrapper.h>
#include <http/connection.h>
#include <http/connection_pool.h>
#include <boost/bind.hpp>
#include <iostream>

using namespace isc::asiolink;

namespace isc {
namespace http {

void
HttpConnection::
SocketCallback::operator()(boost::system::error_code ec, size_t length) {
    if (ec.value() == boost::asio::error::operation_aborted) {
        return;
    }
    callback_(ec, length);
}

HttpConnection:: HttpConnection(asiolink::IOService& io_service,
                                HttpAcceptor& acceptor,
                                HttpConnectionPool& connection_pool,
                                const HttpResponseCreatorPtr& response_creator,
                                const HttpAcceptorCallback& callback,
                                const long request_timeout)
    : request_timer_(io_service),
      request_timeout_(request_timeout),
      socket_(io_service),
      socket_callback_(boost::bind(&HttpConnection::socketReadCallback, this,
                                   _1, _2)),
      socket_write_callback_(boost::bind(&HttpConnection::socketWriteCallback,
                                         this, _1, _2)),
      acceptor_(acceptor),
      connection_pool_(connection_pool),
      response_creator_(response_creator),
      request_(response_creator_->createNewHttpRequest()),
      parser_(new HttpRequestParser(*request_)),
      acceptor_callback_(callback),
      buf_() {
    parser_->initModel();
}

HttpConnection::~HttpConnection() {
    close();
}

void
HttpConnection::close() {
    socket_.close();
}

void
HttpConnection::stopThisConnection() {
    try {
        connection_pool_.stop(shared_from_this());
    } catch (...) {
    }
}

void
HttpConnection::asyncAccept() {
    HttpAcceptorCallback cb = boost::bind(&HttpConnection::acceptorCallback,
                                          this, _1);
    try {
        acceptor_.asyncAccept(socket_, cb);

    } catch (const std::exception& ex) {
        isc_throw(HttpConnectionError, "unable to start accepting TCP "
                  "connections: " << ex.what());
    }
}

void
HttpConnection::doRead() {
    try {
        TCPEndpoint endpoint;
        socket_.asyncReceive(static_cast<void*>(buf_.data()), buf_.size(),
                             0, &endpoint, socket_callback_);

    } catch (const std::exception& ex) {
        stopThisConnection();
    }
}

void
HttpConnection::doWrite() {
    try {
        if (!output_buf_.empty()) {
            socket_.asyncSend(output_buf_.data(),
                              output_buf_.length(),
                              socket_write_callback_);
        }
    } catch (const std::exception& ex) {
        stopThisConnection();
    }
}

void
HttpConnection::asyncSendResponse(const ConstHttpResponsePtr& response) {
    output_buf_ = response->toString();
    doWrite();
}


void
HttpConnection::acceptorCallback(const boost::system::error_code& ec) {
    if (!acceptor_.isOpen()) {
        return;
    }

    if (ec) {
        stopThisConnection();
    }

    acceptor_callback_(ec);

    if (!ec) {
        request_timer_.setup(boost::bind(&HttpConnection::requestTimeoutCallback, this),
                             request_timeout_, IntervalTimer::ONE_SHOT);
        doRead();
    }
}

void
HttpConnection::socketReadCallback(boost::system::error_code ec, size_t length) {
    std::string s(&buf_[0], buf_[0] + length);
    parser_->postBuffer(static_cast<void*>(buf_.data()), length);
    parser_->poll();
    if (parser_->needData()) {
        doRead();

    } else {
        try {
            request_->finalize();
        } catch (...) {
        }

        HttpResponsePtr response = response_creator_->createHttpResponse(request_);
        asyncSendResponse(response);
    }
}

void
HttpConnection::socketWriteCallback(boost::system::error_code ec,
                                    size_t length) {
    if (length <= output_buf_.size()) {
        output_buf_.erase(0, length);
        doWrite();

    } else {
        output_buf_.clear();
    }
}

void
HttpConnection::requestTimeoutCallback() {
    HttpResponsePtr response =
        response_creator_->createStockHttpResponse(request_,
                                                   HttpStatusCode::REQUEST_TIMEOUT);
    asyncSendResponse(response);
}


} // end of namespace isc::http
} // end of namespace isc

