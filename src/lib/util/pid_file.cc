// Copyright (C) 2015 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <util/pid_file.h>
#include <cstdio>
#include <signal.h>
#include <unistd.h>
#include <cerrno>

namespace isc {
namespace util {

PIDFile::PIDFile(const std::string& filename)
    : filename_(filename) {
}

PIDFile::~PIDFile() {
}

int
PIDFile::check() const {
    std::ifstream fs(filename_.c_str());
    int pid;
    bool good;

    // If we weren't able to open the file treat
    // it as if the process wasn't running
    if (!fs.is_open()) {
        return (false);
    }

    // Try to get the pid, get the status and get rid of the file
    fs >> pid;
    good = fs.good();
    fs.close();

    // If we weren't able to read a pid send back an exception
    if (!good) {
        isc_throw(PIDCantReadPID, "Unable to read PID from file '"
                  << filename_ << "'");
    }

    // If the process is still running return its pid.
    if (kill(pid, 0) == 0) {
        return (pid);
    }

    // No process
    return (0);
}

void
PIDFile::write() const {
    write(getpid());
}

void
PIDFile::write(int pid) const {
  std::ofstream fs(filename_.c_str(), std::ofstream::trunc);

    if (!fs.is_open()) {
        isc_throw(PIDFileError, "Unable to open PID file '"
                  << filename_ << "' for write");
    }

    // File is open, write the pid.
    fs << pid << std::endl;

    bool good = fs.good();
    fs.close();

    if (!good) {
        isc_throw(PIDFileError, "Unable to write to PID file '"
                  << filename_ << "'");
    }
}

void
PIDFile::deleteFile() const {
    if ((remove(filename_.c_str()) != 0) &&
        (errno != ENOENT)) {
        isc_throw(PIDFileError, "Unable to delete PID file '"
                  << filename_ << "'");
    }
}

} // namespace isc::util
} // namespace isc
