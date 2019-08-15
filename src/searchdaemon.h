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

/// @file searchdaemon.h
/// Declarations for the stuff specifically needed by searchd to serve the indexes.


#ifndef _searchdaemon_
#define _searchdaemon_

/////////////////////////////////////////////////////////////////////////////
// MACHINE-DEPENDENT STUFF
/////////////////////////////////////////////////////////////////////////////

#if USE_WINDOWS
	// Win-specific headers and calls
	#include <winsock2.h>

	#define HAVE_POLL 1
	static inline int poll ( struct pollfd *pfd, int nfds, int timeout ) { return WSAPoll ( pfd, nfds, timeout ); }
	#pragma comment(linker, "/defaultlib:ws2_32.lib")
	#pragma message("Automatically linking with ws2_32.lib")

	// socket function definitions
	#pragma comment(linker, "/defaultlib:wsock32.lib")
	#pragma message("Automatically linking with wsock32.lib")

	#define sphSockRecv(_sock,_buf,_len)	::recv(_sock,_buf,_len,0)
	#define sphSockSend(_sock,_buf,_len)	::send(_sock,_buf,_len,0)
	#define sphSockClose(_sock)				::closesocket(_sock)
	#define SSIZE_T long
		using sphIovec=WSABUF;
	#define IOBUFTYPE (CHAR FAR *)
	#define IOPTR(tX) (tX.buf)
	#define IOLEN(tX) (tX.len)

	// Windows hacks
	#undef EINTR
	#undef EWOULDBLOCK
	#undef ETIMEDOUT
	#undef EINPROGRESS
	#undef ECONNRESET
	#undef ECONNABORTED
	#define LOCK_EX			0
	#define LOCK_UN			1
	#define STDIN_FILENO	fileno(stdin)
	#define STDOUT_FILENO	fileno(stdout)
	#define STDERR_FILENO	fileno(stderr)
	#define ETIMEDOUT		WSAETIMEDOUT
	#define EWOULDBLOCK		WSAEWOULDBLOCK
	#define EINPROGRESS		WSAEINPROGRESS
	#define EINTR			WSAEINTR
	#define ECONNRESET		WSAECONNRESET
	#define ECONNABORTED	WSAECONNABORTED
	#define ESHUTDOWN		WSAESHUTDOWN

	#define ftruncate		_chsize
	#define getpid			GetCurrentProcessId


#else
	// UNIX-specific headers and calls
	#include <sys/socket.h>
	#include <sys/un.h>
	#include <arpa/inet.h>

	#if HAVE_POLL
	#include <poll.h>
	#endif

	#if HAVE_EPOLL
	#include <sys/epoll.h>
	#endif

	// there's no MSG_NOSIGNAL on OS X
	#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
	#endif

	#define sphSockRecv(_sock,_buf,_len)	::recv(_sock,_buf,_len,MSG_NOSIGNAL)
	#define sphSockSend(_sock,_buf,_len)	::send(_sock,_buf,_len,MSG_NOSIGNAL)
	#define sphSockClose(_sock)				::close(_sock)
	#define SSIZE_T ssize_t
	using sphIovec = iovec;
	#define IOBUFTYPE
	#define IOPTR(tX) (tX.iov_base)
	#define IOLEN(tX) (tX.iov_len)
#endif

// declarations for correct work of code analysis
#include "sphinxutils.h"
#include "sphinxint.h"
#include "sphinxrt.h"
#include "threadutils.h"
using namespace Threads;

#define SPHINXAPI_PORT            9312
#define SPHINXQL_PORT            9306
#define SPH_ADDRESS_SIZE        sizeof("000.000.000.000")
#define SPH_ADDRPORT_SIZE        sizeof("000.000.000.000:00000")
#define NETOUTBUF                8192

// strict check, sphGetAddress will die with sphFatal() on failure
#define GETADDR_STRICT	true

// these defined in searchd.cpp
void sphFatal( const char* sFmt, ... ) __attribute__ (( format ( printf, 1, 2 ))) NO_RETURN;
void sphFatalLog( const char* sFmt, ... ) __attribute__ (( format ( printf, 1, 2 )));
volatile bool& sphGetGotSigterm();

/////////////////////////////////////////////////////////////////////////////
// SOME SHARED GLOBAL VARIABLES
/////////////////////////////////////////////////////////////////////////////

// these are in searchd.cpp
extern int g_iReadTimeout;        // defined in searchd.cpp
extern int g_iWriteTimeout;    // sec

extern int g_iMaxPacketSize;    // in bytes; for both query packets from clients and response packets from agents
extern int g_iDistThreads;

static const int64_t MS2SEC = I64C ( 1000000 );

/////////////////////////////////////////////////////////////////////////////
// MISC GLOBALS
/////////////////////////////////////////////////////////////////////////////

/// known commands
/// (shared here because at least SEARCHD_COMMAND_TOTAL used outside the core)
enum SearchdCommand_e : WORD
{
	SEARCHD_COMMAND_SEARCH		= 0,
	SEARCHD_COMMAND_EXCERPT		= 1,
	SEARCHD_COMMAND_UPDATE		= 2,
	SEARCHD_COMMAND_KEYWORDS	= 3,
	SEARCHD_COMMAND_PERSIST		= 4,
	SEARCHD_COMMAND_STATUS		= 5,
	SEARCHD_COMMAND_FLUSHATTRS	= 7,
	SEARCHD_COMMAND_SPHINXQL	= 8,
	SEARCHD_COMMAND_PING		= 9,
	SEARCHD_COMMAND_DELETE		= 10,
	SEARCHD_COMMAND_UVAR		= 11,
	SEARCHD_COMMAND_INSERT		= 12,
	SEARCHD_COMMAND_REPLACE		= 13,
	SEARCHD_COMMAND_COMMIT		= 14,
	SEARCHD_COMMAND_SUGGEST		= 15,
	SEARCHD_COMMAND_JSON		= 16,
	SEARCHD_COMMAND_CALLPQ 		= 17,
	SEARCHD_COMMAND_CLUSTERPQ	= 18,

	SEARCHD_COMMAND_TOTAL,
	SEARCHD_COMMAND_WRONG = SEARCHD_COMMAND_TOTAL,
};

/// master-agent API SEARCH command protocol extensions version
enum
{
	VER_COMMAND_SEARCH_MASTER = 17
};


/// known command versions
/// (shared here because of REPLICATE)
enum SearchdCommandV_e : WORD
{
	VER_COMMAND_SEARCH		= 0x121, // 1.33
	VER_COMMAND_EXCERPT		= 0x104,
	VER_COMMAND_UPDATE		= 0x104,
	VER_COMMAND_KEYWORDS	= 0x101,
	VER_COMMAND_STATUS		= 0x101,
	VER_COMMAND_FLUSHATTRS	= 0x100,
	VER_COMMAND_SPHINXQL	= 0x100,
	VER_COMMAND_JSON		= 0x100,
	VER_COMMAND_PING		= 0x100,
	VER_COMMAND_UVAR		= 0x100,
	VER_COMMAND_CALLPQ		= 0x100,
	VER_COMMAND_CLUSTERPQ	= 0x102,

