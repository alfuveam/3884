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

#include "otpch.h"

#include "rsa.h"

#include <cryptopp/base64.h>
#include <cryptopp/osrng.h>

static CryptoPP::AutoSeededRandomPool prng;

void RSA::decrypt(char* msg) const
{
	CryptoPP::Integer m{reinterpret_cast<uint8_t*>(msg), 128};
	auto c = pk.CalculateInverse(prng, m);
	c.Encode(reinterpret_cast<uint8_t*>(msg), 128);
}

static const std::string header = "-----BEGIN RSA PRIVATE KEY-----";
static const std::string footer = "-----END RSA PRIVATE KEY-----";

void RSA::loadPEM(const std::string& filename)
{
	std::ifstream file{filename};

	if (!file.is_open()) {
		throw std::runtime_error("Missing file " + filename + ".");
 	}

	std::ostringstream oss;
	for (std::string line; std::getline(file, line); oss << line);
	std::string key = oss.str();

	if (key.substr(0, header.size()) != header) {
		throw std::runtime_error("Missing RSA private key header.");
	}

	if (key.substr(key.size() - footer.size(), footer.size()) != footer) {
		throw std::runtime_error("Missing RSA private key footer.");
	}

	key = key.substr(header.size(), key.size() - footer.size());

	CryptoPP::ByteQueue queue;
	CryptoPP::Base64Decoder decoder;
	decoder.Attach(new CryptoPP::Redirector(queue));
	decoder.Put(reinterpret_cast<const uint8_t*>(key.c_str()), key.size());
	decoder.MessageEnd();

	try {
		pk.BERDecodePrivateKey(queue, false, queue.MaxRetrievable());

		if (!pk.Validate(prng, 3)) {
			throw std::runtime_error("RSA private key is not valid.");
		}
	} catch (const CryptoPP::Exception& e) {
		std::cout << e.what() << '\n';
	}
}

void RSA::getPublicKey(char* buffer)
{
	//	default public key
	buffer = (char*)"9B646903B45B07AC956568D87353BD7165139DD7940703B03E6DD079399661B4A837AA60561D7CCB9452FA0080594909882AB5BCA58A1A1B35F8B1059B72B1212611C6152AD3DBB3CFBEE7ADC142A75D3D75971509C321C5C24A5BD51FD460F01B4E15BEB0DE1930528A5D3F15C1E3CBF5C401D6777E10ACAAB33DBE8D5B7FF5";
}