#include <boost/beast/core.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;       

inline void fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}
