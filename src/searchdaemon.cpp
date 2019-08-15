//
// Copyright (c) 2017-2019, Manticore Software LTD (http://manticoresearch.com)
// Copyright (c) 2001-2016, Andrew Aksyonoff
// Copyright (c) 2008-2016, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

/// @file searchdaemon.cpp
/// Definitions for the stuff need by searchd to work and serve the indexes.

#include "sphinxstd.h"
#include "searchdaemon.h"

#if USE_WINDOWS
	#define USE_PSI_INTERFACE 1
	// for MAC address
	#include <iphlpapi.h>
	#pragma comment(lib, "IPHLPAPI.lib")
#else
	#include <netdb.h>
	// for MAC address
	#include <net/if.h>
	#include <sys/ioctl.h>
	#include <net/ethernet.h>
#endif

// for FreeBSD
#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#endif

#include <cmath>

static auto& g_bGotSigterm = sphGetGotSigterm();    // we just received SIGTERM; need to shutdown

/////////////////////////////////////////////////////////////////////////////
// MISC GLOBALS
/////////////////////////////////////////////////////////////////////////////

// 'like' matcher
CheckLike::CheckLike( const char* sPattern )
{
	if ( !sPattern )
		return;

	m_sPattern.Reserve( 2 * strlen( sPattern ));
	char* d = const_cast<char*> ( m_sPattern.cstr());

	// remap from SQL LIKE syntax to Sphinx wildcards syntax
	// '_' maps to '?', match any single char
	// '%' maps to '*', match zero or mor chars
	for ( const char* s = sPattern; *s; s++ )
	{
		switch ( *s )
		{
			case '_': *d++ = '?';
				break;
			case '%': *d++ = '*';
				break;
			case '?': *d++ = '\\';
				*d++ = '?';
				break;
			case '*': *d++ = '\\';
				*d++ = '*';
				break;
			default: *d++ = *s;
				break;
		}
	}
	*d = '\0';
}

bool CheckLike::Match( const char* sValue )
{
	return sValue && ( m_sPattern.IsEmpty() || sphWildcardMatch( sValue, m_sPattern.cstr()));
}

// string vector with 'like' matcher
/////////////////////////////////////////////////////////////////////////////
VectorLike::VectorLike()
	: CheckLike( nullptr )
{}

VectorLike::VectorLike( const CSphString& sPattern )
	: CheckLike( sPattern.cstr()), m_sColKey( "Variable_name" ), m_sColValue( "Value" )
{}

const char* VectorLike::szColKey() const
{
	return m_sColKey.cstr();
}

const char* VectorLike::szColValue() const
{
	return m_sColValue.cstr();
}

bool VectorLike::MatchAdd( const char* sValue )
{
	if ( Match( sValue ))
	{
		Add( sValue );
		return true;
	}
	return false;
}

bool VectorLike::MatchAddVa( const char* sTemplate, ... )
{
	va_list ap;
	CSphString sValue;

	va_start ( ap, sTemplate );
	sValue.SetSprintfVa( sTemplate, ap );
	va_end ( ap );

	return MatchAdd( sValue.cstr());
}

const char* g_dIndexTypeName[1 + ( int ) IndexType_e::ERROR_] = {
	"plain",
	"template",
	"rt",
	"percolate",
	"distributed",
	"invalid"
};

CSphString GetTypeName( IndexType_e eType )
{
	return g_dIndexTypeName[( int ) eType];
}

IndexType_e TypeOfIndexConfig( const CSphString& sType )
{
	if ( sType=="distributed" )
		return IndexType_e::DISTR;

	if ( sType=="rt" )
		return IndexType_e::RT;

	if ( sType=="percolate" )
		return IndexType_e::PERCOLATE;

	if ( sType=="template" )
		return IndexType_e::TEMPLATE;

	if (( sType.IsEmpty() || sType=="plain" ))
		return IndexType_e::PLAIN;

	return IndexType_e::ERROR_;
}

void CheckPort( int iPort )
{
	if ( !IsPortInRange( iPort ))
		sphFatal( "port %d is out of range", iPort );
}

// check only proto name in lowcase, no '_vip'
static Proto_e SimpleProtoByName ( const CSphString& sProto )
{
	if ( sProto=="sphinx" )
		return Proto_e::SPHINX;
	if ( sProto=="mysql41" )
		return Proto_e::MYSQL41;
	if ( sProto=="http" )
		return Proto_e::HTTP;
	if ( sProto=="replication" )
		return Proto_e::REPLICATION;
	sphFatal( "unknown listen protocol type '%s'", sProto.scstr());
	return Proto_e::SPHINX;
}

static void ProtoByName( CSphString sFullProto, ListenerDesc_t& tDesc )
{
	sFullProto.ToLower();
	StrVec_t dParts;
	sphSplit( dParts, sFullProto.cstr(), "_" );

	if ( !dParts.IsEmpty() )
		tDesc.m_eProto = SimpleProtoByName( dParts[0] );

	if ( dParts.GetLength()==1 )
		return;

	if ( dParts.GetLength()==2 && dParts[1]=="vip" )
	{
		tDesc.m_bVIP = true;
		return;
	}

	sphFatal( "unknown listen protocol type '%s'", sFullProto.scstr() );
}

/// listen = ( address ":" port | port | path | address ":" port start - port end ) [ ":" protocol ] [ "_vip" ]
ListenerDesc_t ParseListener( const char* sSpec )
{
	ListenerDesc_t tRes;
	tRes.m_eProto = Proto_e::SPHINX;
	tRes.m_uIP = htonl(INADDR_ANY);
	tRes.m_iPort = SPHINXAPI_PORT;
	tRes.m_iPortsCount = 0;
	tRes.m_bVIP = false;

	// split by colon
	auto dParts = sphSplit( sSpec, ":" ); // diff. parts are :-separated

	int iParts = dParts.GetLength();
	if ( iParts>3 )
		sphFatal( "invalid listen format (too many fields)" );

	assert ( iParts>=1 && iParts<=3 );

	// handle UNIX socket case
	// might be either name on itself (1 part), or name+protocol (2 parts)
	if ( *dParts[0].scstr()=='/' )
	{
		if ( iParts>2 )
			sphFatal( "invalid listen format (too many fields)" );

		if ( iParts==2 )
			ProtoByName( dParts[1], tRes );

		tRes.m_sUnix = dParts[0];
		return tRes;
	}

	// check if it all starts with a valid port number
	auto sPart = dParts[0].cstr();
	int iLen = strlen( sPart );

	bool bAllDigits = true;
	for ( int i = 0; i<iLen && bAllDigits; ++i )
		if ( !isdigit( sPart[i] ))
			bAllDigits = false;

	int iPort = 0;
	if ( bAllDigits && iLen<=5 ) // if we have num from only digits, it may be only port, nothing else!
	{
		iPort = atol( sPart );
		CheckPort( iPort ); // lets forbid ambiguous magic like 0:sphinx or 99999:mysql41
	}

	// handle TCP port case
	// one part. might be either port name, or host name (unix socked case is already parsed)
	if ( iParts==1 )
	{
		if ( iPort )
			// port name on itself
			tRes.m_iPort = iPort;
		else
			// host name on itself
			tRes.m_uIP = sphGetAddress( sSpec, GETADDR_STRICT );
		return tRes;
	}

	// two or three parts
	if ( iPort )
	{
		// 1st part is a valid port number; must be port:proto
		if ( iParts!=2 )
			sphFatal( "invalid listen format (expected port:proto, got extra trailing part in listen=%s)", sSpec );

		tRes.m_iPort = iPort;
		ProtoByName( dParts[1], tRes );
		return tRes;
	}

	// 1st part must be a host name; must be host:port[:proto]
	if ( iParts==3 )
		ProtoByName( dParts[2], tRes );

	tRes.m_uIP = dParts[0].IsEmpty()
				 ? htonl(INADDR_ANY)
				 : sphGetAddress( dParts[0].cstr(), GETADDR_STRICT );


	auto dPorts = sphSplit( dParts[1].scstr(), "-" );
	tRes.m_iPort = atoi( dPorts[0].cstr());
	CheckPort( tRes.m_iPort );

	if ( dPorts.GetLength()==2 )
	{
		int iPortsEnd = atoi( dPorts[1].scstr() );
		CheckPort( iPortsEnd );
		if ( iPortsEnd<=tRes.m_iPort )
			sphFatal( "ports range invalid %d-%d", iPort, iPortsEnd );
		if (( iPortsEnd - tRes.m_iPort )<2 )
			sphFatal( "ports range %d-%d count should be at least 2, got %d", iPort, iPortsEnd,
					  iPortsEnd - iPort );
		tRes.m_iPortsCount = iPortsEnd - tRes.m_iPort;
	}
	return tRes;
}

