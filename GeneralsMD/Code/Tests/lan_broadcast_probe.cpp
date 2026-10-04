/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
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

/*
 * Port defect 29 across machines (W1): the LAN lobby's sockets, as LANAPI builds them off Windows, on two
 * hosts of a real network.  The real Transport and UDP classes:
 *
 *   lan_broadcast_probe listen <this host's LAN address> <port> <seconds>
 *       holds a lobby socket on <address>:<port> (as LANAPI's) and the broadcast listener (UDP::BindForBroadcasts:
 *       the wildcard address, or 255.255.255.255 on Linux), and prints one line per message heard, saying
 *       which socket heard it:
 *       HEARD socket=unicast|listener from=<a.b.c.d> text=<text>
 *   lan_broadcast_probe send <this host's LAN address> <port> <text> [<to address>]
 *       sends <text> from a lobby socket on <address> to 255.255.255.255 (as LANAPI announces a game), or
 *       to <to address> (as a join is sent), and prints SENT.
 *
 * A listener on one host and a sender on the other then show a broadcast crossing the network into the
 * other lobby's listener (and not into its unicast socket), and a directed message into its unicast
 * socket only.  What it cannot see: the lobby's GUI and its message handling above the sockets
 * (test_lan_broadcast and LANAPI's own loop cover the inbox), and a Windows host.
 */

#include "Common/GlobalData.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/Transport.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)

namespace {

UnsignedInt hostOrder( const char *dotted )
{
	struct in_addr a;
	if (inet_pton( AF_INET, dotted, &a ) != 1)
		return 0;
	return ntohl( a.s_addr );
}

double now( void )
{
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return t.tv_sec + t.tv_nsec * 1e-9;
}

void report( Transport &t, const char *which )
{
	for (Int i = 0; i < MAX_MESSAGES; ++i)
	{
		TransportMessage &m = t.m_inBuffer[i];
		if (m.length <= 0)
			continue;
		struct in_addr a;
		a.s_addr = htonl( m.addr );
		char text[ 128 ];
		const Int n = m.length < (Int)sizeof( text ) - 1 ? m.length : (Int)sizeof( text ) - 1;
		memcpy( text, m.data, n );
		text[n] = 0;
		printf( "HEARD socket=%s from=%s text=%s\n", which, inet_ntoa( a ), text );
		fflush( stdout );
		m.length = 0;
	}
}

}	// namespace

int main( int argc, char **argv )
{
	if (argc < 5 || (strcmp( argv[1], "listen" ) != 0 && strcmp( argv[1], "send" ) != 0))
	{
		fprintf( stderr, "usage: lan_broadcast_probe listen <address> <port> <seconds>\n"
			"       lan_broadcast_probe send <address> <port> <text> [<to address>]\n" );
		return 2;
	}
	TheWritableGlobalData = NEW GlobalData;		// Transport::init reads the link simulation's settings
	const UnsignedInt local = hostOrder( argv[2] );
	const UnsignedShort port = (UnsignedShort)atoi( argv[3] );
	if (local == 0 || port == 0)
	{
		fprintf( stderr, "lan_broadcast_probe: bad address or port\n" );
		return 2;
	}

	Transport lobby;
	lobby.shareAddress( TRUE );
	if (!lobby.init( local, port ))
	{
		printf( "FAILED cannot bind the lobby socket %s:%u\n", argv[2], (unsigned)port );
		return 1;
	}

	if (strcmp( argv[1], "send" ) == 0)
	{
		const UnsignedInt to = argc > 5 ? hostOrder( argv[5] ) : (UnsignedInt)INADDR_BROADCAST;
		lobby.allowBroadcasts( TRUE );
		lobby.queueSend( to, port, (const UnsignedByte *)argv[4], (Int)strlen( argv[4] ) + 1 );
		lobby.update();
		printf( "SENT from=%s to=%s text=%s\n", argv[2], argc > 5 ? argv[5] : "255.255.255.255", argv[4] );
		return 0;
	}

	Transport listener;
	if (!listener.initBroadcastListener( port ))
	{
		printf( "FAILED cannot bind the broadcast listener on port %u\n", (unsigned)port );
		return 1;
	}
	printf( "LISTENING unicast=%s:%u listener=broadcasts:%u\n", argv[2], (unsigned)port, (unsigned)port );
	fflush( stdout );
	const double end = now() + atof( argv[4] );
	struct timespec pause = { 0, 20 * 1000 * 1000 };
	while (now() < end)
	{
		lobby.update();
		listener.update();
		report( lobby, "unicast" );
		report( listener, "listener" );
		nanosleep( &pause, NULL );
	}
	printf( "DONE\n" );
	return 0;
}

#else
int main( void ) { return 0; }
#endif
