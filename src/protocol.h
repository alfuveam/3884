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

#ifndef __PROTOCOL__
#define __PROTOCOL__

#include <zlib.h>

#include "tools.h"
#include "rsa.h"

class OutputMessage;
typedef std::shared_ptr<OutputMessage> OutputMessage_ptr;

class Connection;
typedef std::shared_ptr<Connection> Connection_ptr;

class NetworkMessage;

class Protocol
{
	public:
		explicit Protocol(Connection_ptr connection);
		virtual ~Protocol();

		// non-copyable
		Protocol(const Protocol&) = delete;
		Protocol& operator=(const Protocol&) = delete;
			

		virtual void onConnect() {}
		virtual void onRecvFirstMessage(NetworkMessage& msg) = 0;

		void onRecvMessage(NetworkMessage& msg);
		void onSendMessage(OutputMessage_ptr msg);

		virtual void parsePacket(NetworkMessage&) {}
		uint32_t getIP() const;

		Connection_ptr getConnection() {return m_connection;}
		const Connection_ptr getConnection() const {return m_connection;}
		void setConnection(Connection_ptr connection) {m_connection = connection;}

		int32_t addRef() {return ++m_refCount;}
		int32_t unRef() {return --m_refCount;}

	protected:
		//use this function for autosend messages only
		OutputMessage_ptr getOutputBuffer();

		void setRawMessages(bool value) {m_rawMessages = value;}
		void enableChecksum() {m_checksumEnabled = true;}
		void disableChecksum() {m_checksumEnabled = false;}

		void enableXTEAEncryption() {m_encryptionEnabled = true;}
		void disableXTEAEncryption() {m_encryptionEnabled = false;}
		void setXTEAKey(const uint32_t* key) {memcpy(&m_key, key, sizeof(uint32_t) * 4);}

		void enableCompression()
		{
			compression = true;
		}

		void XTEA_encrypt(OutputMessage& msg);
		bool XTEA_decrypt(NetworkMessage& msg);
		bool RSA_decrypt(NetworkMessage& msg);
		bool RSA_decrypt(RSA* rsa, NetworkMessage& msg);
		void compress(OutputMessage& msg) const;

		virtual void releaseProtocol();
		virtual void deleteProtocolTask();

		friend class Connection;

	private:
		OutputMessage_ptr m_outputBuffer;
		Connection_ptr m_connection;
		uint32_t m_refCount;

		bool m_rawMessages, m_encryptionEnabled, m_checksumEnabled;
		uint32_t m_key[4];
		bool compression = false;
		mutable z_stream zstream = {};		
};
#endif