/////////////////////////////////////////////////////////////////////////////
// NETWORK SOCKET WRAPPERS
/////////////////////////////////////////////////////////////////////////////

#if USE_WINDOWS
const char * sphSockError ( int iErr )
{
	if ( iErr==0 )
		iErr = WSAGetLastError ();

	static char sBuf [ 256 ];
	_snprintf ( sBuf, sizeof(sBuf), "WSA error %d", iErr );
	return sBuf;
}
#else

const char* sphSockError( int )
{
	return strerrorm(errno);
}

#endif


int sphSockGetErrno()
{
#if USE_WINDOWS
	return WSAGetLastError();
#else
	return errno;
#endif
}


void sphSockSetErrno( int iErr )
{
#if USE_WINDOWS
	WSASetLastError ( iErr );
#else
	errno = iErr;
#endif
}


int sphSockPeekErrno()
{
	int iRes = sphSockGetErrno();
	sphSockSetErrno( iRes );
	return iRes;
}


int sphSetSockNB( int iSock )
{
#if USE_WINDOWS
	u_long uMode = 1;
		return ioctlsocket ( iSock, FIONBIO, &uMode );
#else
	return fcntl( iSock, F_SETFL, O_NONBLOCK );
#endif
}

int RecvNBChunk( int iSock, char*& pBuf, int& iLeftBytes )
{
	// try to receive next chunk
	auto iRes = sphSockRecv ( iSock, pBuf, iLeftBytes );

	if ( iRes>0 )
	{
		pBuf += iRes;
		iLeftBytes -= iRes;
	}
	return ( int ) iRes;
}

/// wait until socket is readable or writable
int sphPoll( int iSock, int64_t tmTimeout, bool bWrite )
{
	// don't need any epoll/kqueue here, since we check only 1 socket
#if HAVE_POLL
	struct pollfd pfd;
	pfd.fd = iSock;
	pfd.events = bWrite ? POLLOUT : POLLIN;

	return ::poll( &pfd, 1, int( tmTimeout / 1000 ));
#else
	fd_set fdSet;
	FD_ZERO ( &fdSet );
	sphFDSet ( iSock, &fdSet );

	struct timeval tv;
	tv.tv_sec = (int)( tmTimeout / 1000000 );
	tv.tv_usec = (int)( tmTimeout % 1000000 );

	return ::select ( iSock+1, bWrite ? NULL : &fdSet, bWrite ? &fdSet : NULL, NULL, &tv );
#endif
}

#if USE_WINDOWS

/// on Windows, the wrapper just prevents the warnings

#pragma warning(push) // store current warning values
#pragma warning(disable:4127) // conditional expr is const
#pragma warning(disable:4389) // signed/unsigned mismatch

void sphFDSet ( int fd, fd_set * fdset )
{
	FD_SET ( fd, fdset );
}

void sphFDClr ( int fd, fd_set * fdset )
{
	FD_SET ( fd, fdset );
}

#pragma warning(pop) // restore warnings

#else // !USE_WINDOWS

#define SPH_FDSET_OVERFLOW( _fd ) ( (_fd)<0 || (_fd)>=(int)FD_SETSIZE )

/// on UNIX, we also check that the descript won't corrupt the stack
void sphFDSet( int fd, fd_set* set )
{
	if ( SPH_FDSET_OVERFLOW( fd ))
		sphFatal( "sphFDSet() failed fd=%d, FD_SETSIZE=%d", fd, FD_SETSIZE );
	else
		FD_SET ( fd, set );
}

void sphFDClr( int fd, fd_set* set )
{
	if ( SPH_FDSET_OVERFLOW( fd ))
		sphFatal( "sphFDClr() failed fd=%d, FD_SETSIZE=%d", fd, FD_SETSIZE );
	else
		FD_CLR ( fd, set );
}

#endif // USE_WINDOWS


DWORD sphGetAddress( const char* sHost, bool bFatal, bool bIP )
{
	struct addrinfo tHints, * pResult = nullptr;
	memset( &tHints, 0, sizeof( tHints ));
	tHints.ai_family = AF_INET;
	tHints.ai_socktype = SOCK_STREAM;
	if ( bIP )
		tHints.ai_flags = AI_NUMERICHOST;

	int iResult = getaddrinfo( sHost, nullptr, &tHints, &pResult );
	auto pOrigResult = pResult;
	if ( iResult!=0 || !pResult )
	{
		if ( bFatal )
			sphFatal( "no AF_INET address found for: %s", sHost );
		else
			sphLogDebugv( "no AF_INET address found for: %s", sHost );
		return 0;
	}

	assert ( pResult );
	auto* pSockaddr_ipv4 = ( struct sockaddr_in* ) pResult->ai_addr;
	DWORD uAddr = pSockaddr_ipv4->sin_addr.s_addr;

	if ( pResult->ai_next )
	{
		StringBuilder_c sBuf( "; ip=", "ip=" );
		for ( ; pResult->ai_next; pResult = pResult->ai_next )
		{
			char sAddrBuf[SPH_ADDRESS_SIZE];
			auto* pAddr = ( struct sockaddr_in* ) pResult->ai_addr;
			DWORD uNextAddr = pAddr->sin_addr.s_addr;
			sphFormatIP( sAddrBuf, sizeof( sAddrBuf ), uNextAddr );
			sBuf << sAddrBuf;
		}

		sphWarning( "multiple addresses found for '%s', using the first one (%s)", sHost, sBuf.cstr());
	}

	freeaddrinfo( pOrigResult );
	return uAddr;
}


