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

#ifndef __RSA__
#define __RSA__

#include <cryptopp/rsa.h>

class RSA
{
	public:
		RSA() = default;

		// non-copyable
		RSA(const RSA&) = delete;
		RSA& operator=(const RSA&) = delete;

		void loadPEM(const std::string& filename);
		void decrypt(char* msg) const;

		void getPublicKey(char* buffer);
	private:
		CryptoPP::RSA::PrivateKey pk;
		CryptoPP::RSA::PublicKey _pk;
};

#endif