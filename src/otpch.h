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

#include <pugixml.hpp>

#include <string>
#include <algorithm>
#include <chrono>
#include <bitset>
#include <queue>
#include <set>
#include <vector>
#include <list>
#include <map>
#include <limits>

// from src
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <cmath>
#include <memory>
#include <unordered_set>
// from src - end

//boost
#include <boost/config.hpp>
#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>
#include <boost/utility.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

// from src
#include <boost/any.hpp>
#include <boost/filesystem.hpp>
#include <boost/version.hpp>
// from src - end

#include <cstddef>
#include <cstdlib>

#include <ctime>
#include <cassert>

#if BOOST_VERSION < 104400
	#define BOOST_DIR_ITER_FILENAME(iterator) (iterator)->path().filename()
#else
	#define BOOST_DIR_ITER_FILENAME(iterator) (iterator)->path().filename().string()
#endif