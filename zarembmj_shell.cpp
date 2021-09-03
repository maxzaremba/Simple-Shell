/**
 * Copyright (C) 2020 zarembmj@miamioh.edu
 *
 * This program uses the features of the ChildProcess class to create
 * a custom Linux shell that can run a command entered by the user. In
 * addition, this shell provides the following additional features:
 *
 *    1. It provides a 'SERIAL' command where commands in a given
 *    shell script (text file or URL) are run one after another
 *    (serially).
 *
 *    2. It provides a 'PARALLEL' command where commands in a given
 *    shell script (text file or URL) are run in parallel.
 */

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>

// Convenience namespace for accessing boost I/O streams.
using namespace boost::asio;
using namespace boost::asio::ip;

#include "ChildProcess.h"

// Shortcut to refer to a list of child processes. The ChildProcess
// class is defined in the ChildProcess.h header file.
using ProcessList = std::vector<ChildProcess>;

// Forward declaration of method that is defined further below.
void processScript(const std::string& fileOrUrl, bool parallel);

/**
 * Helper method to split words in a given string into an array of
 * strings.  The array of strings is typically used to provide
 * arguments to the execvp system call.
 *
 * @note This method uses std::quoted to handle words with quotations
 * around them correctly (to perserve multi-word arguments as a single
 * word)
 *
 * @param line The line containing words to be split.
 *
 * @return A vector-of-strings containing the words in the given line.
 */
StrVec split(const std::string& line) {
    StrVec argList; 
    std::string word;
    std::istringstream is(line);

    while (is >> std::quoted(word)) {
        argList.push_back(word);
    }
    return argList;
}

/**
 * Helper method to fork and execute a given command (and its
 * arugments).  This method uses the ChildProcess class to run the
 * command.
 *
 * @note This method assumes that the first entry in the argList is
 * the command to be executed.
 *
 * @param argList The command and its arguments to be executed by this
 * method.
 *
 * @return This method returns an instance of the ChildProcess class
 * that was used to run the command.
 */
ChildProcess runCommand(const StrVec& argList) {
    std::cout << "Running:";
    for (auto& arg : argList) {
        std::cout << " " << arg;  // Print command/argument
    }

    std::cout << std::endl;

    ChildProcess cp;
    cp.forkNexec(argList);
    return cp; 
}

/**
 * The primary method in this program that processes user-inputs (from
 * console or from a data file) and runs the user-specified commands.
 *
 * @param is The input stream from where the commands are to be read.
 * This stream can either be std::cin or a std::ifstream (a text file
 * containing the commands).
 *
 * @param prompt The string to be displayed to the user. If a prompt
 * should not be displayed, then pass an empty string ("") as the
 * argument.
 *
 * @param parallel If this flag is true, then this method assumes that
 * the commands (read from is) are to be run in parallel. If not, the
 * commands are run serially (i.e., one after another).
 */
void process(std::istream& is, const std::string& prompt = "> ", const bool parallel = false) {
    // The following childList is used to hold child processes when
    // running in parallel.  It is not used in the serial case.
    ProcessList childList;

    std::string line;  
    while (std::cout << prompt, std::getline(is, line) && (line != "exit")) {
        if (line.empty() || (line[0] == '#')) {
            continue;
        }

        const StrVec argList = split(line);

        if ((argList[0] == "SERIAL") || (argList[0] == "PARALLEL")) {
            processScript(argList[1], argList[0] == "PARALLEL");

        } else {
            // Must be a general command to run. Use helper method
            // to create a child process and run the command.
            ChildProcess cp = runCommand(argList);
            if (!parallel) {
                std::cout << "Exit code: " << cp.wait() << std::endl;

            } else {
                childList.push_back(cp);
            }
        }
    }

    for (auto& proc : childList) {
        std::cout << "Exit code: " << proc.wait() << std::endl;
    }
}

/**
 * The main method just calls the process method to
 * run commands typed by the user at the console.
 */
int main() {
    process(std::cin);
}

//--------------------------------------------------------------------
//  Code below is part of additional features for serial & parallel
//  processing of commands from a file or a URL
//--------------------------------------------------------------------

/**
 * Helper method to break down a URL into hostname, port and path. For
 * example, given the url: "https://localhost:8080/~raodm/one.txt"
 * this method returns <"localhost", "8080", "/~raodm/one.txt">
 *
 * Similarly, given the url: "ftp://ftp.files.miamioh.edu/index.html"
 * this method returns <"ftp.files.miamioh.edu", "80", "/index.html">
 *
 * @param url A string containing a valid URL. The port number in URL
 * is always optional.  The default port number is assumed to be 80.
 *
 * @return This method returns a std::tuple with 3 strings. The 3
 * strings are in the order: hostname, port, and path.  Here we use
 * std::tuple because a method can return only 1 value.  The
 * std::tuple is a convenient class to encapsulate multiple return
 * values into a single return value.
 */
std::tuple<std::string, std::string, std::string>
breakDownURL(const std::string& url) {
    std::string hostName, port = "80", path = "/";

    const size_t hostStart = url.find("//") + 2;
    const size_t pathStart = url.find('/', hostStart);
    const size_t portPos   = url.find(':', hostStart);
    const size_t hostEnd   = (portPos == std::string::npos ? pathStart :
                              portPos);

    hostName = url.substr(hostStart, hostEnd - hostStart);
    path     = url.substr(pathStart);
    if (portPos != std::string::npos) {
        port = url.substr(portPos + 1, pathStart - portPos - 1);
    }

    return {hostName, port, path};
}

/**
 * Convenience/helper method to setup an HTTP stream to download the
 * script to be run from a given URL.  This method just requests the
 * data (via a HTTP request) and then reads the HTTP headers.  Rest of
 * the processing is done by the process method.
 *
 * @param url The URL to the script file to be processed. 
 */
void setupHTTPStream(const std::string& url, tcp::iostream& client) {
    std::string hostname, port, path;
    std::tie(hostname, port, path) = breakDownURL(url);

    client.connect(hostname, port);
    client << "GET "   << path     << " HTTP/1.1\r\n"
           << "Host: " << hostname << "\r\n"
           << "Connection: Close\r\n\r\n";

    for (std::string hdr; std::getline(client, hdr) &&
             !hdr.empty() && hdr != "\r";) {}
}

/**
 * Helper method to handle running a script from a given file or a
 * URL.  This method is called from the process() method below and
 * recursively calls the process() method.  This method just sets up
 * the input stream to be processed below.
 *
 * @param fileOrUrl The file or URL to be processed.  This parameter
 * is in the form "simple.sh" or
 * "http://www.users.miamioh.edu/raodm/simple.sh"
 *
 * @param parallel If this parameter is true then this method
 * processes each command in the script in parallel.
 */
void processScript(const std::string& fileOrUrl, bool parallel) {
    // Print the source of the commands
    // std::cout << "Processing commands from file " << std::quoted(fileOrUrl)
    //           << std::endl;
    if (fileOrUrl.find("http://") == 0) {
        tcp::iostream client;
        setupHTTPStream(fileOrUrl, client);
        process(client, "", parallel);
        
    } else {
        std::ifstream script(fileOrUrl);
        process(script, "", parallel);
    }
}