	VER_COMMAND_WRONG = 0,
};

enum UpdateType_e
{
	UPDATE_INT		= 0,
	UPDATE_MVA32	= 1,
	UPDATE_STRING	= 2,
	UPDATE_JSON		= 3
};

enum ESphAddIndex
{
	ADD_ERROR	= 0, // wasn't added because of config or other error
	ADD_DSBLED	= 1, // added into disabled hash (need to prealloc/preload, etc)
	ADD_DISTR	= 2, // distributed
	ADD_SERVED	= 3, // added and active (can be used in queries)
//	ADD_CLUSTER	= 4,
};

enum class IndexType_e
{
	PLAIN = 0,
	TEMPLATE,
	RT,
	PERCOLATE,
	DISTR,
	ERROR_, // simple "ERROR" doesn't work on win due to '#define ERROR 0' somewhere.
};

struct ListenerDesc_t
{
	Proto_e m_eProto;
	CSphString m_sUnix;
	DWORD m_uIP;
	int m_iPort;
	int m_iPortsCount;
	bool m_bVIP;
};

// 'like' matcher
class CheckLike
{
private:
	CSphString m_sPattern;

public:
	explicit CheckLike( const char* sPattern );
	bool Match( const char* sValue );
};

// string vector with 'like' matcher
class VectorLike: public StrVec_t, public CheckLike
{
public:
	CSphString m_sColKey;
	CSphString m_sColValue;

public:

	VectorLike();
	explicit VectorLike( const CSphString& sPattern );

	const char* szColKey() const;
	const char* szColValue() const;
	bool MatchAdd( const char* sValue );
	bool MatchAddVa( const char* sTemplate, ... ) __attribute__ (( format ( printf, 2, 3 )));
};

CSphString GetTypeName ( IndexType_e eType );
IndexType_e TypeOfIndexConfig ( const CSphString & sType );

// forwards from searchd
void CheckPort( int iPort );
ListenerDesc_t ParseListener( const char* sSpec );

// use check outside ParseListener in order to make tests consistent despite platforms
#if USE_WINDOWS
	#define CHECK_LISTENER(dListener) \
		if ( !(dListener).m_sUnix.IsEmpty() ) \
			sphFatal( "UNIX sockets are not supported on Windows" );
#else
	#define CHECK_LISTENER(dListener)
#endif


/////////////////////////////////////////////////////////////////////////////
// NETWORK SOCKET WRAPPERS
/////////////////////////////////////////////////////////////////////////////

const char* sphSockError( int = 0 );
int sphSockGetErrno();
void sphSockSetErrno( int );
int sphSockPeekErrno();
int sphSetSockNB( int );
int RecvNBChunk( int iSock, char*& pBuf, int& iLeftBytes );
int sphPoll( int iSock, int64_t tmTimeout, bool bWrite = false );
void sphFDSet( int fd, fd_set* fdset );
void sphFDClr( int fd, fd_set* fdset );


/** \brief wrapper over getaddrinfo
Invokes getaddrinfo for given host which perform name resolving (dns).
 \param[in] sHost the host name
 \param[in] bFatal whether failed resolving is fatal (false by default)
 \param[in] bIP set true if sHost contains IP address (false by default)
 so no potentially lengthy network host address lockup necessary
 \return ipv4 address as DWORD, to be directly used as s_addr in connect(). */
DWORD sphGetAddress( const char* sHost, bool bFatal = false, bool bIP = false );
char* sphFormatIP( char* sBuffer, int iBufferSize, DWORD uAddress );
CSphString GetMacAddress();

bool IsPortInRange( int iPort );
int sphSockRead( int iSock, void* buf, int iLen, int iReadTimeout, bool bIntr );

// first try to get data, and only then fall into sphSockRead (which poll socket first)
int SockReadFast( int iSock, void* buf, int iLen, int iReadTimeout );

/////////////////////////////////////////////////////////////////////////////
// NETWORK BUFFERS
/////////////////////////////////////////////////////////////////////////////

/// dynamic send buffer
/// to remove ISphNoncopyable just add copy c-tor and operator=
/// we just cache all streamed data into internal blob;
/// NO data sending itself lives in this class.

class ISphOutputBuffer : public ISphRefcountedMT
{
public:
	ISphOutputBuffer ();
	explicit ISphOutputBuffer ( CSphVector<BYTE>& dChunk );
	~ISphOutputBuffer() override {}

	void		SendInt ( int iValue )			{ SendT<int> ( htonl ( iValue ) ); }

	void		SendAsDword ( int64_t iValue ) ///< sends the 32bit MAX_UINT if the value is greater than it.
	{
		iValue = Min ( Max ( iValue, 0 ), UINT_MAX );
		SendDword ( DWORD(iValue) );
	}
	void		SendDword ( DWORD iValue )		{ SendT<DWORD> ( htonl ( iValue ) ); }
	void		SendWord ( WORD iValue )		{ SendT<WORD> ( htons ( iValue ) ); }
	void		SendFloat ( float fValue )		{ SendT<DWORD> ( htonl ( sphF2DW ( fValue ) ) ); }
	void		SendByte ( BYTE uValue )		{ SendT<BYTE> ( uValue ); }

	void SendLSBDword ( DWORD v )
	{
		SendByte ( (BYTE)( v & 0xff ) );
		SendByte ( (BYTE)( (v>>8) & 0xff ) );
		SendByte ( (BYTE)( (v>>16) & 0xff ) );
		SendByte ( (BYTE)( (v>>24) & 0xff) );
	}

	void SendUint64 ( uint64_t uValue )
	{
		SendT<DWORD> ( htonl ( (DWORD)( uValue>>32) ) );
		SendT<DWORD> ( htonl ( (DWORD)( uValue & 0xffffffffUL) ) );
	}

	void SendUint64 ( int64_t iValue )
	{
		SendUint64 ( (uint64_t) iValue );
	}

	void		SendBytes ( const void * pBuf, int iLen );	///< (was) protected to avoid network-vs-host order bugs
	void		SendBytes ( const char * pBuf );    // used strlen() to get length
	void		SendBytes ( const CSphString& sStr );    // used strlen() to get length
	void		SendBytes ( const VecTraits_T<BYTE>& dBuf );
	void		SendBytes ( const StringBuilder_c& dBuf );

	// send array: first length(int), then byte blob
	void		SendString ( const char * sStr );
	void		SendArray ( const ISphOutputBuffer &tOut );
	void		SendArray ( const VecTraits_T<BYTE> &dBuf, int iElems=-1 );
	void		SendArray ( const void * pBuf, int iLen );
	void		SendArray ( const StringBuilder_c &dBuf );