/// formats IP address given in network byte order into sBuffer
/// returns the buffer
char* sphFormatIP( char* sBuffer, int iBufferSize, DWORD uAddress )
{
	const BYTE* a = ( const BYTE* ) &uAddress;
	snprintf( sBuffer, iBufferSize, "%u.%u.%u.%u", a[0], a[1], a[2], a[3] );
	return sBuffer;
}


bool IsPortInRange( int iPort )
{
	return ( iPort>0 ) && ( iPort<=0xFFFF );
}

int sphSockRead( int iSock, void* buf, int iLen, int iReadTimeout, bool bIntr )
{
	assert ( iLen>0 );

	int64_t tmMaxTimer = sphMicroTimer() + I64C( 1000000 ) * Max( 1, iReadTimeout ); // in microseconds
	int iLeftBytes = iLen; // bytes to read left

	auto pBuf = ( char* ) buf;
	int iErr = 0;
	int iRes = -1;

	while ( iLeftBytes>0 )
	{
		int64_t tmMicroLeft = tmMaxTimer - sphMicroTimer();
		if ( tmMicroLeft<=0 )
			break; // timed out

#if USE_WINDOWS
		// Windows EINTR emulation
		// Ctrl-C will not interrupt select on Windows, so let's handle that manually
		// forcibly limit select() to 100 ms, and check flag afterwards
		if ( bIntr )
			tmMicroLeft = Min ( tmMicroLeft, 100000 );
#endif

		// wait until there is data
		iRes = sphPoll( iSock, tmMicroLeft );

		// if there was EINTR, retry
		// if any other error, bail
		if ( iRes==-1 )
		{
			// only let SIGTERM (of all them) to interrupt, and only if explicitly allowed
			iErr = sphSockGetErrno();
			if ( iErr==EINTR )
			{
				if ( !( g_bGotSigterm && bIntr ))
					continue;
				sphLogDebug( "sphSockRead: select got SIGTERM, exit -1" );
			}
			return -1;
		}

		// if there was a timeout, report it as an error
		if ( iRes==0 )
		{
#if USE_WINDOWS
			// Windows EINTR emulation
			if ( bIntr )
			{
				// got that SIGTERM
				if ( g_bGotSigterm )
				{
					sphLogDebug ( "sphSockRead: got SIGTERM emulation on Windows, exit -1" );
					sphSockSetErrno ( EINTR );
					return -1;
				}

				// timeout might not be fully over just yet, so re-loop
				continue;
			}
#endif

			sphSockSetErrno( ETIMEDOUT );
			return -1;
		}

		// try to receive next chunk
		iRes = RecvNBChunk( iSock, pBuf, iLeftBytes );

		// if there was eof, we're done
		if ( !iRes )
		{
			sphSockSetErrno( ECONNRESET );
			return -1;
		}

		// if there was EINTR, retry
		// if any other error, bail
		if ( iRes==-1 )
		{
			// only let SIGTERM (of all them) to interrupt, and only if explicitly allowed
			iErr = sphSockGetErrno();
			if ( iErr==EINTR )
			{
				if ( !( g_bGotSigterm && bIntr ))
					continue;
				sphLogDebug( "sphSockRead: select got SIGTERM, exit -1" );
			}
			return -1;
		}

		// avoid partial buffer loss in case of signal during the 2nd (!) read
		bIntr = false;
	}

	// if there was a timeout, report it as an error
	if ( iLeftBytes!=0 )
	{
		sphSockSetErrno( ETIMEDOUT );
		return -1;
	}

	return iLen;
}


int SockReadFast( int iSock, void* buf, int iLen, int iReadTimeout )
{
	auto pBuf = ( char* ) buf;
	int iFullLen = iLen;
	// try to receive available chunk
	int iChunk = RecvNBChunk( iSock, pBuf, iLen );
	if ( !iLen ) // all read in one-shot
	{
		assert ( iChunk==iFullLen );
		return iFullLen;
	}

	auto iRes = sphSockRead( iSock, pBuf, iLen, iReadTimeout, false );
	if ( iRes>=0 )
		iRes += iChunk;
	return iRes;
}


/////////////////////////////////////////////////////////////////////////////
// NETWORK BUFFERS
/////////////////////////////////////////////////////////////////////////////

ISphOutputBuffer::ISphOutputBuffer()
{
	m_dBuf.Reserve( NETOUTBUF );
}

// construct via adopting external buf
ISphOutputBuffer::ISphOutputBuffer( CSphVector<BYTE>& dChunk )
{
	m_dBuf.SwapData( dChunk );
}


void ISphOutputBuffer::SendString( const char* sStr )
{
	int iLen = sStr ? strlen( sStr ) : 0;
	SendInt( iLen );
	SendBytes( sStr, iLen );
}

/////////////////////////////////////////////////////////////////////////////

void CachedOutputBuffer_c::Flush()
{
	CommitAllMeasuredLengths();
	ISphOutputBuffer::Flush();
}

intptr_t CachedOutputBuffer_c::StartMeasureLength()
{
	auto iPos = ( intptr_t ) m_dBuf.GetLength();
	m_dBlobs.Add( iPos );
	SendInt( 0 );
	return iPos;
}

void CachedOutputBuffer_c::CommitMeasuredLength( intptr_t iStoredPos )
{
	if ( m_dBlobs.IsEmpty()) // possible if flush happens before APIheader destroyed.
		return;
	auto iPos = m_dBlobs.Pop();
	assert ( iStoredPos==-1 || iStoredPos==iPos );
	int iBlobLen = m_dBuf.GetLength() - iPos - sizeof( int );
	WriteInt( iPos, iBlobLen );
}

void CachedOutputBuffer_c::CommitAllMeasuredLengths()
{
	while ( !m_dBlobs.IsEmpty())
	{
		auto uPos = m_dBlobs.Pop();
		int iBlobLen = m_dBuf.GetLength() - uPos - sizeof( int );
		WriteInt( uPos, iBlobLen );
	}
}

/// SmartOutputBuffer_t : chain of blobs could be used in scattered sending
/////////////////////////////////////////////////////////////////////////////
SmartOutputBuffer_t::~SmartOutputBuffer_t()
{
	m_dChunks.Apply( []( ISphOutputBuffer*& pChunk ) {
		SafeRelease ( pChunk );
	} );
}

int SmartOutputBuffer_t::GetSentCount() const
{
	int iSize = 0;
	m_dChunks.Apply( [ &iSize ]( ISphOutputBuffer*& pChunk ) {
		iSize += pChunk->GetSentCount();
	} );
	return iSize + m_dBuf.GetLength();
}

void SmartOutputBuffer_t::StartNewChunk()
{
	CommitAllMeasuredLengths();
	assert ( BlobsEmpty());
	m_dChunks.Add( new ISphOutputBuffer( m_dBuf ));
	m_dBuf.Reserve( NETOUTBUF );
}

