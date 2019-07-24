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

//libxml
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/threads.h>

//boost
#include <boost/config.hpp>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>

#include <string>
#include <algorithm>
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

#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/err.h>
// from src - end

#include <boost/utility.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

// from src
#include <boost/enable_shared_from_this.hpp>
#include <boost/any.hpp>
#include <boost/filesystem.hpp>
#include <boost/version.hpp>
#include <boost/pool/pool.hpp>
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


#if OPENSSL_VERSION_NUMBER < 0x10100000L

// Provide compatibility across OpenSSL 1.02 and 1.1.
static int old_RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d) {
  /* If the fields n and e in r are NULL, the corresponding input
   * parameters MUST be non-NULL for n and e.  d may be
   * left NULL (in case only the public key is used).
   */
  if ((r->n == NULL && n == NULL) || (r->e == NULL && e == NULL)) {
    return 0;
  }

  if (n != NULL) {
    BN_free(r->n);
    r->n = n;
  }
  if (e != NULL) {
    BN_free(r->e);
    r->e = e;
  }
  if (d != NULL) {
    BN_free(r->d);
    r->d = d;
  }

  return 1;
}

 static int old_RSA_set0_factors(RSA *r, BIGNUM *p, BIGNUM *q) {
    /* If the fields p and q in r are NULL, the corresponding input
     * parameters MUST be non-NULL.
     */
    if ((r->p == NULL && p == NULL)
        || (r->q == NULL && q == NULL))
        return 0;

    if (p != NULL) {
        BN_free(r->p);
        r->p = p;
    }
    if (q != NULL) {
        BN_free(r->q);
        r->q = q;
    }

    return 1;
 }

#endif  // OPENSSL_VERSION_NUMBER < 0x10100000L