	virtual void	SwapData ( CSphVector<BYTE> & rhs ) { m_dBuf.SwapData ( rhs ); }

	virtual void	Flush () {}
	virtual bool	GetError () const { return false; }
	virtual int		GetSentCount () const { return m_dBuf.GetLength(); }
	virtual void	SetProfiler ( CSphQueryProfile * ) {}
	virtual void *	GetBufPtr () const { return (char*) m_dBuf.begin();}

protected:
	void WriteInt ( intptr_t iOff, int iValue )
	{ WriteT<int> ( iOff, htonl ( iValue ) ); }

	CSphVector<BYTE>	m_dBuf;

	template < typename T > void WriteT ( intptr_t iOff, T tValue )
	{
		sphUnalignedWrite ( m_dBuf.Begin () + iOff, tValue );
	}

private:
	template < typename T > void	SendT ( T tValue )							///< (was) protected to avoid network-vs-host order bugs
	{
		intptr_t iOff = m_dBuf.GetLength();
		m_dBuf.AddN ( sizeof(T) );
		WriteT ( iOff, tValue );
	}
};


// assume buffer never flushed between different Send()
class CachedOutputBuffer_c : public ISphOutputBuffer
{
	CSphVector<intptr_t> m_dBlobs;
	using BASE = ISphOutputBuffer;

public:
	// start blob on create, commit on dtr.
	class ReqLenCalc : public ISphNoncopyable
	{
		CachedOutputBuffer_c &m_dBuff;
		intptr_t m_iPos;
	public:
		ReqLenCalc ( CachedOutputBuffer_c &dBuff, WORD uCommand, WORD uVer = 0 /* SEARCHD_OK */ )
			: m_dBuff ( dBuff )
		{
			m_dBuff.AddRef();
			m_dBuff.SendWord ( uCommand );
			m_dBuff.SendWord ( uVer );
			m_iPos = m_dBuff.StartMeasureLength();
		}

		~ReqLenCalc ()
		{
			m_dBuff.CommitMeasuredLength ( m_iPos );
			m_dBuff.Release();
		}
	};

public:
	void Flush() override; // just check integrity before flush
	void SwapData ( CSphVector<BYTE> &rhs ) override { CommitAllMeasuredLengths (); BASE::SwapData (rhs); }
	inline bool BlobsEmpty () const { return m_dBlobs.IsEmpty (); }
public:
	intptr_t StartMeasureLength (); // reserve int in the buf, push it's position, return cur pos.
	void CommitMeasuredLength ( intptr_t uStoredPos=-1 ); // get last pushed int, write delta count there.
	void CommitAllMeasuredLengths (); // finalize all nums starting from the last one.
};

using APICommand_t = CachedOutputBuffer_c::ReqLenCalc;

// buffer that knows if it has requested data or not
class SmartOutputBuffer_t : public CachedOutputBuffer_c
{
	CSphVector<ISphOutputBuffer *> m_dChunks;
public:
	SmartOutputBuffer_t () = default;
	~SmartOutputBuffer_t () override;
	int GetSentCount () const override;

	void StartNewChunk ();
//	void AppendBuf ( SmartOutputBuffer_t &dBuf );
//	void PrependBuf ( SmartOutputBuffer_t &dBuf );
	size_t GetIOVec ( CSphVector<sphIovec> &dOut ) const;
	void Reset();
#if USE_WINDOWS
	void LeakTo ( CSphVector<ISphOutputBuffer *> dOut );
#endif
};

class NetOutputBuffer_c : public CachedOutputBuffer_c
{
public:
	explicit	NetOutputBuffer_c ( int iSock );

	void	Flush () override;
	bool	GetError () const override { return m_bError; }
	int		GetSentCount () const override { return m_iSent; }
	void	SetProfiler ( CSphQueryProfile * pProfiler ) override { m_pProfile = pProfiler; }

private:
	CSphQueryProfile *	m_pProfile = nullptr;
	int			m_iSock;			///< my socket
	int			m_iSent = 0;
	bool		m_bError = false;
};


/// generic request buffer
class InputBuffer_c
{
public:
	InputBuffer_c ( const BYTE * pBuf, int iLen );
	InputBuffer_c ( const VecTraits_T<BYTE>& dBuf );
	virtual			~InputBuffer_c () {}

	int				GetInt () { return ntohl ( GetT<int> () ); }
	WORD			GetWord () { return ntohs ( GetT<WORD> () ); }
	DWORD			GetDword () { return ntohl ( GetT<DWORD> () ); }
	DWORD			GetLSBDword () { return GetByte() + ( GetByte()<<8 ) + ( GetByte()<<16 ) + ( GetByte()<<24 ); }
	uint64_t		GetUint64() { uint64_t uRes = GetDword(); return (uRes<<32)+GetDword(); }
	BYTE			GetByte () { return GetT<BYTE> (); }
	float			GetFloat () { return sphDW2F ( ntohl ( GetT<DWORD> () ) ); }
	CSphString		GetString ();
	CSphString		GetRawString ( int iLen );
	bool			GetString ( CSphVector<BYTE> & dBuffer );
	bool			GetError () { return m_bError; }
	bool			GetBytes ( void * pBuf, int iLen );
	const BYTE *	GetBufferPtr () const { return m_pBuf; }
	int				GetLength() const { return m_iLen; }
	bool			GetBytesZerocopy ( const BYTE ** ppData, int iLen );

	bool	GetDwords ( CSphVector<DWORD> & dBuffer, int & iGot, int iMax );
	bool	GetQwords ( CSphVector<SphAttr_t> & dBuffer, int & iGot, int iMax );

	inline int		HasBytes() const
	{
		return ( m_pBuf + m_iLen - m_pCur );
	}

protected:
	const BYTE *	m_pBuf;
	const BYTE *	m_pCur;
	bool			m_bError;
	int				m_iLen;

protected:
	void			SetError ( bool bError ) { m_bError = bError; }
	template < typename T > T	GetT ()
	{
		if ( m_bError || ( m_pCur + sizeof( T )>m_pBuf + m_iLen ))
		{
			SetError( true );
			return 0;
		}

		T iRes = sphUnalignedRead( *( T* ) m_pCur );
		m_pCur += sizeof( T );
		return iRes;
	}
};

/// simple memory request buffer
using MemInputBuffer_c = InputBuffer_c;

/// simple network request buffer
class NetInputBuffer_c : private LazyVector_T<BYTE>, public InputBuffer_c
{
	using STORE = LazyVector_T<BYTE>;
public:
	explicit		NetInputBuffer_c ( int iSock );

	bool			ReadFrom ( int iLen, int iTimeout, bool bIntr=false, bool bAppend=false );
	bool			ReadFrom ( int iLen ) { return ReadFrom ( iLen, g_iReadTimeout ); }

	bool			IsIntr () const { return m_bIntr; }

	using InputBuffer_c::GetLength;
private:
	static const int	NET_MINIBUFFER_SIZE = STORE::iSTATICSIZE;

