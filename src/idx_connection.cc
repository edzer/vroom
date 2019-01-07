#include "idx.h"

#include <fstream>

#include <Rcpp.h>

// clang-format off
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wkeyword-macro"
#define class class_name
#define private private_ptr
#include <R_ext/Connections.h>
#undef class
#undef private
# pragma clang diagnostic pop
// clang-format on

#if R_CONNECTIONS_VERSION != 1
#error "Missing or unsupported connection API in R"
#endif

#if R_VERSION < R_Version(3, 3, 0)
/* R before 3.3.0 didn't have R_GetConnection() */
extern Rconnection getConnection(int n);
static Rconnection R_GetConnection(SEXP sConn) {
  return getConnection(asInteger(sConn));
}
#endif

std::tuple<
    std::shared_ptr<std::vector<size_t> >,
    size_t,
    mio::shared_mmap_source>
create_index_connection(
    SEXP in,
    const std::string& out_file,
    const char delim,
    const char quote,
    R_xlen_t chunk_size) {
  size_t columns = 0;

  std::vector<size_t> values;
  std::vector<size_t> quotes;

  values.reserve(128);

  values.push_back(0);

  std::ofstream out_h(
      out_file.c_str(),
      std::fstream::out | std::fstream::binary | std::fstream::trunc);

  R_xlen_t cur_loc = 0;

  auto con = R_GetConnection(in);

  std::vector<char> buf(chunk_size);

  auto sz = R_ReadConnection(con, buf.data(), chunk_size);
  while (sz > 0) {
    for (const auto& c : buf) {
      if (c == '\n') {
        if (columns == 0) {
          columns = values.size();
        }
        values.push_back(cur_loc + 1);
      }

      else if (c == delim) {
        // Rcpp::Rcout << id << '\n';
        values.push_back(cur_loc + 1);
      }

      else if (c == quote) {
        quotes.push_back(cur_loc);
      }

      ++cur_loc;
    }
    out_h.write(buf.data(), sz);

    sz = R_ReadConnection(con, buf.data(), chunk_size);
  }

  out_h.close();

  std::error_code error;
  mio::shared_mmap_source mmap = mio::make_mmap_source(out_file, error);
  if (error) {
    throw Rcpp::exception(error.message().c_str(), false);
  }

  if (quotes.size() == 0) {
    return std::make_tuple(
        std::make_shared<std::vector<size_t> >(values), columns, mmap);
  }

  auto out = std::make_shared<std::vector<size_t> >();
  out->reserve(values.size());

  bool in_quote = false;

  auto i = values.cbegin();
  auto q = quotes.cbegin();

  while (q != quotes.cend()) {
    while (i != values.cend() && *i <= *q) {
      if (!in_quote) {
        out->emplace_back(*i);
      }
      ++i;
    }
    ++q;
    in_quote = !in_quote;
  }

  while (i != values.cend()) {
    out->emplace_back(*i);
    ++i;
  }

  return std::make_tuple(out, columns, mmap);
}
