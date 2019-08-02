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

#ifndef __SERVER__
#define __SERVER__

class ServiceBase;
typedef std::shared_ptr<ServiceBase> Service_ptr;

class ServicePort;
typedef std::shared_ptr<ServicePort> ServicePort_ptr;

class Connection;
typedef std::shared_ptr<Connection> Connection_ptr;

class Protocol;
class NetworkMessage;

typedef boost::asio::ip::address IPAddress;
typedef std::vector<IPAddress> IPAddressList;

class ServiceBase
{
	public:
		virtual ~ServiceBase() {}
		virtual Protocol* makeProtocol(Connection_ptr connection) const = 0;

		virtual uint8_t getProtocolId() const = 0;
		virtual bool isSingleSocket() const = 0;
		virtual bool hasChecksum() const = 0;
		virtual const char* getProtocolName() const = 0;
};

template <typename ProtocolType>
class Service : public ServiceBase
{
	public:
		Protocol* makeProtocol(Connection_ptr connection) const {return new ProtocolType(connection);}

		uint8_t getProtocolId() const {return ProtocolType::protocolId;}
		bool isSingleSocket() const {return ProtocolType::isSingleSocket;}
		bool hasChecksum() const {return ProtocolType::hasChecksum;}
		const char* getProtocolName() const {return ProtocolType::protocolName();}
};

typedef std::shared_ptr<boost::asio::ip::tcp::acceptor> Acceptor_ptr;

class ServicePort : public std::enable_shared_from_this<ServicePort>
{
	public:
		// non-copyable
		ServicePort(const ServicePort&) = delete;
		ServicePort& operator=(const ServicePort&) = delete;

		ServicePort(boost::asio::io_service& io_service): m_io_service(io_service),
			m_serverPort(0), m_pendingStart(false) {}
		virtual ~ServicePort() {close();}

		static void services(std::weak_ptr<ServicePort> weakService, IPAddressList ips, uint16_t port);
		static void service(std::weak_ptr<ServicePort> weakService, IPAddress ip, uint16_t port);

		bool add(Service_ptr);
		void open(IPAddressList ips, uint16_t port);
		void close();

		void handle(Acceptor_ptr acceptor, boost::asio::ip::tcp::socket* socket, const boost::system::error_code& error);

		bool isSingleSocket() const {return m_services.size() && m_services.front()->isSingleSocket();}
		std::string getProtocolNames() const;

		Protocol* makeProtocol(bool checksum, NetworkMessage& msg) const;

	protected:
		void accept(Acceptor_ptr acceptor);

		typedef std::vector<Service_ptr> ServiceVec;
		ServiceVec m_services;

		typedef std::vector<Acceptor_ptr> AcceptorVec;
		AcceptorVec m_acceptors;

		boost::asio::io_service& m_io_service;
		uint16_t m_serverPort;
		bool m_pendingStart;

		static bool m_logError;
};

class ServiceManager
{
	public:		
		// non-copyable
		ServiceManager(const ServiceManager&) = delete;
		ServiceManager& operator=(const ServiceManager&) = delete;
					
		ServiceManager(): m_io_service(), deathTimer(m_io_service), running(false) {}
		virtual ~ServiceManager() {stop();}

		template <typename ProtocolType>
		bool add(uint16_t port, IPAddressList ips);

		void run();
		void stop();

		bool isRunning() const {return !m_acceptors.empty();}
		std::list<uint16_t> getPorts() const;

	protected:
		void die() {m_io_service.stop();}

		boost::asio::io_service m_io_service;
		boost::asio::deadline_timer deathTimer;
		bool running;

		typedef std::map<uint16_t, ServicePort_ptr> AcceptorsMap;
		AcceptorsMap m_acceptors;
};

template <typename ProtocolType>
bool ServiceManager::add(uint16_t port, IPAddressList ips)
{
	if(!port)
	{
		std::clog << "> ERROR: No port provided for service " << ProtocolType::protocolName() << ", service disabled." << std::endl;
		return false;
	}

	ServicePort_ptr servicePort;
	AcceptorsMap::iterator it = m_acceptors.find(port);
	if(it == m_acceptors.end())
	{
		servicePort.reset(new ServicePort(m_io_service));
		servicePort->open(ips, port);
		m_acceptors[port] = servicePort;
	}
	else
	{
		servicePort = it->second;
		if(servicePort->isSingleSocket() || ProtocolType::isSingleSocket)
		{
			std::clog << "> ERROR: " << ProtocolType::protocolName() << " and " << servicePort->getProtocolNames()
				<< " cannot use the same port (" << port << ")." << std::endl;
			return false;
		}
	}

	return servicePort->add(Service_ptr(new Service<ProtocolType>()));
}
#endif