	int					m_iSock;
	bool				m_bIntr = false;
};



/////////////////////////////////////////////////////////////////////////////
// SERVED INDEX DESCRIPTORS STUFF
/////////////////////////////////////////////////////////////////////////////
namespace QueryStats {
	enum
	{
		INTERVAL_1MIN,
		INTERVAL_5MIN,
		INTERVAL_15MIN,
		INTERVAL_ALLTIME,

		INTERVAL_TOTAL
	};


	enum
	{
		TYPE_AVG,
		TYPE_MIN,
		TYPE_MAX,
		TYPE_95,
		TYPE_99,

		TYPE_TOTAL,
	};
}

struct QueryStatElement_t
{
	uint64_t	m_dData[QueryStats::TYPE_TOTAL] = { 0, UINT64_MAX, 0, 0, 0, };
	uint64_t	m_uTotalQueries = 0;
};


struct QueryStats_t
{
	QueryStatElement_t	m_dStats[QueryStats::INTERVAL_TOTAL];
};


struct QueryStatRecord_t
{
	uint64_t	m_uQueryTimeMin;
	uint64_t	m_uQueryTimeMax;
	uint64_t	m_uQueryTimeSum;
	uint64_t	m_uFoundRowsMin;
	uint64_t	m_uFoundRowsMax;
	uint64_t	m_uFoundRowsSum;

	uint64_t	m_uTimestamp;
	int			m_iCount;
};


class QueryStatContainer_i
{
public:
	virtual								~QueryStatContainer_i() {}
	virtual void						Add ( uint64_t uFoundRows, uint64_t uQueryTime, uint64_t uTimestamp ) = 0;
	virtual void						GetRecord ( int iRecord, QueryStatRecord_t & tRecord ) const = 0;
	virtual int							GetNumRecords() const = 0;
};


class ServedStats_c
{
public:
						ServedStats_c();
	virtual				~ServedStats_c();

	void				AddQueryStat ( uint64_t uFoundRows, uint64_t uQueryTime ); //  REQUIRES ( !m_tStatsLock );
						/// since mutex is internal,
	void				CalculateQueryStats ( QueryStats_t & tRowsFoundStats, QueryStats_t & tQueryTimeStats ) const EXCLUDES ( m_tStatsLock );
#ifndef NDEBUG
	void				CalculateQueryStatsExact ( QueryStats_t & tRowsFoundStats, QueryStats_t & tQueryTimeStats ) const EXCLUDES ( m_tStatsLock );
#endif
private:
	mutable CSphRwlock m_tStatsLock;
	CSphScopedPtr<QueryStatContainer_i> m_pQueryStatRecords GUARDED_BY ( m_tStatsLock );

#ifndef NDEBUG
	CSphScopedPtr<QueryStatContainer_i> m_pQueryStatRecordsExact GUARDED_BY ( m_tStatsLock );
#endif

	TDigest_i *			m_pQueryTimeDigest GUARDED_BY ( m_tStatsLock ) = nullptr;
	TDigest_i *			m_pRowsFoundDigest GUARDED_BY ( m_tStatsLock ) = nullptr;

	uint64_t			m_uTotalFoundRowsMin GUARDED_BY ( m_tStatsLock )= UINT64_MAX;
	uint64_t			m_uTotalFoundRowsMax GUARDED_BY ( m_tStatsLock )= 0;
	uint64_t			m_uTotalFoundRowsSum GUARDED_BY ( m_tStatsLock )= 0;

	uint64_t			m_uTotalQueryTimeMin GUARDED_BY ( m_tStatsLock )= UINT64_MAX;
	uint64_t			m_uTotalQueryTimeMax GUARDED_BY ( m_tStatsLock )= 0;
	uint64_t			m_uTotalQueryTimeSum GUARDED_BY ( m_tStatsLock )= 0;

	uint64_t			m_uTotalQueries GUARDED_BY ( m_tStatsLock ) = 0;

	static void			CalcStatsForInterval ( const QueryStatContainer_i * pContainer, QueryStatElement_t & tRowResult,
							QueryStatElement_t & tTimeResult, uint64_t uTimestamp, uint64_t uInterval, int iRecords );

	void				DoStatCalcStats ( const QueryStatContainer_i * pContainer, QueryStats_t & tRowsFoundStats,
							QueryStats_t & tQueryTimeStats ) const EXCLUDES ( m_tStatsLock );
};


struct ServedDesc_t
{
	CSphIndex *	m_pIndex		= nullptr; ///< owned index; will be deleted in d-tr
	CSphString	m_sIndexPath;	///< current index path; independent but related to one in m_pIndex
	CSphString	m_sNewPath;		///< when reloading because of config changed, it contains path to new index.
	bool		m_bPreopen		= false;
	int			m_iExpandKeywords { KWE_DISABLED };
	bool		m_bOnlyNew		= false; ///< load new clean index - no previous valid files, no .old backups possible, no way to serve if loading failed.
	CSphString	m_sGlobalIDFPath;
	int64_t		m_iMass			= 0; // relative weight (by access speed) of the index
	int			m_iRotationPriority = 0;	// rotation priority (for proper rotation of indexes chained by killlist_target). 0==high priority
	StrVec_t	m_dKilllistTargets;
	mutable CSphString	m_sUnlink;
	IndexType_e	m_eType			= IndexType_e::PLAIN;
	bool		m_bJson			= false; // index came from replication json config, not from usual config file
	CSphString	m_sCluster;
	FileAccessSettings_t m_tFileAccessSettings;

	// statics instead of members below used to simultaneously check pointer for null also.

	// mutable is one which can be insert/replace
	static bool IsMutable ( const ServedDesc_t* pServed )
	{
		if ( !pServed )
			return false;
		return pServed->m_eType==IndexType_e::RT || pServed->m_eType==IndexType_e::PERCOLATE;
	}

	// cluster is one which can deals with replication
	static bool IsCluster ( const ServedDesc_t* pServed )
	{
		if ( !pServed )
			return false;
		return pServed->m_bJson || !pServed->m_sCluster.IsEmpty ();
	}

	// CanSelect is one which supports select ... from (at least full-scan).
	static bool IsSelectable ( const ServedDesc_t* pServed )
	{
		if ( !pServed )
			return false;
		return IsFT ( pServed ) || pServed->m_eType==IndexType_e::PERCOLATE;
	}

	// FT is one which supports full-text searching
	static bool IsFT ( const ServedDesc_t* pServed )
	{
		if ( !pServed )
			return false;
		return pServed->m_eType==IndexType_e::PLAIN
			|| pServed->m_eType==IndexType_e::RT
			|| pServed->m_eType==IndexType_e::DISTR; // fixme! distrs not necessary ft.
	}

	virtual                ~ServedDesc_t ();
};