/*
void SmartOutputBuffer_t::AppendBuf ( SmartOutputBuffer_t &dBuf )
{
	if ( !dBuf.m_dBuf.IsEmpty () )
		dBuf.StartNewChunk ();
	for ( auto * pChunk : dBuf.m_dChunks )
	{
		pChunk->AddRef ();
		m_dChunks.Add ( pChunk );
	}
}

void SmartOutputBuffer_t::PrependBuf ( SmartOutputBuffer_t &dBuf )
{
	CSphVector<ISphOutputBuffer *> dChunks;
	if ( !dBuf.m_dBuf.IsEmpty () )
		dBuf.StartNewChunk ();
	for ( auto * pChunk : dBuf.m_dChunks )
	{
		pChunk->AddRef ();
		dChunks.Add ( pChunk );
	}
	dChunks.Append ( m_dChunks );
	m_dChunks.SwapData ( dChunks );
}
*/

#ifndef UIO_MAXIOV
#define UIO_MAXIOV (1024)
#endif

// makes vector of chunks suitable to direct using in Send() or WSASend()
// returns federated size of the chunks
size_t SmartOutputBuffer_t::GetIOVec( CSphVector<sphIovec>& dOut ) const
{
	size_t iOutSize = 0;
	dOut.Reset();
	m_dChunks.Apply( [ &dOut, &iOutSize ]( const ISphOutputBuffer* pChunk ) {
		auto& dIovec = dOut.Add();
		IOPTR( dIovec ) = IOBUFTYPE ( pChunk->GetBufPtr());
		IOLEN ( dIovec ) = pChunk->GetSentCount();
		iOutSize += IOLEN ( dIovec );
	} );
	if ( !m_dBuf.IsEmpty())
	{
		auto& dIovec = dOut.Add();
		IOPTR ( dIovec ) = IOBUFTYPE ( GetBufPtr());
		IOLEN ( dIovec ) = m_dBuf.GetLengthBytes();
		iOutSize += IOLEN ( dIovec );
	}
	assert ( dOut.GetLength()<UIO_MAXIOV );
	return iOutSize;
};

void SmartOutputBuffer_t::Reset()
{
	m_dChunks.Apply( []( ISphOutputBuffer*& pChunk ) {
		SafeRelease ( pChunk );
	} );
	m_dChunks.Reset();
	m_dBuf.Reset();
	m_dBuf.Reserve( NETOUTBUF );
};

#if USE_WINDOWS
void SmartOutputBuffer_t::LeakTo ( CSphVector<ISphOutputBuffer *> dOut )
{
	for ( auto & pChunk : m_dChunks )
		dOut.Add ( pChunk );
	m_dChunks.Reset ();
	dOut.Add ( new ISphOutputBuffer ( m_dBuf ) );
	m_dBuf.Reserve ( NETOUTBUF );
}
#endif

/////////////////////////////////////////////////////////////////////////////

NetOutputBuffer_c::NetOutputBuffer_c( int iSock )
	: m_iSock( iSock )
{
	assert ( m_iSock>0 );
}

void NetOutputBuffer_c::Flush()
{
	CommitAllMeasuredLengths();

	if ( m_bError )
		return;

	int iLen = m_dBuf.GetLength();
	if ( !iLen )
		return;

	if ( g_bGotSigterm )
		sphLogDebug( "SIGTERM in NetOutputBuffer::Flush" );

	StringBuilder_c sError;
	auto* pBuffer = ( const char* ) m_dBuf.Begin();

	CSphScopedProfile tProf( m_pProfile, SPH_QSTATE_NET_WRITE );

	const int64_t tmMaxTimer = sphMicroTimer() + MS2SEC * g_iWriteTimeout; // in microseconds
	while ( !m_bError )
	{
		auto iRes = sphSockSend ( m_iSock, pBuffer, iLen );
		if ( iRes<0 )
		{
			int iErrno = sphSockGetErrno();
			if ( iErrno==EINTR ) // interrupted before any data was sent; just loop
				continue;
			if ( iErrno!=EAGAIN && iErrno!=EWOULDBLOCK )
			{
				sError.Sprintf( "send() failed: %d: %s", iErrno, sphSockError( iErrno ));
				sphWarning( "%s", sError.cstr());
				m_bError = true;
				break;
			}
		} else
		{
			m_iSent += iRes;
			pBuffer += iRes;
			iLen -= iRes;
			if ( iLen==0 )
				break;
		}

		// wait until we can write
		int64_t tmMicroLeft = tmMaxTimer - sphMicroTimer();
		iRes = 0;
		if ( tmMicroLeft>0 )
			iRes = sphPoll( m_iSock, tmMicroLeft, true );

		if ( !iRes ) // timeout
		{
			sError << "timed out while trying to flush network buffers";
			sphWarning( "%s", sError.cstr());
			m_bError = true;
			break;
		}

		if ( iRes<0 )
		{
			int iErrno = sphSockGetErrno();
			if ( iErrno==EINTR )
				break;
			sError.Sprintf( "sphPoll() failed: %d: %s", iErrno, sphSockError( iErrno ));
			sphWarning( "%s", sError.cstr());
			m_bError = true;
			break;
		}
		assert ( iRes>0 );
	}

	m_dBuf.Resize( 0 );
}


/////////////////////////////////////////////////////////////////////////////

InputBuffer_c::InputBuffer_c( const BYTE* pBuf, int iLen )
	: m_pBuf( pBuf ), m_pCur( pBuf ), m_bError( !pBuf || iLen<0 ), m_iLen( iLen )
{}

InputBuffer_c::InputBuffer_c ( const VecTraits_T<BYTE> & dBuf )
	: m_pBuf ( dBuf.begin() ), m_pCur ( dBuf.begin () ), m_bError ( dBuf.IsEmpty() ), m_iLen ( dBuf.GetLength() ) {}


CSphString InputBuffer_c::GetString()
{
	CSphString sRes;

	int iLen = GetInt();
	if ( m_bError || iLen<0 || iLen>g_iMaxPacketSize || ( m_pCur + iLen>m_pBuf + m_iLen ))
	{
		SetError( true );
		return sRes;
	}

	if ( iLen )
		sRes.SetBinary(( char* ) m_pCur, iLen );

	m_pCur += iLen;
	return sRes;
}


CSphString InputBuffer_c::GetRawString( int iLen )
{
	CSphString sRes;

	if ( m_bError || iLen<0 || iLen>g_iMaxPacketSize || ( m_pCur + iLen>m_pBuf + m_iLen ))
	{
		SetError( true );
		return sRes;
	}

	if ( iLen )
		sRes.SetBinary(( char* ) m_pCur, iLen );

	m_pCur += iLen;
	return sRes;
}


bool InputBuffer_c::GetString( CSphVector<BYTE>& dBuffer )
{
	int iLen = GetInt();
	if ( m_bError || iLen<0 || iLen>g_iMaxPacketSize || ( m_pCur + iLen>m_pBuf + m_iLen ))
	{
		SetError( true );
		return false;
	}

	if ( !iLen )
		return true;

	return GetBytes( dBuffer.AddN( iLen ), iLen );
}


bool InputBuffer_c::GetBytes( void* pBuf, int iLen )
{
	assert ( pBuf );
	assert ( iLen>0 && iLen<=g_iMaxPacketSize );

	if ( m_bError || ( m_pCur + iLen>m_pBuf + m_iLen ))
	{
		SetError( true );
		return false;
	}

	memcpy( pBuf, m_pCur, iLen );
	m_pCur += iLen;
	return true;
}

