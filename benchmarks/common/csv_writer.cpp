/// @file csv_writer.cpp

#include "csv_writer.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace hs {
namespace bench {

CsvWriter::CsvWriter(const std::string& path) {
    file_.open(path, std::ios::app);
    if (!file_.is_open())
        std::cerr << "[csv_writer] WARNING: cannot open " << path << "\n";
}

CsvWriter::~CsvWriter() {
    if (file_.is_open()) file_.close();
}

void CsvWriter::write_header(const std::vector<std::string>& columns) {
    if (!file_.is_open()) return;
    n_cols_ = columns.size();
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) file_ << ',';
        file_ << columns[i];
    }
    file_ << '\n';
    header_written_ = true;
}

void CsvWriter::write_row(const std::vector<std::string>& values) {
    if (!file_.is_open()) return;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) file_ << ',';
        // Quote values containing commas or newlines
        const auto& v = values[i];
        if (v.find(',') != std::string::npos || v.find('\n') != std::string::npos) {
            file_ << '"' << v << '"';
        } else {
            file_ << v;
        }
    }
    file_ << '\n';
}

void CsvWriter::flush() {
    if (file_.is_open()) file_.flush();
}

} // namespace bench
} // namespace hs