// wrapped ServedDesc_t - to be served as pointers in containers
// (fully block any access to internals)
// create ServedDesc[R|W]Ptr_c instance to have actual access to the members.
class ServedIndex_c : public ISphRefcountedMT, private ServedDesc_t, public ServedStats_c
{
	mutable RwLock_t m_tLock;
private:
	friend class ServedDescRPtr_c;
	friend class ServedDescWPtr_c;

	const ServedDesc_t * ReadLock () const ACQUIRE_SHARED( m_tLock );
	ServedDesc_t * WriteLock () const ACQUIRE( m_tLock );
	void Unlock () const UNLOCK_FUNCTION( m_tLock );

protected:
	// no manual deletion; lifetime managed by AddRef/Release()
	~ServedIndex_c () override = default;

public:

	explicit ServedIndex_c ( const ServedDesc_t& tDesc );
	//ServedIndex_c ();

	// fake alias to private m_tLock to allow clang thread-safety analysis
	CSphRwlock * rwlock () const RETURN_CAPABILITY ( m_tLock )
	{ return nullptr; }

};


/// RAII shared reader for ServedDesc_t hidden in ServedIndex_c
class SCOPED_CAPABILITY ServedDescRPtr_c : ISphNoncopyable
{
public:
	ServedDescRPtr_c() = default;
	// by default acquire read (shared) lock
	explicit ServedDescRPtr_c ( const ServedIndex_c * pLock ) ACQUIRE_SHARED( pLock->m_tLock )
		: m_pLock { pLock }
	{
		if ( m_pLock )
			m_pCore = m_pLock->ReadLock();
	}

	/// unlock on going out of scope
	~ServedDescRPtr_c () RELEASE ()
	{
		if ( m_pLock )
			m_pLock->Unlock ();
	}

public:
	const ServedDesc_t * operator-> () const
	{ return m_pCore; }

	explicit operator bool () const
	{ return m_pCore!=nullptr; }

	operator const ServedDesc_t * () const
	{ return m_pCore; }

	const ServedDesc_t * Ptr () const
	{ return m_pCore; }

private:
	const ServedDesc_t * m_pCore = nullptr;
	const ServedIndex_c * m_pLock = nullptr;
};


/// RAII exclusive writer for ServedDesc_t hidden in ServedIndex_c
class SCOPED_CAPABILITY ServedDescWPtr_c : ISphNoncopyable
{
public:
	ServedDescWPtr_c () = default;

	// acquire write (exclusive) lock
	explicit ServedDescWPtr_c ( const ServedIndex_c * pLock ) ACQUIRE ( pLock->m_tLock )
		: m_pLock { pLock }
	{
		if ( m_pLock )
			m_pCore = m_pLock->WriteLock ();
	}

	/// unlock on going out of scope
	~ServedDescWPtr_c () RELEASE ()
	{
		if ( m_pLock )
			m_pLock->Unlock ();
	}

public:
	ServedDesc_t * operator-> () const
	{ return m_pCore; }

	explicit operator bool () const
	{ return m_pCore!=nullptr; }

	operator ServedDesc_t * () const
	{ return m_pCore; }

	ServedDesc_t * Ptr () const
	{ return m_pCore; }

private:
	ServedDesc_t * m_pCore = nullptr;
	const ServedIndex_c * m_pLock = nullptr;
};


using ServedIndexRefPtr_c = CSphRefcountedPtr<ServedIndex_c>;

using AddOrReplaceHookFn = std::function<void( ISphRefcountedMT*, const CSphString& )>;

/// hash of ref-counted pointers, guarded by RW-lock
class GuardedHash_c : public ISphNoncopyable
{
	friend class RLockedHashIt_c;
	friend class WLockedHashIt_c;
	using RefCntHash_t = SmallStringHash_T<ISphRefcountedMT *>;

public:
	GuardedHash_c ();
	~GuardedHash_c ();

	// atomically try add an entry and adopt it
	bool AddUniq ( ISphRefcountedMT * pEntry, const CSphString &tKey ) EXCLUDES ( m_tIndexesRWLock );

	// atomically set new entry, then release previous, if not the same and is non-zero
	void AddOrReplace ( ISphRefcountedMT * pEntry, const CSphString &tKey ) EXCLUDES ( m_tIndexesRWLock );

	void SetAddOrReplaceHook( AddOrReplaceHookFn pHook )	{ m_pHook = std::move( pHook ); }

	// release and delete from hash by key
	bool Delete ( const CSphString &tKey ) EXCLUDES ( m_tIndexesRWLock );

	// delete by key if item exists, but null
	bool DeleteIfNull ( const CSphString &tKey ) EXCLUDES ( m_tIndexesRWLock );

	int GetLength () const EXCLUDES ( m_tIndexesRWLock );

	// check if value exists (even if it is nullptr)
	bool Contains ( const CSphString &tKey ) const EXCLUDES ( m_tIndexesRWLock );

	// reset the hash
	void ReleaseAndClear () EXCLUDES ( m_tIndexesRWLock );

	// returns addreffed value
	ISphRefcountedMT * Get ( const CSphString &tKey ) const EXCLUDES ( m_tIndexesRWLock );

	// if value not exist, addref and add it. Then act as Get (addref and return by key)
	ISphRefcountedMT * TryAddThenGet ( ISphRefcountedMT * pValue, const CSphString &tKey ) EXCLUDES ( m_tIndexesRWLock );

	// fake alias to private m_tLock to allow clang thread-safety analysis
	CSphRwlock * IndexesRWLock () const RETURN_CAPABILITY ( m_tIndexesRWLock )
	{ return nullptr; }

private:
	int GetLengthUnl () const REQUIRES_SHARED ( m_tIndexesRWLock );
	void Rlock () const ACQUIRE_SHARED( m_tIndexesRWLock );
	void Wlock () const ACQUIRE ( m_tIndexesRWLock );
	void Unlock () const UNLOCK_FUNCTION ( m_tIndexesRWLock );

private:
	mutable CSphRwlock m_tIndexesRWLock; // distinguishable name for catch possible warnings
	RefCntHash_t m_hIndexes GUARDED_BY ( m_tIndexesRWLock );
	AddOrReplaceHookFn m_pHook = nullptr;
};

// multi-threaded hash iterator
// iterates guarded hash, holding it's readlock
// that is important, since accidental changing of the hash during iteration
// may invalidate the iterator, and then cause crash.
// each iterator has own iteration cookie, so several of them could work in parallel

/* Usage example:
 * 	for ( RLockedServedIt_c it ( g_pLocalIndexes ); it.Next (); )
	{
		auto pIdx = it.Get(); // returns smart pointer to value, addrefed; autorelease on destroy
		if ( !pIdx )
			continue;
		...
		const CSphString &sIndex = it.GetName (); // returns key value
	}
 */