bool InputBuffer_c::GetBytesZerocopy( const BYTE** ppData, int iLen )
{
	assert ( ppData );
	assert ( iLen>0 && iLen<=g_iMaxPacketSize );

	if ( m_bError || ( m_pCur + iLen>m_pBuf + m_iLen ))
	{
		SetError( true );
		return false;
	}

	*ppData = m_pCur;
	m_pCur += iLen;
	return true;
}


bool InputBuffer_c::GetDwords( CSphVector<DWORD>& dBuffer, int& iGot, int iMax )
{
	iGot = GetInt();
	if ( iGot<0 || iGot>iMax )
	{
		SetError( true );
		return false;
	}

	dBuffer.Resize( iGot );
	ARRAY_FOREACH ( i, dBuffer )
		dBuffer[i] = GetDword();

	if ( m_bError )
		dBuffer.Reset();

	return !m_bError;
}


bool InputBuffer_c::GetQwords( CSphVector<SphAttr_t>& dBuffer, int& iGot, int iMax )
{
	iGot = GetInt();
	if ( iGot<0 || iGot>iMax )
	{
		SetError( true );
		return false;
	}

	dBuffer.Resize( iGot );
	ARRAY_FOREACH ( i, dBuffer )
		dBuffer[i] = GetUint64();

	if ( m_bError )
		dBuffer.Reset();

	return !m_bError;
}


/////////////////////////////////////////////////////////////////////////////

NetInputBuffer_c::NetInputBuffer_c( int iSock )
	: STORE( NET_MINIBUFFER_SIZE ), InputBuffer_c( m_pData, NET_MINIBUFFER_SIZE ), m_iSock( iSock )
{
	Resize( 0 );
}


bool NetInputBuffer_c::ReadFrom( int iLen, int iTimeout, bool bIntr, bool bAppend )
{
	int iTail = bAppend ? m_iLen : 0;

	m_bIntr = false;
	if ( iLen<=0 || iLen>g_iMaxPacketSize || m_iSock<0 )
		return false;

	int iOff = m_pCur - m_pBuf;
	Resize( m_iLen );
	Reserve( iTail + iLen );
	BYTE* pBuf = m_pData + iTail;
	m_pBuf = m_pData;
	m_pCur = bAppend ? m_pData + iOff : m_pData;
	int iGot = sphSockRead( m_iSock, pBuf, iLen, iTimeout, bIntr );
	if ( g_bGotSigterm )
	{
		sphLogDebug( "NetInputBuffer_c::ReadFrom: got SIGTERM, return false" );
		m_bError = true;
		m_bIntr = true;
		return false;
	}

	m_bError = ( iGot!=iLen );
	m_bIntr = m_bError && ( sphSockPeekErrno()==EINTR );
	m_iLen = m_bError ? 0 : iTail + iLen;
	return !m_bError;
}


/////////////////////////////////////////////////////////////////////////////
// SERVED INDEX DESCRIPTORS STUFF
/////////////////////////////////////////////////////////////////////////////


class QueryStatContainer_c: public QueryStatContainer_i
{
public:
	void Add( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp ) final;
	void GetRecord( int iRecord, QueryStatRecord_t& tRecord ) const final;
	int GetNumRecords() const final;

	QueryStatContainer_c();
	QueryStatContainer_c( QueryStatContainer_c&& tOther ) noexcept;
	void Swap( QueryStatContainer_c& rhs ) noexcept;
	QueryStatContainer_c& operator=( QueryStatContainer_c tOther ) noexcept;

private:
	CircularBuffer_T<QueryStatRecord_t> m_dRecords;
};

void QueryStatContainer_c::Add( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp )
{
	if ( !m_dRecords.IsEmpty())
	{
		QueryStatRecord_t& tLast = m_dRecords.Last();
		const uint64_t BUCKET_TIME_DELTA = 100000;
		if ( uTimestamp - tLast.m_uTimestamp<=BUCKET_TIME_DELTA )
		{
			tLast.m_uFoundRowsMin = Min( uFoundRows, tLast.m_uFoundRowsMin );
			tLast.m_uFoundRowsMax = Max( uFoundRows, tLast.m_uFoundRowsMax );
			tLast.m_uFoundRowsSum += uFoundRows;

			tLast.m_uQueryTimeMin = Min( uQueryTime, tLast.m_uQueryTimeMin );
			tLast.m_uQueryTimeMax = Max( uQueryTime, tLast.m_uQueryTimeMax );
			tLast.m_uQueryTimeSum += uQueryTime;

			tLast.m_iCount++;

			return;
		}
	}

	const uint64_t MAX_TIME_DELTA = 15 * 60 * 1000000;
	while ( !m_dRecords.IsEmpty() && ( uTimestamp - m_dRecords[0].m_uTimestamp )>MAX_TIME_DELTA )
		m_dRecords.Pop();

	QueryStatRecord_t& tRecord = m_dRecords.Push();
	tRecord.m_uFoundRowsMin = uFoundRows;
	tRecord.m_uFoundRowsMax = uFoundRows;
	tRecord.m_uFoundRowsSum = uFoundRows;

	tRecord.m_uQueryTimeMin = uQueryTime;
	tRecord.m_uQueryTimeMax = uQueryTime;
	tRecord.m_uQueryTimeSum = uQueryTime;

	tRecord.m_uTimestamp = uTimestamp;
	tRecord.m_iCount = 1;
}

void QueryStatContainer_c::GetRecord( int iRecord, QueryStatRecord_t& tRecord ) const
{
	tRecord = m_dRecords[iRecord];
}


int QueryStatContainer_c::GetNumRecords() const
{
	return m_dRecords.GetLength();
}

QueryStatContainer_c::QueryStatContainer_c() = default;

QueryStatContainer_c::QueryStatContainer_c( QueryStatContainer_c&& tOther ) noexcept
	: QueryStatContainer_c()
{ Swap( tOther ); }

void QueryStatContainer_c::Swap( QueryStatContainer_c& rhs ) noexcept
{
	rhs.m_dRecords.Swap( m_dRecords );
}

QueryStatContainer_c& QueryStatContainer_c::operator=( QueryStatContainer_c tOther ) noexcept
{
	Swap( tOther );
	return *this;
}

//////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG

class QueryStatContainerExact_c: public QueryStatContainer_i
{
public:
	void Add( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp ) final;
	void GetRecord( int iRecord, QueryStatRecord_t& tRecord ) const final;
	int GetNumRecords() const final;

	QueryStatContainerExact_c();
	QueryStatContainerExact_c( QueryStatContainerExact_c&& tOther ) noexcept;
	void Swap( QueryStatContainerExact_c& rhs ) noexcept;
	QueryStatContainerExact_c& operator=( QueryStatContainerExact_c tOther ) noexcept;

private:
	struct QueryStatRecordExact_t
	{
		uint64_t m_uQueryTime;
		uint64_t m_uFoundRows;
		uint64_t m_uTimestamp;
	};

	CircularBuffer_T<QueryStatRecordExact_t> m_dRecords;
};

