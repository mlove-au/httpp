/*
 * Part of HTTPP.
 *
 * Distributed under the 3-clause BSD licence (See LICENCE.TXT file at the
 * project root).
 *
 * Copyright (c) 2013 Thomas Sanchez.  All rights reserved.
 *
 */

#ifndef _HTTPP_HTPP_RESPONSE_HPP_
# define _HTTPP_HTPP_RESPONSE_HPP_

# include <functional>
# include <vector>
# include <string>

# include <boost/asio.hpp>

# include "Protocol.hpp"

namespace HTTPP
{
namespace HTTP
{

class Response
{
    static char const HTTP_DELIMITER[2];
    static char const HEADER_SEPARATOR[2];
    static char const END_OF_STREAM_MARKER[5];

public:

    Response();
    Response(HttpCode code);
    Response(HttpCode code, const std::string& body);
    Response(HttpCode code, std::string&& body);
    Response(HttpCode code, std::function<std::string()> chunkedBodyCallback);

    Response& setCode(HttpCode code)
    {
        code_ = code;
        return *this;
    }

    Response& addHeader(const std::string& k, const std::string& v);
    Response& setBody(const std::string& body);
    Response& setBody(std::function<std::string()> chunkedBodyCallback);


    template <typename Writer, typename WriteHandler>
    void sendResponse(Writer& writer, WriteHandler&& writeHandler)
    {
        generate_status_string();

        finalize_response_headers();

        std::vector<boost::asio::const_buffer> buffers;
        buffers.push_back(boost::asio::buffer(status_string_));
        buffers.push_back(boost::asio::buffer(HTTP_DELIMITER));
        for (auto const& header : headers_)
        {
            buffers.push_back(boost::asio::buffer(header.first));
            buffers.push_back(boost::asio::buffer(HEADER_SEPARATOR));
            buffers.push_back(boost::asio::buffer(header.second));
            buffers.push_back(boost::asio::buffer(HTTP_DELIMITER));
        }
        buffers.push_back(boost::asio::buffer(HTTP_DELIMITER));

        
        if (!is_chunked_enconding())
        {
            // append full body and send.
            if (!body_.empty())
            {
                buffers.push_back(boost::asio::buffer(body_));
            }
            boost::asio::async_write(writer, buffers, writeHandler);
        }
        else
        {            
            // send headers, then chunks individually.
            boost::asio::async_write(writer, buffers, [this, &writer, buffers, writeHandler](boost::system::error_code const& ec, size_t size)
            {
                if (ec)
                {
                    writeHandler(ec, size);
                }
                else
                {                    
                    sendChunk(writer, writeHandler);
                }
            });
            
        }
    }

    template <typename Writer, typename WriteHandler>
    void sendChunk(Writer& writer, WriteHandler writeHandler)
    {
        // try to generate the next chunk.
        current_chunk_ = chunkedBodyCallback_();

        if (!current_chunk_.empty())
        {        
            // Format the chunk header and chunk body. NB. Ensure objects used in asio::buffer retain lifetime until the
            // async_write handler has been invoked.
            std::vector<boost::asio::const_buffer> buffers;

            std::stringstream header;
            header << std::hex << current_chunk_.size();                 
            current_chunk_header_=header.str();

            buffers.push_back(boost::asio::buffer(current_chunk_header_));
            buffers.push_back(boost::asio::buffer(HTTP_DELIMITER));
            buffers.push_back(boost::asio::buffer(current_chunk_));
            buffers.push_back(boost::asio::buffer(HTTP_DELIMITER));
            
            boost::asio::async_write(writer, buffers, [this,&writer,writeHandler](boost::system::error_code const& ec, size_t size)
            {
                if (ec)
                {
                    // notify the original caller that an error occured during sending
                    writeHandler(ec, size);                    
                }
                else
                {                    
                    sendChunk(writer, writeHandler);
                }
            });
        }
        else
        {        
            // Send end of stream marker.
            boost::asio::async_write(writer, boost::asio::buffer(END_OF_STREAM_MARKER), writeHandler);                            
        }
    }


    bool connectionShouldBeClosed() const
    {
        return should_be_closed_;
    }

    Response& connectionShouldBeClosed(bool b)
    {
        should_be_closed_ = b;
        return *this;
    }

    const std::string& body() const noexcept
    {
        return body_;
    }

    bool isComplete() const
    {
        return code_ != HttpCode::Continue;
    }

private:
    bool is_chunked_enconding() const
    {
        return chunkedBodyCallback_ != 0;
    }

    void generate_status_string()
    {
        status_string_ = "HTTP/" + std::to_string(major_) + "." +
                         std::to_string(minor_) + " " +
                         std::to_string(static_cast<unsigned int>(code_)) +
                         " " + getDefaultMessage(code_);
    }

    void finalize_response_headers()
    {
        if (is_chunked_enconding())
        {
            headers_.emplace_back("Transfer-Encoding", "chunked");
        }
        else
        {
            headers_.emplace_back("Content-Length", std::to_string(body_.size()));
        }        
    }


private:
    HttpCode code_;
    std::string body_;
    std::function<std::string()> chunkedBodyCallback_;
    std::string current_chunk_header_;
    std::string current_chunk_;
    int major_ = 1;
    int minor_ = 1;
    std::vector<Header> headers_;
    bool should_be_closed_ = false;
    std::string status_string_;

};

} // namespace HTTP
} // namespace HTTPP

#endif // ! _HTTPP_HTPP_RESPONSE_HPP_
