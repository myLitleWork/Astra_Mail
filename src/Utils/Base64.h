#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

#include <sys/stat.h>
#include <cerrno>




std::vector<unsigned char> decodeBase64(const std::string& in);