class SCOPED_CAPABILITY RLockedHashIt_c : public ISphNoncopyable
{
public:
	using RefPtr_c = CSphRefcountedPtr<ISphRefcountedMT>;
	explicit RLockedHashIt_c ( const GuardedHash_c * pHash ) ACQUIRE_SHARED ( pHash->m_tIndexesRWLock
																		  , m_pHash->m_tIndexesRWLock )
		: m_pHash ( pHash )
	{ m_pHash->Rlock (); }

	~RLockedHashIt_c () UNLOCK_FUNCTION ()
	{
		m_pHash->Unlock ();
	}

	bool Next () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{ return m_pHash->m_hIndexes.IterateNext ( &m_pIterator ); }

	RefPtr_c Get () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{
		assert ( m_pIterator );
		auto pRes = GuardedHash_c::RefCntHash_t::IterateGet ( &m_pIterator );
		if ( pRes )
			pRes->AddRef ();
		return RefPtr_c ( pRes );
	}

	const CSphString &GetName () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{
		assert ( m_pIterator );
		return GuardedHash_c::RefCntHash_t::IterateGetKey ( &m_pIterator );
	}

protected:
	const GuardedHash_c * m_pHash;
	void * m_pIterator = nullptr;
};

// same as above, but wlocked due to delete() member.
// since it holds exclusive, rlocked iterator will not co-exist.
// also it uses hash's internal iteration to allow deletion (it is ok since it is exclusive).
/* Usage example:
	for ( WLockedHashIt_c it ( &gAgents ); it.Next (); )
	{
		auto pAgent = it.Get (); // returns smart pointer to value, addrefed; autorelease on destroy
		if ( !pAgent )
			continue;
		...
		it.Delete ();	// it is safe. Delete current item, move iterator back to previous.
	}
 */
class SCOPED_CAPABILITY WLockedHashIt_c : public ISphNoncopyable
{
public:
	explicit WLockedHashIt_c ( GuardedHash_c * pHash ) ACQUIRE ( pHash->m_tIndexesRWLock
																  , m_pHash->m_tIndexesRWLock )
		: m_pHash ( pHash )
	{
		m_pHash->Wlock ();
		m_pHash->m_hIndexes.IterateStart ();
	}

	~WLockedHashIt_c () UNLOCK_FUNCTION ()
	{
		m_pHash->Unlock ();
	}

	bool Next () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{ return m_pHash->m_hIndexes.IterateNext (); }

	ISphRefcountedMT * Get () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{
		auto pRes = m_pHash->m_hIndexes.IterateGet ();
		if ( pRes )
			pRes->AddRef ();
		return pRes;
	}

	void Delete () REQUIRES ( m_pHash->m_tIndexesRWLock )
	{
		m_pHash->m_hIndexes.Delete ( m_pHash->m_hIndexes.IterateGetKey () );
	}

	const CSphString &GetName () REQUIRES_SHARED ( m_pHash->m_tIndexesRWLock )
	{
		return m_pHash->m_hIndexes.IterateGetKey ();
	}

protected:
	GuardedHash_c * m_pHash;
};

class SCOPED_CAPABILITY RLockedServedIt_c : public RLockedHashIt_c
{
public:
	explicit RLockedServedIt_c ( const GuardedHash_c * pHash ) ACQUIRE_SHARED ( pHash->IndexesRWLock(), m_pHash->IndexesRWLock() )
		: RLockedHashIt_c ( pHash )
	{}
	~RLockedServedIt_c() UNLOCK_FUNCTION() {}; // d-tr explicitly written because attr UNLOCK_FUNCTION().

	ServedIndexRefPtr_c Get () REQUIRES_SHARED ( m_pHash->IndexesRWLock() )
	{
		auto pServed = ( ServedIndex_c * ) RLockedHashIt_c::Get ().Leak ();
		return ServedIndexRefPtr_c ( pServed );
	}
};

extern GuardedHash_c * g_pLocalIndexes;    // served (local) indexes hash
inline ServedIndexRefPtr_c GetServed ( const CSphString &sName, GuardedHash_c * pHash = g_pLocalIndexes )
{
	return ServedIndexRefPtr_c ( ( ServedIndex_c * ) pHash->Get ( sName ) );
}


ESphAddIndex ConfigureAndPreloadIndex( const CSphConfigSection& hIndex, const char* sIndexName, bool bJson );
ESphAddIndex
AddIndexMT( GuardedHash_c& dPost, const char* szIndexName, const CSphConfigSection& hIndex, bool bReplace = false );
bool PreallocNewIndex( ServedDesc_t& tIdx, const CSphConfigSection* pConfig, const char* szIndexName );

struct AttrUpdateArgs: public CSphAttrUpdateEx
{
	const CSphQuery* m_pQuery = nullptr;
	const ThdDesc_t* m_pThd = nullptr;
	const ServedDesc_t* m_pDesc = nullptr;
	const CSphString* m_pIndexName = nullptr;
	bool m_bJson = false;
};

void HandleMySqlExtendedUpdate( AttrUpdateArgs& tArgs );

/////////////////////////////////////////////////////////////////////////////
// SERVED INDEX DESCRIPTORS STUFF
/////////////////////////////////////////////////////////////////////////////

enum SqlStmt_e
{
	STMT_PARSE_ERROR = 0,
	STMT_DUMMY,

	STMT_SELECT,
	STMT_INSERT,
	STMT_REPLACE,
	STMT_DELETE,
	STMT_SHOW_WARNINGS,
	STMT_SHOW_STATUS,
	STMT_SHOW_META,
	STMT_SET,
	STMT_BEGIN,
	STMT_COMMIT,
	STMT_ROLLBACK,
	STMT_CALL, // check.pl STMT_CALL_SNIPPETS STMT_CALL_KEYWORDS
	STMT_DESCRIBE,
	STMT_SHOW_TABLES,
	STMT_UPDATE,
	STMT_CREATE_FUNCTION,
	STMT_DROP_FUNCTION,
	STMT_ATTACH_INDEX,
	STMT_FLUSH_RTINDEX,
	STMT_FLUSH_RAMCHUNK,
	STMT_SHOW_VARIABLES,
	STMT_TRUNCATE_RTINDEX,
	STMT_SELECT_SYSVAR,
	STMT_SHOW_COLLATION,
	STMT_SHOW_CHARACTER_SET,
	STMT_OPTIMIZE_INDEX,
	STMT_SHOW_AGENT_STATUS,
	STMT_SHOW_INDEX_STATUS,
	STMT_SHOW_PROFILE,
	STMT_ALTER_ADD,
	STMT_ALTER_DROP,
	STMT_SHOW_PLAN,
	STMT_SELECT_DUAL,
	STMT_SHOW_DATABASES,
	STMT_CREATE_PLUGIN,
	STMT_DROP_PLUGIN,
	STMT_SHOW_PLUGINS,
	STMT_SHOW_THREADS,
	STMT_FACET,
	STMT_ALTER_RECONFIGURE,
	STMT_SHOW_INDEX_SETTINGS,
	STMT_FLUSH_INDEX,
	STMT_RELOAD_PLUGINS,
	STMT_RELOAD_INDEX,
	STMT_FLUSH_HOSTNAMES,
	STMT_FLUSH_LOGS,
	STMT_RELOAD_INDEXES,
	STMT_SYSFILTERS,
	STMT_DEBUG,
	STMT_ALTER_KLIST_TARGET,
	STMT_JOIN_CLUSTER,
	STMT_CLUSTER_CREATE,
	STMT_CLUSTER_DELETE,
	STMT_CLUSTER_ALTER_ADD,
	STMT_CLUSTER_ALTER_DROP,
	STMT_CLUSTER_ALTER_UPDATE,