void QueryStatContainerExact_c::Add( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp )
{
	const uint64_t MAX_TIME_DELTA = 15 * 60 * 1000000;
	while ( !m_dRecords.IsEmpty() && ( uTimestamp - m_dRecords[0].m_uTimestamp )>MAX_TIME_DELTA )
		m_dRecords.Pop();

	QueryStatRecordExact_t& tRecord = m_dRecords.Push();
	tRecord.m_uFoundRows = uFoundRows;
	tRecord.m_uQueryTime = uQueryTime;
	tRecord.m_uTimestamp = uTimestamp;
}


int QueryStatContainerExact_c::GetNumRecords() const
{
	return m_dRecords.GetLength();
}


void QueryStatContainerExact_c::GetRecord( int iRecord, QueryStatRecord_t& tRecord ) const
{
	const QueryStatRecordExact_t& tExact = m_dRecords[iRecord];

	tRecord.m_uQueryTimeMin = tExact.m_uQueryTime;
	tRecord.m_uQueryTimeMax = tExact.m_uQueryTime;
	tRecord.m_uQueryTimeSum = tExact.m_uQueryTime;
	tRecord.m_uFoundRowsMin = tExact.m_uFoundRows;
	tRecord.m_uFoundRowsMax = tExact.m_uFoundRows;
	tRecord.m_uFoundRowsSum = tExact.m_uFoundRows;

	tRecord.m_uTimestamp = tExact.m_uTimestamp;
	tRecord.m_iCount = 1;
}

QueryStatContainerExact_c::QueryStatContainerExact_c() = default;

QueryStatContainerExact_c::QueryStatContainerExact_c( QueryStatContainerExact_c&& tOther ) noexcept
	: QueryStatContainerExact_c()
{ Swap( tOther ); }

void QueryStatContainerExact_c::Swap( QueryStatContainerExact_c& rhs ) noexcept
{
	rhs.m_dRecords.Swap( m_dRecords );
}

QueryStatContainerExact_c& QueryStatContainerExact_c::operator=( QueryStatContainerExact_c tOther ) noexcept
{
	Swap( tOther );
	return *this;
}

#endif


//////////////////////////////////////////////////////////////////////////
ServedStats_c::ServedStats_c()
	: m_pQueryStatRecords { new QueryStatContainer_c }
#ifndef NDEBUG
	, m_pQueryStatRecordsExact { new QueryStatContainerExact_c }
#endif
{
	Verify ( m_tStatsLock.Init( true ));
	m_pQueryTimeDigest = sphCreateTDigest();
	m_pRowsFoundDigest = sphCreateTDigest();
	assert ( m_pQueryTimeDigest && m_pRowsFoundDigest );
}


ServedStats_c::~ServedStats_c()
{
	SafeDelete ( m_pRowsFoundDigest );
	SafeDelete ( m_pQueryTimeDigest );
	m_tStatsLock.Done();
}

void ServedStats_c::AddQueryStat( uint64_t uFoundRows, uint64_t uQueryTime )
{
	ScWL_t wLock( m_tStatsLock );

	m_pRowsFoundDigest->Add(( double ) uFoundRows );
	m_pQueryTimeDigest->Add(( double ) uQueryTime );

	uint64_t uTimeStamp = sphMicroTimer();
	m_pQueryStatRecords->Add( uFoundRows, uQueryTime, uTimeStamp );

#ifndef NDEBUG
	m_pQueryStatRecordsExact->Add( uFoundRows, uQueryTime, uTimeStamp );
#endif

	m_uTotalFoundRowsMin = Min( uFoundRows, m_uTotalFoundRowsMin );
	m_uTotalFoundRowsMax = Max( uFoundRows, m_uTotalFoundRowsMax );
	m_uTotalFoundRowsSum += uFoundRows;

	m_uTotalQueryTimeMin = Min( uQueryTime, m_uTotalQueryTimeMin );
	m_uTotalQueryTimeMax = Max( uQueryTime, m_uTotalQueryTimeMax );
	m_uTotalQueryTimeSum += uQueryTime;

	++m_uTotalQueries;
}


static const uint64_t g_dStatsIntervals[] =
	{
		1 * 60 * 1000000,
		5 * 60 * 1000000,
		15 * 60 * 1000000
	};


void ServedStats_c::CalculateQueryStats( QueryStats_t& tRowsFoundStats, QueryStats_t& tQueryTimeStats ) const
{
	DoStatCalcStats( m_pQueryStatRecords.Ptr(), tRowsFoundStats, tQueryTimeStats );
}


#ifndef NDEBUG

void ServedStats_c::CalculateQueryStatsExact( QueryStats_t& tRowsFoundStats, QueryStats_t& tQueryTimeStats ) const
{
	DoStatCalcStats( m_pQueryStatRecordsExact.Ptr(), tRowsFoundStats, tQueryTimeStats );
}

#endif // !NDEBUG


void ServedStats_c::CalcStatsForInterval( const QueryStatContainer_i* pContainer, QueryStatElement_t& tRowResult,
	QueryStatElement_t& tTimeResult, uint64_t uTimestamp, uint64_t uInterval, int iRecords )
{
	assert ( pContainer );
	using namespace QueryStats;

	tRowResult.m_dData[TYPE_AVG] = 0;
	tRowResult.m_dData[TYPE_MIN] = UINT64_MAX;
	tRowResult.m_dData[TYPE_MAX] = 0;

	tTimeResult.m_dData[TYPE_AVG] = 0;
	tTimeResult.m_dData[TYPE_MIN] = UINT64_MAX;
	tTimeResult.m_dData[TYPE_MAX] = 0;

	CSphTightVector<uint64_t> dFound, dTime;
	dFound.Reserve( iRecords );
	dTime.Reserve( iRecords );

	DWORD uTotalQueries = 0;
	QueryStatRecord_t tRecord;

	for ( int i = 0; i<pContainer->GetNumRecords(); ++i )
	{
		pContainer->GetRecord( i, tRecord );

		if ( uTimestamp - tRecord.m_uTimestamp<=uInterval )
		{
			tRowResult.m_dData[TYPE_MIN] = Min( tRecord.m_uFoundRowsMin, tRowResult.m_dData[TYPE_MIN] );
			tRowResult.m_dData[TYPE_MAX] = Max( tRecord.m_uFoundRowsMax, tRowResult.m_dData[TYPE_MAX] );

			tTimeResult.m_dData[TYPE_MIN] = Min( tRecord.m_uQueryTimeMin, tTimeResult.m_dData[TYPE_MIN] );
			tTimeResult.m_dData[TYPE_MAX] = Max( tRecord.m_uQueryTimeMax, tTimeResult.m_dData[TYPE_MAX] );

			dFound.Add( tRecord.m_uFoundRowsSum / tRecord.m_iCount );
			dTime.Add( tRecord.m_uQueryTimeSum / tRecord.m_iCount );

			tRowResult.m_dData[TYPE_AVG] += tRecord.m_uFoundRowsSum;
			tTimeResult.m_dData[TYPE_AVG] += tRecord.m_uQueryTimeSum;
			uTotalQueries += tRecord.m_iCount;
		}
	}

	dFound.Sort();
	dTime.Sort();

	tRowResult.m_uTotalQueries = uTotalQueries;
	tTimeResult.m_uTotalQueries = uTotalQueries;

	if ( !dFound.GetLength())
		return;

	tRowResult.m_dData[TYPE_AVG] /= uTotalQueries;
	tTimeResult.m_dData[TYPE_AVG] /= uTotalQueries;

	int u95 = Max( 0, Min( int( ceilf( dFound.GetLength() * 0.95f ) + 0.5f ) - 1, dFound.GetLength() - 1 ));
	int u99 = Max( 0, Min( int( ceilf( dFound.GetLength() * 0.99f ) + 0.5f ) - 1, dFound.GetLength() - 1 ));

	tRowResult.m_dData[TYPE_95] = dFound[u95];
	tRowResult.m_dData[TYPE_99] = dFound[u99];

	tTimeResult.m_dData[TYPE_95] = dTime[u95];
	tTimeResult.m_dData[TYPE_99] = dTime[u99];
}

