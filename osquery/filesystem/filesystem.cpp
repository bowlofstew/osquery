// Copyright 2004-present Facebook. All Rights Reserved.

#include <exception>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "osquery/filesystem.h"

using osquery::Status;

namespace pt = boost::property_tree;

namespace osquery {

Status writeTextFile(const std::string& path, const std::string& content,
                     int permissions, bool force_permissions) {
  // Open the file with the request permissions.
  int output_fd = open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY,
                       permissions);
  if (output_fd <= 0) {
    return Status(1, "Could not create file");
  }

  // If the file existed with different permissions before our open
  // they must be restricted.
  if (chmod(path.c_str(), permissions) != 0) {
    // Could not change the file to the requested permissions.
    return Status(1, "Failed to change permissions");
  }

  auto bytes = write(output_fd, content.c_str(), content.size());
  if (bytes != content.size()) {
    close(output_fd);
    return Status(1, "Failed to write contents");
  }

  close(output_fd);
  return Status(0, "OK");
}

Status readFile(const std::string& path, std::string& content) {
  auto path_exists = pathExists(path);
  if (!path_exists.ok()) {
    return path_exists;
  }

  int statusCode = 0;
  std::string statusMessage = "OK";
  char* buffer;

  std::ifstream file_h(path);
  if (file_h) {
    file_h.seekg(0, file_h.end);
    int len = file_h.tellg();
    file_h.seekg(0, file_h.beg);
    buffer = new char[len];
    file_h.read(buffer, len);
    if (!file_h) {
      statusCode = 1;
      statusMessage = "Could not read file";
      goto cleanup_buffer;
    }
    content.assign(buffer, len);
  } else {
    statusCode = 1;
    statusMessage = "Could not open file for reading";
    goto cleanup;
  }

cleanup_buffer:
  delete[] buffer;
cleanup:
  if (file_h) {
    file_h.close();
  }
  return Status(statusCode, statusMessage);
}

Status isWritable(const std::string& path) {
  auto path_exists = pathExists(path);
  if (!path_exists.ok()) {
    return path_exists;
  }

  if (access(path.c_str(), W_OK) == 0) {
    return Status(0, "OK");
  }
  return Status(1, "Path is not writable.");
}

Status isReadable(const std::string& path) {
  auto path_exists = pathExists(path);
  if (!path_exists.ok()) {
    return path_exists;
  }

  if (access(path.c_str(), R_OK) == 0) {
    return Status(0, "OK");
  }
  return Status(1, "Path is not readable.");
}

Status pathExists(const std::string& path) {
  if (path.length() == 0) {
    return Status(1, "-1");
  }

  // A tri-state determination of presence
  if (!boost::filesystem::exists(path)) {
    return Status(1, "0");
  }
  return Status(0, "1");
}

Status listFilesInDirectory(const std::string& path,
                            std::vector<std::string>& results) {
  try {
    if (!boost::filesystem::exists(path)) {
      return Status(1, "Directory not found");
    }

    if (!boost::filesystem::is_directory(path)) {
      return Status(1, "Supplied path is not a directory");
    }

    boost::filesystem::directory_iterator begin_iter(path);
    boost::filesystem::directory_iterator end_iter;
    for (; begin_iter != end_iter; begin_iter++) {
      results.push_back(begin_iter->path().string());
    }

    return Status(0, "OK");
  } catch (const boost::filesystem::filesystem_error& e) {
    return Status(1, e.what());
  }
}

Status getDirectory(const std::string& path, std::string& dirpath) {
  if (!isDirectory(path).ok()) {
    dirpath = boost::filesystem::path(path).parent_path().string();
    return Status(0, "OK");
  }
  dirpath = path;
  return Status(1, "Path is a directory");
}

Status isDirectory(const std::string& path) {
  if (boost::filesystem::is_directory(path)) {
    return Status(0, "OK");
  }
  return Status(1, "Path is not a directory");
}

Status parseTomcatUserConfigFromDisk(
    const std::string& path,
    std::vector<std::pair<std::string, std::string> >& credentials) {
  std::string content;
  auto s = readFile(path, content);
  if (s.ok()) {
    return parseTomcatUserConfig(content, credentials);
  } else {
    return s;
  }
}

Status parseTomcatUserConfig(
    const std::string& content,
    std::vector<std::pair<std::string, std::string> >& credentials) {
  std::stringstream ss;
  ss << content;
  pt::ptree tree;
  try {
    pt::xml_parser::read_xml(ss, tree);
  } catch (const pt::xml_parser_error& e) {
    return Status(1, e.what());
  }
  try {
    for (const auto& i : tree.get_child("tomcat-users")) {
      if (i.first == "user") {
        try {
          std::pair<std::string, std::string> user;
          user.first = i.second.get<std::string>("<xmlattr>.username");
          user.second = i.second.get<std::string>("<xmlattr>.password");
          credentials.push_back(user);
        } catch (const std::exception& e) {
          LOG(ERROR)
              << "An error occured parsing the tomcat users xml: " << e.what();
          return Status(1, e.what());
        }
      }
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "An error occured while trying to access the tomcat-users"
               << " key in the XML content: " << e.what();
    return Status(1, e.what());
  }
  return Status(0, "OK");
}
}