	STMT_TOTAL
};


enum SqlSet_e
{
	SET_LOCAL,
	SET_GLOBAL_UVAR,
	SET_GLOBAL_SVAR,
	SET_INDEX_UVAR,
	SET_CLUSTER_UVAR
};

/// refcounted vector
template < typename T >
class RefcountedVector_c : public CSphVector<T>, public ISphRefcounted
{
};

using AttrValues_p = CSphRefcountedPtr < RefcountedVector_c<SphAttr_t> >;

/// insert value
struct SqlInsert_t
{
	int						m_iType = 0;
	CSphString				m_sVal;		// OPTIMIZE? use char* and point to node?
	int64_t					m_iVal = 0;
	float					m_fVal = 0.0;
	AttrValues_p			m_pVals;
};

/// parsing result
/// one day, we will start subclassing this
struct SqlStmt_t
{
	SqlStmt_e				m_eStmt = STMT_PARSE_ERROR;
	int						m_iRowsAffected = 0;
	const char *			m_sStmt = nullptr; // for error reporting

	// SELECT specific
	CSphQuery				m_tQuery;
	ISphTableFunc *			m_pTableFunc = nullptr;

	CSphString				m_sTableFunc;
	StrVec_t				m_dTableFuncArgs;

	// used by INSERT, DELETE, CALL, DESC, ATTACH, ALTER, RELOAD INDEX
	CSphString				m_sIndex;
	CSphString				m_sCluster;
	bool					m_bClusterUpdateNodes = false;

	// INSERT (and CALL) specific
	CSphVector<SqlInsert_t>	m_dInsertValues; // reused by CALL
	StrVec_t				m_dInsertSchema;
	int						m_iSchemaSz = 0;

	// SET specific
	CSphString				m_sSetName;		// reused by ATTACH
	SqlSet_e				m_eSet = SET_LOCAL;
	int64_t					m_iSetValue = 0;
	CSphString				m_sSetValue;
	CSphVector<SphAttr_t>	m_dSetValues;
//	bool					m_bSetNull = false; // not(yet) used

	// CALL specific
	CSphString				m_sCallProc;
	StrVec_t				m_dCallOptNames;
	CSphVector<SqlInsert_t>	m_dCallOptValues;
	StrVec_t				m_dCallStrings;

	// UPDATE specific
	CSphAttrUpdate			m_tUpdate;
	int						m_iListStart = -1; // < the position of start and end of index's definition in original query.
	int						m_iListEnd = -1;

	// CREATE/DROP FUNCTION, INSTALL PLUGIN specific
	CSphString				m_sUdfName; // FIXME! move to arg1?
	CSphString				m_sUdfLib;
	ESphAttr				m_eUdfType = SPH_ATTR_NONE;

	// ALTER specific
	CSphString				m_sAlterAttr;
	CSphString				m_sAlterOption;
	ESphAttr				m_eAlterColType = SPH_ATTR_NONE;

	// SHOW THREADS specific
	int						m_iThreadsCols = 0;
	CSphString				m_sThreadFormat;

	// generic parameter, different meanings in different statements
	// filter pattern in DESCRIBE, SHOW TABLES / META / VARIABLES
	// target index name in ATTACH
	// token filter options in INSERT
	// plugin type in INSTALL PLUGIN
	// path in RELOAD INDEX
	CSphString				m_sStringParam;

	// generic integer parameter, used in SHOW SETTINGS, default value -1
	int						m_iIntParam = -1;

	bool					m_bJson = false;
	CSphString				m_sEndpoint;

	SqlStmt_t ();
	~SqlStmt_t();

	bool AddSchemaItem ( const char * psName );
	// check if the number of fields which would be inserted is in accordance to the given schema
	bool CheckInsertIntegrity();
};

/// result set aggregated across indexes
struct AggrResult_t : CSphQueryResult
{
	CSphVector<CSphSchema>			m_dSchemas;			///< aggregated result sets schemas (for schema minimization)
	CSphVector<int>					m_dMatchCounts;		///< aggregated result sets lengths (for schema minimization)
	CSphVector<const CSphIndex*>	m_dLockedAttrs;		///< indexes which are hold in the memory until sending result
	StrVec_t						m_dZeroCount;

	void ClampMatches ( int iLimit, bool bCommonSchema );
	void FreeMatchesPtrs ( int iLimit, bool bCommonSchema );
};


class ISphSearchHandler
{
public:
									ISphSearchHandler () {}
	virtual							~ISphSearchHandler() {}
	virtual void					RunQueries () = 0;					///< run all queries, get all results

	virtual void					SetQuery ( int iQuery, const CSphQuery & tQuery, ISphTableFunc * pTableFunc ) = 0;
	virtual void					SetProfile ( CSphQueryProfile * pProfile ) = 0;
	virtual AggrResult_t *			GetResult ( int iResult ) = 0;
};


class CSphSessionAccum
{
public:
	CSphSessionAccum () = default;
	~CSphSessionAccum();

	RtAccum_t * GetAcc ( RtIndex_i * pIndex, CSphString & sError );
	RtIndex_i * GetIndex ();

private:
	RtAccum_t * m_pAcc = nullptr;
};


// from mysqld_error.h
enum MysqlErrors_e
{
	MYSQL_ERR_UNKNOWN_COM_ERROR			= 1047,
	MYSQL_ERR_SERVER_SHUTDOWN			= 1053,
	MYSQL_ERR_PARSE_ERROR				= 1064,
	MYSQL_ERR_FIELD_SPECIFIED_TWICE		= 1110,
	MYSQL_ERR_NO_SUCH_TABLE				= 1146,
	MYSQL_ERR_TOO_MANY_USER_CONNECTIONS	= 1203
};

class SqlRowBuffer_c;

class StmtErrorReporter_i
{
public:
	virtual void Ok ( int iAffectedRows, const CSphString & sWarning, int64_t iLastInsertId ) = 0;
	virtual void Ok ( int iAffectedRows, int nWarnings=0 ) = 0;
	virtual void Error ( const char * sStmt, const char * sError, MysqlErrors_e iErr = MYSQL_ERR_PARSE_ERROR ) = 0;

	virtual SqlRowBuffer_c * GetBuffer() = 0;
};


class QueryParser_i;
class RequestBuilder_i;
class ReplyParser_i;

