////////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
////////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////////////////////////////////////////////////////////////////////////

// Definitions should be global.
#include "definitions.h"

#include <string>
#include <algorithm>
#include <random>
#include <chrono>
#include <bitset>
#include <queue>
#include <set>
#include <vector>
#include <list>
#include <map>
#include <limits>

#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cmath>
#include <memory>
#include <unordered_set>

#include <ctime>
#include <cassert>

#include <cstdint>
#include <regex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>

#if __GNUC__ < 8 && !defined( _MSC_VER) && !defined(__APPLE__)
    #include <experimental/filesystem>
    namespace fs = std::experimental::filesystem;
#else
    #include <filesystem>
    namespace fs = std::filesystem;
#endif

//	lib
#include <boost/tokenizer.hpp>
#include <boost/asio.hpp>

#include <boost/any.hpp>

#include <pugixml.hpp>

//	def
#define OTSYS_TIME() std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()