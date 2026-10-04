/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameNetwork/IPEnumeration.h"

#if !defined(_WIN32)
#include <ifaddrs.h>
#include <net/if.h>
#include <vector>

/* The machine's IPv4 addresses, off Windows.  Windows asks the resolver for its own host name, which
	 answers with the address of every adapter that has one, and 127.0.0.1 when none has; POSIX's
	 resolver may answer with 127.0.1.1 (Debian's /etc/hosts) or not at all, so the interfaces are read
	 instead: every IPv4 address on an interface that is up, loopback only when nothing else is.  They
	 come back in the interfaces' order, network byte order; getAddresses sorts them as it does on
	 Windows. */
static void getInterfaceAddresses( std::vector<in_addr> &addresses )
{
	struct ifaddrs *interfaces = NULL;
	if (getifaddrs(&interfaces) != 0)
	{
		DEBUG_LOG(("Failed call to getifaddrs; errno is %d\n", errno));
		return;
	}

	std::vector<in_addr> loopback;
	for (struct ifaddrs *ifa = interfaces; ifa; ifa = ifa->ifa_next)
	{
		if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !(ifa->ifa_flags & IFF_UP))
			continue;

		const in_addr address = ((const struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
		if (ifa->ifa_flags & IFF_LOOPBACK)
			loopback.push_back(address);
		else
			addresses.push_back(address);
	}
	freeifaddrs(interfaces);

	if (addresses.empty())
		addresses = loopback;
}
#endif

IPEnumeration::IPEnumeration( void )
{
	m_IPlist = NULL;
	m_isWinsockInitialized = false;
}

IPEnumeration::~IPEnumeration( void )
{
#if defined(_WIN32)
	if (m_isWinsockInitialized)
	{
		WSACleanup();
		m_isWinsockInitialized = false;
	}
#endif

	EnumeratedIP *ip = m_IPlist;
	while (ip)
	{
		ip = ip->getNext();
		m_IPlist->deleteInstance();
		m_IPlist = ip;
	}
}

EnumeratedIP * IPEnumeration::getAddresses( void )
{
	if (m_IPlist)
		return m_IPlist;

	// (Windows only: POSIX sockets need no start-up, so m_isWinsockInitialized stays false there.)
#if defined(_WIN32)
	if (!m_isWinsockInitialized)
	{
		WORD verReq = MAKEWORD(2, 2);
		WSADATA wsadata;

		int err = WSAStartup(verReq, &wsadata);
		if (err != 0) {
			return NULL;
		}

		if ((LOBYTE(wsadata.wVersion) != 2) || (HIBYTE(wsadata.wVersion) !=2)) {
			WSACleanup();
			return NULL;
		}
		m_isWinsockInitialized = true;
	}
#endif

	// get the local machine's host name
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)))
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d\n", lastSocketError()));
		return NULL;
	}
	DEBUG_LOG(("Hostname is '%s'\n", hostname));
	
#if defined(_WIN32)
	// get host information from the host name
	HOSTENT* hostEnt = gethostbyname(hostname);
	if (hostEnt == NULL)
	{
		DEBUG_LOG(("Failed call to gethostnyname; WSAGetLastError returned %d\n", WSAGetLastError()));
		return NULL;
	}
#else
	// The interfaces' addresses, in the shape gethostbyname gives them, for the loop below.
	std::vector<in_addr> addresses;
	getInterfaceAddresses(addresses);
	std::vector<char *> addressList;
	for (size_t i = 0; i < addresses.size(); ++i)
		addressList.push_back((char *)&addresses[i]);
	addressList.push_back(NULL);

	struct hostent interfaceHost = {};
	interfaceHost.h_addrtype = AF_INET;
	interfaceHost.h_length = sizeof(in_addr);
	interfaceHost.h_addr_list = addressList.data();
	struct hostent *hostEnt = &interfaceHost;
#endif
	
	// sanity-check the length of the IP adress
	if (hostEnt->h_length != 4)
	{
		DEBUG_LOG(("gethostbyname returns oddly-sized IP addresses!\n"));
		return NULL;
	}
	
	// construct a list of addresses
	int numAddresses = 0;
	char *entry;
	while ( (entry = hostEnt->h_addr_list[numAddresses++]) != 0 )
	{
		EnumeratedIP *newIP = newInstance(EnumeratedIP);

		AsciiString str;
		str.format("%d.%d.%d.%d", (unsigned char)entry[0], (unsigned char)entry[1], (unsigned char)entry[2], (unsigned char)entry[3]);

		UnsignedInt testIP = *((UnsignedInt *)entry);
		UnsignedInt ip = ntohl(testIP);

		/*
		ip = *entry++;
		ip <<= 8;
		ip += *entry++;
		ip <<= 8;
		ip += *entry++;
		ip <<= 8;
		ip += *entry++;
		*/

		newIP->setIPstring(str);
		newIP->setIP(ip);

		DEBUG_LOG(("IP: 0x%8.8X / 0x%8.8X (%s)\n", testIP, ip, str.str()));

		// Add the IP to the list in ascending order
		if (!m_IPlist)
		{
			m_IPlist = newIP;
			newIP->setNext(NULL);
		}
		else
		{
			if (newIP->getIP() < m_IPlist->getIP())
			{
				newIP->setNext(m_IPlist);
				m_IPlist = newIP;
			}
			else
			{
				EnumeratedIP *p = m_IPlist;
				while (p->getNext() && p->getNext()->getIP() < newIP->getIP())
				{
					p = p->getNext();
				}
				newIP->setNext(p->getNext());
				p->setNext(newIP);
			}
		}
	}

	return m_IPlist;
}

AsciiString IPEnumeration::getMachineName( void )
{
	// (Windows only: POSIX sockets need no start-up, so m_isWinsockInitialized stays false there.)
#if defined(_WIN32)
	if (!m_isWinsockInitialized)
	{
		WORD verReq = MAKEWORD(2, 2);
		WSADATA wsadata;

		int err = WSAStartup(verReq, &wsadata);
		if (err != 0) {
			return NULL;
		}

		if ((LOBYTE(wsadata.wVersion) != 2) || (HIBYTE(wsadata.wVersion) !=2)) {
			WSACleanup();
			return NULL;
		}
		m_isWinsockInitialized = true;
	}
#endif

	// get the local machine's host name
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)))
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d\n", lastSocketError()));
		return NULL;
	}

#if !defined(_WIN32)
	// Windows names the machine by its one-label name; POSIX's host name may carry the domain
	// ("name.local" on macOS).
	char *dot = strchr(hostname, '.');
	if (dot)
		*dot = 0;
#endif

	return AsciiString(hostname);
}