void ServedStats_c::DoStatCalcStats( const QueryStatContainer_i* pContainer,
	QueryStats_t& tRowsFoundStats, QueryStats_t& tQueryTimeStats ) const
{
	assert ( pContainer );
	using namespace QueryStats;

	auto uTimestamp = sphMicroTimer();

	ScRL_t rLock( m_tStatsLock );

	int iRecords = m_pQueryStatRecords->GetNumRecords();
	for ( int i = INTERVAL_1MIN; i<=INTERVAL_15MIN; ++i )
		CalcStatsForInterval( pContainer, tRowsFoundStats.m_dStats[i], tQueryTimeStats.m_dStats[i], uTimestamp,
							  g_dStatsIntervals[i], iRecords );

	auto& tRowsAllStats = tRowsFoundStats.m_dStats[INTERVAL_ALLTIME];
	tRowsAllStats.m_dData[TYPE_AVG] = m_uTotalQueries ? m_uTotalFoundRowsSum / m_uTotalQueries : 0;
	tRowsAllStats.m_dData[TYPE_MIN] = m_uTotalFoundRowsMin;
	tRowsAllStats.m_dData[TYPE_MAX] = m_uTotalFoundRowsMax;
	tRowsAllStats.m_dData[TYPE_95] = ( uint64_t ) m_pRowsFoundDigest->Percentile( 95 );
	tRowsAllStats.m_dData[TYPE_99] = ( uint64_t ) m_pRowsFoundDigest->Percentile( 99 );
	tRowsAllStats.m_uTotalQueries = m_uTotalQueries;

	auto& tQueryAllStats = tQueryTimeStats.m_dStats[INTERVAL_ALLTIME];
	tQueryAllStats.m_dData[TYPE_AVG] = m_uTotalQueries ? m_uTotalQueryTimeSum / m_uTotalQueries : 0;
	tQueryAllStats.m_dData[TYPE_MIN] = m_uTotalQueryTimeMin;
	tQueryAllStats.m_dData[TYPE_MAX] = m_uTotalQueryTimeMax;
	tQueryAllStats.m_dData[TYPE_95] = ( uint64_t ) m_pQueryTimeDigest->Percentile( 95 );
	tQueryAllStats.m_dData[TYPE_99] = ( uint64_t ) m_pQueryTimeDigest->Percentile( 99 );
	tQueryAllStats.m_uTotalQueries = m_uTotalQueries;
}

//////////////////////////////////////////////////////////////////////////
ServedDesc_t::~ServedDesc_t()
{
	if ( m_pIndex )
		m_pIndex->Dealloc();
	if ( !m_sUnlink.IsEmpty())
	{
		sphLogDebug( "unlink %s", m_sUnlink.cstr());
		sphUnlinkIndex( m_sUnlink.cstr(), false );
	}
	SafeDelete ( m_pIndex );
}

//////////////////////////////////////////////////////////////////////////
const ServedDesc_t* ServedIndex_c::ReadLock() const
{
	AddRef();
	if ( m_tLock.ReadLock())
		sphLogDebugvv( "ReadLock %p", this );
	else
	{
		sphLogDebug( "ReadLock %p failed", this );
		assert ( false );
	}
	return ( const ServedDesc_t* ) this;
}

// want write lock to wipe out reader and not wait readers
// but only for RT and PQ indexes as these operations are rare there
ServedDesc_t* ServedIndex_c::WriteLock() const
{
	AddRef();
	sphLogDebugvv( "WriteLock %p wait", this );
	if ( m_tLock.WriteLock())
		sphLogDebugvv( "WriteLock %p", this );
	else
	{
		sphLogDebug( "WriteLock %p failed", this );
		assert ( false );
	}
	return ( ServedDesc_t* ) this;
}

void ServedIndex_c::Unlock() const
{
	if ( m_tLock.Unlock())
		sphLogDebugvv( "Unlock %p", this );
	else
	{
		sphLogDebug( "Unlock %p failed", this );
		assert ( false );
	}
	Release();
}

ServedIndex_c::ServedIndex_c( const ServedDesc_t& tDesc )
	: m_tLock( ServedDesc_t::IsMutable( &tDesc ))
{
	*( ServedDesc_t* ) ( this ) = tDesc;
}


//////////////////////////////////////////////////////////////////////////
GuardedHash_c::GuardedHash_c()
{
	if ( !m_tIndexesRWLock.Init())
		sphDie( "failed to init hash indexes rwlock" );
}

GuardedHash_c::~GuardedHash_c()
{
	ReleaseAndClear();
	Verify ( m_tIndexesRWLock.Done());
}

// atomically try add an entry and adopt it
bool GuardedHash_c::AddUniq( ISphRefcountedMT* pValue, const CSphString& tKey )
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	int iPrevSize = GetLengthUnl();
	ISphRefcountedMT*& pVal = m_hIndexes.AddUnique( tKey );
	if ( iPrevSize==GetLengthUnl())
		return false;

	pVal = pValue;
	SafeAddRef ( pVal );
	return true;
}

// atomically set new entry, then release previous, if not the same and is non-zero
void GuardedHash_c::AddOrReplace( ISphRefcountedMT* pValue, const CSphString& tKey )
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	// can not use AddUnique as new inserted item has no values
	ISphRefcountedMT** ppEntry = m_hIndexes( tKey );
	if ( ppEntry )
	{
		SafeRelease ( *ppEntry );
		( *ppEntry ) = pValue;
	} else
	{
		Verify ( m_hIndexes.Add( pValue, tKey ));
	}
	SafeAddRef ( pValue );
	if ( m_pHook )
		m_pHook( pValue, tKey );
}

bool GuardedHash_c::Delete( const CSphString& tKey )
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	ISphRefcountedMT** ppEntry = m_hIndexes( tKey );
	// release entry - last owner will free it
	if ( ppEntry ) SafeRelease( *ppEntry );

	// remove from hash
	return m_hIndexes.Delete( tKey );
}

bool GuardedHash_c::DeleteIfNull( const CSphString& tKey )
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	ISphRefcountedMT** ppEntry = m_hIndexes( tKey );
	if ( ppEntry && *ppEntry )
		return false;
	return m_hIndexes.Delete( tKey );
}

int GuardedHash_c::GetLength() const
{
	CSphScopedRLock dRL { m_tIndexesRWLock };
	return GetLengthUnl();
}