QueryParser_i * CreateQueryParser ( bool bJson );
RequestBuilder_i * CreateRequestBuilder ( const CSphString & sQuery, const SqlStmt_t & tStmt );
ReplyParser_i * CreateReplyParser ( bool bJson, int & iUpdated, int & iWarnings );

enum ESphHttpStatus
{
	SPH_HTTP_STATUS_200,
	SPH_HTTP_STATUS_206,
	SPH_HTTP_STATUS_400,
	SPH_HTTP_STATUS_500,
	SPH_HTTP_STATUS_501,
	SPH_HTTP_STATUS_503,

	SPH_HTTP_STATUS_TOTAL
};

enum ESphHttpEndpoint
{
	SPH_HTTP_ENDPOINT_INDEX,
	SPH_HTTP_ENDPOINT_SQL,
	SPH_HTTP_ENDPOINT_JSON_SEARCH,
	SPH_HTTP_ENDPOINT_JSON_INDEX,
	SPH_HTTP_ENDPOINT_JSON_CREATE,
	SPH_HTTP_ENDPOINT_JSON_INSERT,
	SPH_HTTP_ENDPOINT_JSON_REPLACE,
	SPH_HTTP_ENDPOINT_JSON_UPDATE,
	SPH_HTTP_ENDPOINT_JSON_DELETE,
	SPH_HTTP_ENDPOINT_JSON_BULK,
	SPH_HTTP_ENDPOINT_PQ,

	SPH_HTTP_ENDPOINT_TOTAL
};


bool CheckCommandVersion ( WORD uVer, WORD uDaemonVersion, CachedOutputBuffer_c & tOut );
ISphSearchHandler * sphCreateSearchHandler ( int iQueries, const QueryParser_i * pQueryParser, QueryType_e eQueryType, bool bMaster, const ThdDesc_t & tThd );
void sphFormatFactors ( StringBuilder_c& dOut, const unsigned int * pFactors, bool bJson );
bool sphParseSqlQuery ( const char * sQuery, int iLen, CSphVector<SqlStmt_t> & dStmt, CSphString & sError, ESphCollation eCollation );
void sphHandleMysqlInsert ( StmtErrorReporter_i & tOut, SqlStmt_t & tStmt, bool bReplace, bool bCommit, CSphString & sWarning, CSphSessionAccum & tAcc, ESphCollation	eCollation, CSphVector<int64_t> & dLastIds );
void sphHandleMysqlUpdate ( StmtErrorReporter_i & tOut, const SqlStmt_t & tStmt, const CSphString & sQuery, CSphString & sWarning, const ThdDesc_t & tThd );
void sphHandleMysqlDelete ( StmtErrorReporter_i & tOut, const SqlStmt_t & tStmt, const CSphString & sQuery, bool bCommit, CSphSessionAccum & tAcc, const ThdDesc_t & tThd );

bool				sphLoopClientHttp ( const BYTE * pRequest, int iRequestLen, CSphVector<BYTE> & dResult, const ThdDesc_t & tThd );
bool				sphProcessHttpQueryNoResponce ( ESphHttpEndpoint eEndpoint, const char * sQuery, const SmallStringHash_T<CSphString> & tOptions, const ThdDesc_t & tThd, CSphVector<BYTE> & dResult );
void				sphHttpErrorReply ( CSphVector<BYTE> & dData, ESphHttpStatus eCode, const char * szError );
ESphHttpEndpoint	sphStrToHttpEndpoint ( const CSphString & sEndpoint );
CSphString			sphHttpEndpointToStr ( ESphHttpEndpoint eEndpoint );

// get tokens from sphinxql
int sphGetTokTypeInt();
int sphGetTokTypeFloat();
int sphGetTokTypeStr();
int sphGetTokTypeConstMVA();


struct PercolateOptions_t
{
	enum MODE
	{
		unknown = 0, sparsed = 1, sharded = 2
	};
	bool m_bGetDocs = false;
	bool m_bVerbose = false;
	bool m_bJsonDocs = true;
	bool m_bGetQuery = false;
	bool m_bSkipBadJson = false; // don't fail whole call if one doc is bad; warn instead.
	int m_iShift = 0;
	MODE m_eMode = unknown;
	CSphString m_sIdAlias;
	CSphString m_sIndex;
};

struct CPqResult; // defined in sphinxpq.h

bool PercolateParseFilters ( const char * sFilters, ESphCollation eCollation, const CSphSchema & tSchema, CSphVector<CSphFilterSettings> & dFilters, CSphVector<FilterTreeItem_t> & dFilterTree, CSphString & sError );
void PercolateMatchDocuments ( const BlobVec_t &dDocs, const PercolateOptions_t &tOpts, CSphSessionAccum &tAcc
							   , CPqResult &tResult );

void SendArray ( const VecTraits_T<CSphString> & dBuf, ISphOutputBuffer & tOut );
void GetArray ( CSphFixedVector<CSphString> & dBuf, InputBuffer_c & tIn );
void SaveArray ( const VecTraits_T<CSphString> & dBuf, MemoryWriter_c & tOut );
void GetArray ( CSphVector<CSphString> & dBuf, MemoryReader_c & tIn );

template <typename T>
void SendArray ( const VecTraits_T<T> & dBuf, ISphOutputBuffer & tOut )
{
	tOut.SendInt ( dBuf.GetLength() );
	if ( dBuf.GetLength() )
		tOut.SendBytes ( dBuf.Begin(), sizeof(dBuf[0]) * dBuf.GetLength() );
}

template<typename T>
void GetArray ( CSphFixedVector<T> & dBuf, InputBuffer_c & tIn )
{
	int iCount = tIn.GetInt();
	if ( !iCount )
		return;

	dBuf.Reset ( iCount );
	tIn.GetBytes ( dBuf.Begin(), dBuf.GetLengthBytes() );
}

template<typename T>
void GetArray ( CSphVector<T> & dBuf, MemoryReader_c & tIn )
{
	int iCount = tIn.GetDword();
	if ( !iCount )
		return;

	dBuf.Resize ( iCount );
	tIn.GetBytes ( dBuf.Begin(), dBuf.GetLengthBytes() );
}

template <typename T>
void SaveArray ( const VecTraits_T<T> & dBuf, MemoryWriter_c & tOut )
{
	tOut.PutDword ( dBuf.GetLength() );
	if ( dBuf.GetLength() )
		tOut.PutBytes ( dBuf.Begin(), sizeof(dBuf[0]) * dBuf.GetLength() );
}

// add handler which will be called on daemon's shutdown right after
// g_bShutdown is set to true. Returns cookie for refer the callback in future.
using Handler_fn = std::function<void ()>;

namespace searchd {

	void* AddShutdownCb ( Handler_fn fnCb );

	// remove previously set shutdown cb by cookie
	void DeleteShutdownCb ( void* pCookie );

	// invoke shutdown handlers
	void FireShutdownCbs ();
}

#endif // _searchdaemon_