// check if hash contains an entry
bool GuardedHash_c::Contains( const CSphString& tKey ) const
{
	ScRL_t hHashRLock { m_tIndexesRWLock };
	ISphRefcountedMT** ppEntry = m_hIndexes( tKey );
	return ppEntry!=nullptr;
}

void GuardedHash_c::ReleaseAndClear()
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	for ( m_hIndexes.IterateStart(); m_hIndexes.IterateNext(); ) SafeRelease ( m_hIndexes.IterateGet());

	m_hIndexes.Reset();
}

ISphRefcountedMT* GuardedHash_c::Get( const CSphString& tKey ) const
{
	ScRL_t hHashRLock { m_tIndexesRWLock };
	ISphRefcountedMT** ppEntry = m_hIndexes( tKey );
	if ( !ppEntry )
		return nullptr;
	if ( !*ppEntry )
		return nullptr;
	( *ppEntry )->AddRef();
	return *ppEntry;
}

ISphRefcountedMT* GuardedHash_c::TryAddThenGet( ISphRefcountedMT* pValue, const CSphString& tKey )
{
	ScWL_t hHashWLock { m_tIndexesRWLock };
	int iPrevSize = GetLengthUnl();
	ISphRefcountedMT*& pVal = m_hIndexes.AddUnique( tKey );
	if ( iPrevSize<GetLengthUnl()) // value just inserted
	{
		pVal = pValue;
		SafeAddRef ( pVal );
	}

	SafeAddRef ( pVal );
	return pVal;
}

int GuardedHash_c::GetLengthUnl() const
{
	return m_hIndexes.GetLength();
}

void GuardedHash_c::Rlock() const
{
	Verify ( m_tIndexesRWLock.ReadLock());
}

void GuardedHash_c::Wlock() const
{
	Verify ( m_tIndexesRWLock.WriteLock());
}

void GuardedHash_c::Unlock() const
{
	Verify ( m_tIndexesRWLock.Unlock());
}


CSphString GetMacAddress()
{
	StringBuilder_c sMAC( ":" );

#if USE_WINDOWS
	CSphFixedVector<IP_ADAPTER_ADDRESSES> dAdapters ( 128 );
	PIP_ADAPTER_ADDRESSES pAdapter = dAdapters.Begin();
	DWORD uSize = dAdapters.GetLengthBytes();
	if ( GetAdaptersAddresses ( 0, 0, nullptr, pAdapter, &uSize )==NO_ERROR )
	{
		while ( pAdapter )
		{
			if ( pAdapter->IfType == IF_TYPE_ETHERNET_CSMACD && pAdapter->PhysicalAddressLength>=6 )
			{
				const BYTE * pMAC = pAdapter->PhysicalAddress;
				for ( DWORD i=0; i<pAdapter->PhysicalAddressLength; i++ )
				{
					sMAC.Appendf ( "%02x", *pMAC );
					pMAC++;
				}
				break;
			}
			pAdapter = pAdapter->Next;
		}
	}
#elif defined(__FreeBSD__)
	size_t iLen = 0;
	const int iMibLen = 6;
	int dMib[iMibLen] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };

	if ( sysctl ( dMib, iMibLen, NULL, &iLen, NULL, 0 )!=-1 )
	{
		CSphFixedVector<char> dBuf ( iLen );
		if ( sysctl ( dMib, iMibLen, dBuf.Begin(), &iLen, NULL, 0 )>=0 )
		{
			if_msghdr * pIf = nullptr;
			for ( const char * pNext = dBuf.Begin(); pNext<dBuf.Begin() + iLen; pNext+=pIf->ifm_msglen )
			{
				pIf = (if_msghdr *)pNext;
				if ( pIf->ifm_type==RTM_IFINFO )
				{
					bool bAllZero = true;
					const sockaddr_dl * pSdl= (const sockaddr_dl *)(pIf + 1);
					const BYTE * pMAC = (const BYTE *)LLADDR(pSdl);
					for ( int i=0; i<ETHER_ADDR_LEN; i++ )
					{
						BYTE uPart = *pMAC;
						pMAC++;
						bAllZero &= ( uPart==0 );
						sMAC.Appendf ( "%02x", uPart );
					}

					if ( !bAllZero )
						break;

					sMAC.Clear();
					sMAC.StartBlock ( ":" );
				}
			}
		}
	}
#elif defined ( __APPLE__ )
	// no MAC address for OSX

#else
	int iFD = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( iFD>=0 )
	{
		ifreq dIf[64];
		ifconf tIfConf;
		tIfConf.ifc_len = sizeof( dIf );
		tIfConf.ifc_req = dIf;

		if ( ioctl( iFD, SIOCGIFCONF, &tIfConf )>=0 )
		{
			const ifreq* pIfEnd = dIf + ( tIfConf.ifc_len / sizeof( dIf[0] ));
			for ( const ifreq* pIfCur = tIfConf.ifc_req; pIfCur<pIfEnd; pIfCur++ )
			{
				if ( pIfCur->ifr_addr.sa_family==AF_INET )
				{
					ifreq tIfCur;
					memset( &tIfCur, 0, sizeof( tIfCur ));
					memcpy( tIfCur.ifr_name, pIfCur->ifr_name, sizeof( tIfCur.ifr_name ));
					if ( ioctl( iFD, SIOCGIFHWADDR, &tIfCur )>=0 )
					{
						bool bAllZero = true;
						const BYTE* pMAC = ( const BYTE* ) tIfCur.ifr_hwaddr.sa_data;
						for ( int i = 0; i<ETHER_ADDR_LEN; i++ )
						{
							BYTE uPart = *pMAC;
							pMAC++;
							bAllZero &= ( uPart==0 );
							sMAC.Appendf( "%02x", uPart );
						}

						if ( !bAllZero )
							break;

						sMAC.Clear();
						sMAC.StartBlock( ":" );
					}
				}
			}
		}
	}
	SafeClose( iFD );
#endif

	return sMAC.cstr();
}

struct Handler_t : public ListNode_t
{
	Handler_fn	m_fnCb;
	Handler_t ( Handler_fn&& fnCb )
		: m_fnCb ( std::move ( fnCb )) {}
};

static RwLock_t dShutdownGuard;
static List_t dShutdownList GUARDED_BY ( dShutdownGuard );

void * searchd::AddShutdownCb ( std::function<void ()> fnCb )
{
	auto * pCb = new Handler_t ( std::move ( fnCb ) );
	ScWL_t tGuard ( dShutdownGuard );
	dShutdownList.Add ( pCb );
	return pCb;
}

// remove previously set shutdown cb by cookie
void searchd::DeleteShutdownCb ( void * pCookie )
{
	if ( !pCookie )
		return;

	auto * pCb = (Handler_t *) pCookie;

	ScWL_t tGuard ( dShutdownGuard );
	if ( !dShutdownList.GetLength () )
		return;

	dShutdownList.Remove ( pCb );
	SafeDelete ( pCb );
}

// invoke shutdown handlers
void searchd::FireShutdownCbs ()
{
	ScRL_t tGuard ( dShutdownGuard );
	while (dShutdownList.GetLength ())
	{
		auto * pCb = (Handler_t *) dShutdownList.Begin ();
		dShutdownList.Remove ( pCb );
		pCb->m_fnCb();
		SafeDelete( pCb );
	}
}