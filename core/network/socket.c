/*©mit**************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
* Copyright 2014-2017 Friend Software Labs AS                                  *
*                                                                              *
* Permission is hereby granted, free of charge, to any person obtaining a copy *
* of this software and associated documentation files (the "Software"), to     *
* deal in the Software without restriction, including without limitation the   *
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or  *
* sell copies of the Software, and to permit persons to whom the Software is   *
* furnished to do so, subject to the following conditions:                     *
*                                                                              *
* The above copyright notice and this permission notice shall be included in   *
* all copies or substantial portions of the Software.                          *
*                                                                              *
* This program is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
* MIT License for more details.                                                *
*                                                                              *
*****************************************************************************©*/

/** @file
 *
 *  Sockets
 *
 * file contain all functitons related network sockets
 *
 *  @author HT
 *  @date created 11/2014
 */

#include <core/types.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <util/log/log.h>
#include <strings.h>

#include "network/socket.h"
#include <system/systembase.h>
#include <pthread.h>

static int ssl_session_ctx_id = 1;
static int ssl_sockopt_on = 1;

/**
 * Open new socket on specified port
 *
 * @param sb pointer to SystemBase
 * @param ssl ctionset to TRUE if you want to setup secured conne
 * @param port number on which connection will be set
 * @param type of connection, for server :SOCKET_TYPE_SERVER, for client: SOCKET_TYPE_CLIENT
 * @return Socket structure when success, otherwise NULL
 */

Socket* SocketOpen( void *sb, FBOOL ssl, unsigned short port, int type )
{
	Socket *sock = NULL;
	int fd = socket( AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0 );
	if( fd == -1 )
	{
		FERROR( "[SOCKET] ERROR socket failed\n" );
		return NULL;
	}
	
	if( type == SOCKET_TYPE_SERVER )
	{
		if( ( sock = (Socket *) FCalloc( 1, sizeof( Socket ) ) ) != NULL )
		{
			sock->port = port;
		}
		else
		{
			//close( fd );
			FERROR("Cannot allocate memory for socket!\n");
			return NULL;
		}
		
		sock->s_SB = sb;
		SystemBase *lsb = (SystemBase *)sb;
		sock->s_Timeouts = 0;
		sock->s_Timeoutu = lsb->sl_SocketTimeout;
		sock->s_Users = 0;
		sock->s_SSLEnabled = ssl;
		
		if( sock->s_SSLEnabled == TRUE )
		{
			sock->s_VerifyClient = TRUE;
			
			INFO("SSL Connection enabled\n");
			
			// Create a SSL_METHOD structure (choose a SSL/TLS protocol version)
			//sock->s_Meth = SSLv3_method();
			sock->s_Meth = SSLv23_server_method();
 
			// Create a SSL_CTX structure 
			sock->s_Ctx = SSL_CTX_new( sock->s_Meth );
 
			if ( sock->s_Ctx == NULL )
			{
				FERROR( "SSLContext error %s\n", (char *)stderr );
				close( fd );
				SocketFree( sock );
				return NULL;
			}
 
			if( sock->s_VerifyClient == TRUE )
			{
				// Load the RSA CA certificate into the SSL_CTX structure 
				if ( !SSL_CTX_load_verify_locations( sock->s_Ctx, lsb->RSA_SERVER_CA_CERT, lsb->RSA_SERVER_CA_PATH )) 
				{
					FERROR( "Could not verify cert CA: %s CA_PATH: %s", lsb->RSA_SERVER_CA_CERT, lsb->RSA_SERVER_CA_PATH );
					close( fd );
					SocketFree( sock );
					return NULL;
				}
				
				// Set to require peer (client) certificate verification 
				SSL_CTX_set_verify( sock->s_Ctx, SSL_VERIFY_NONE, NULL );
				
				// Set the verification depth to 1 
				SSL_CTX_set_verify_depth( sock->s_Ctx ,1);
			}
			
			// Load the server certificate into the SSL_CTX structure 
			if( SSL_CTX_use_certificate_file( sock->s_Ctx, lsb->RSA_SERVER_CERT, SSL_FILETYPE_PEM ) <= 0 ) 
			{
				FERROR("UseCertyficate file fail : %s\n", lsb->RSA_SERVER_CERT );
				SocketFree( sock );
				close( fd );
				return NULL;
			}
 
			// Load the private-key corresponding to the server certificate 
			if( SSL_CTX_use_PrivateKey_file( sock->s_Ctx, lsb->RSA_SERVER_KEY, SSL_FILETYPE_PEM ) <= 0 ) 
			{
				FERROR( "SSLuseprivatekeyfile fail %s\n", (char *)stderr);
				close( fd );
				SocketFree( sock );
				return NULL;
			}
			
 
			// Check if the server certificate and private-key matches
			if( !SSL_CTX_check_private_key( sock->s_Ctx ) ) 
			{
				FERROR("Private key does not match the certificate public key\n");
				close( fd );
				SocketFree( sock );
				return NULL;
			}
			
			// Lets not block and lets allow retries!
			SocketSetBlocking( sock, FALSE );
			
			SSL_CTX_set_mode( sock->s_Ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_AUTO_RETRY );
			SSL_CTX_set_session_cache_mode( sock->s_Ctx, SSL_SESS_CACHE_BOTH ); // for now
			SSL_CTX_set_options( sock->s_Ctx, SSL_OP_NO_SSLv3 | SSL_OP_NO_SSLv2 | SSL_OP_NO_TICKET | SSL_OP_ALL | SSL_OP_NO_COMPRESSION );
		    SSL_CTX_set_session_id_context( sock->s_Ctx, (void *)&ssl_session_ctx_id, sizeof(ssl_session_ctx_id) );
		    SSL_CTX_set_cipher_list( sock->s_Ctx, "HIGH:!aNULL:!MD5:!RC4" );
		}
		
		if( setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, (char*)&ssl_sockopt_on, sizeof(ssl_sockopt_on) ) < 0 )
		{
			FERROR( "[SOCKET] ERROR setsockopt(SO_REUSEADDR) failed\n");
			close( fd );
			SocketFree( sock );
			return NULL;
		}
		
		struct timeval t = { 60, 0 };
		
		if( setsockopt( fd, SOL_SOCKET, SO_SNDTIMEO, ( void *)&t, sizeof( t ) ) < 0 )
		{
			FERROR( "[SOCKET] ERROR setsockopt(SO_SNDTIMEO) failed\n");
			close( fd );
			SocketFree( sock );
			return NULL;
		}
		
		if( setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, ( void *)&t, sizeof( t ) ) < 0 )
		{
			FERROR( "[SOCKET] ERROR setsockopt(SO_RCVTIMEO) failed\n");
			close( fd );
			SocketFree( sock );
			return NULL;
		}

		struct sockaddr_in6 server;
		memset( &server, 0, sizeof( server ) );
		//server.sin6_len = sizeof( server );
		server.sin6_family = AF_INET6;
		server.sin6_addr = in6addr_any;//inet_addr("0.0.0.0");//inet_pton("0.0.0.0");//in6addr_any;

		//server.sin6_addr = inet_addr("0.0.0.0");
		//inet_pton(AF_INET6, "0:0:0:0:0:0:0:0", (struct sockaddr_in6 *) &server.sin6_addr);
		//inet_pton(AF_INET6, "0:0:0:0:0:0:0:215.148.12.10", (struct sockaddr_in6 *) &server.sin6_addr);

		server.sin6_port = ntohs( port );

		if( bind( fd, (struct sockaddr*)&server, sizeof( server ) ) == -1 )
		{
			FERROR( "[SOCKET] ERROR bind failed on port %d\n", port );
			SocketClose( sock );
			return NULL;
		}
		
		sock->fd = fd;
		//SSL_set_fd( sock->s_Ssl, sock->fd );
		
	}
	// CLIENT
	else
	{	// connect to server socket
		if( ( sock = (Socket*) FCalloc( 1, sizeof( Socket ) ) ) != NULL )
		{
			sock->fd = fd;
			sock->port = port;
		}
		else
		{
			SocketClose( sock );
			FERROR("Cannot allocate memory for socket!\n");
			return NULL;
		}
		
		sock->s_SB = sb;
		SystemBase *lsb = (SystemBase *)sb;
		sock->s_Timeouts = 0;
		sock->s_Timeoutu = lsb->sl_SocketTimeout;
		sock->s_Users = 0;
		sock->s_SSLEnabled = ssl;
		
		if( sock->s_SSLEnabled == TRUE )
		{
			OpenSSL_add_all_ciphers();
			OpenSSL_add_all_algorithms();
			
//  ERR_load_BIO_strings();
 // ERR_load_crypto_strings();
//  SSL_load_error_strings();
			
			sock->s_BIO = BIO_new(BIO_s_mem());
			
//			SSL_library_init();
			
			sock->s_Meth = SSLv23_client_method();
			if( sock->s_Meth  == NULL )
			{
				FERROR("Cannot create SSL client method!\n");
				SocketClose( sock );
				return NULL;
			}
 
			// Create a SSL_CTX structure 
			sock->s_Ctx = SSL_CTX_new( sock->s_Meth );
			if( sock->s_Ctx  == NULL )
			{
				FERROR("Cannot create SSL context!\n");
				SocketClose( sock );
				return NULL;
			}
			
			SSL_CTX_set_mode( sock->s_Ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_AUTO_RETRY );
			SSL_CTX_set_session_cache_mode( sock->s_Ctx, SSL_SESS_CACHE_BOTH ); // for now
			SSL_CTX_set_options( sock->s_Ctx, SSL_OP_NO_SSLv3 | SSL_OP_NO_SSLv2 | SSL_OP_NO_TICKET | SSL_OP_ALL | SSL_OP_NO_COMPRESSION );
		    SSL_CTX_set_session_id_context( sock->s_Ctx, (void *)&ssl_session_ctx_id, sizeof(ssl_session_ctx_id) );
		    SSL_CTX_set_cipher_list( sock->s_Ctx, "HIGH:!aNULL:!MD5:!RC4" );
		    
		    // Lets use non blocking sockets
		    SocketSetBlocking( sock, FALSE );
						
			sock->s_Ssl = SSL_new( sock->s_Ctx );
			if( sock->s_Ssl == NULL )
			{
				SocketClose( sock );
				FERROR("Cannot create new SSL connection\n");
				return NULL;
			}
			SSL_set_fd( sock->s_Ssl, sock->fd );
			
			//SSL_CTX_set_session_cache_mode( sock->s_Ctx, SSL_SESS_CACHE_BOTH );
			int cache = SSL_CTX_get_session_cache_mode( sock->s_Ctx );
			INFO("Cache mode set to: ");
			switch( cache )
			{
				case SSL_SESS_CACHE_OFF:
					INFO("off\n");
					break;
				case SSL_SESS_CACHE_CLIENT:
					INFO("client only\n");
					break;
				case SSL_SESS_CACHE_SERVER:
					INFO("server only\n" );
					break;
				case SSL_SESS_CACHE_BOTH:
					INFO("server and client\n");
					break;
				default:
					INFO("undefined\n");
			}
		}
	}

	//DEBUG( "Create mutex\n" );
	pthread_mutex_init( &sock->mutex, NULL );

	return sock;
}

/**
 * Make the socket listen for incoming connections
 *
 * @param socket whitch will listen  incoming connections
 * @return 0 when success, otherwise error number
 */

int SocketListen( Socket *sock )
{
	if( sock == NULL )
	{
		return -1;
	}

	if( listen( sock->fd, SOMAXCONN ) < 0 )
	{
		FERROR( "[SOCKET] ERROR listen failed\n" );
		close( sock->fd );
		return -2;
	}
	sock->listen = TRUE;
	return 0;
}

#include <errno.h>

/**
 * Function load SSL certyficates
 *
 * @param ctx ssl context
 * @param CertFile certyficate file name
 * @param KeyFile certyficate key file name
 * @return 0 when success, otherwise error number
 */

inline int LoadCertificates( SSL_CTX* ctx, char* CertFile, char* KeyFile)
{
 /* set the local certificate from CertFile */
    if ( SSL_CTX_use_certificate_file(ctx, CertFile, SSL_FILETYPE_PEM) <= 0 )
    {
        ERR_print_errors_fp(stderr);
        return 1;
    }
    /* set the private key from KeyFile (may be the same as CertFile) */
    if ( SSL_CTX_use_PrivateKey_file(ctx, KeyFile, SSL_FILETYPE_PEM) <= 0 )
    {
        ERR_print_errors_fp(stderr);
        return 2;
    }
    /* verify private key */
    if ( !SSL_CTX_check_private_key(ctx) )
    {
        DEBUG( "Private key does not match the public certificate\n");
		return 3;
    }
    return 0;
}


#define h_addr h_addr_list[0]

/**
 * Setup connection with another server
 *
 * @param hostname internet address
 * @param service port or service name
 * @param family socket family descriptor
 * @param socktype SOCK_DGRAM or SOCK_STREAM
 * @return socket descriptor
 */

int SocketConnectClient (const char *hostname, const char *service, int family, int socktype)
{
	struct sockaddr_in sin;
	struct hostent *phe;
	struct servent *pse;
	struct protoent *ppe;
	int sockfd;
	char *protocol;
    
	memset(&sin, 0, sizeof(sin));
	
	DEBUG("Connect to %s on port %s\n", hostname, service );

	sin.sin_family=AF_INET;

	switch(socktype)
	{
		case SOCK_DGRAM:
			protocol= "udp";
			break;
		case SOCK_STREAM:
			protocol= "tcp";
		break;
		default:
			FERROR("Listen_server:: unknown socket type=[%d]\n", socktype);
			return -1;
	}


	if ( (pse = (struct servent *)getservbyname(service, protocol) ) != NULL  ) 
	{
		sin.sin_port = pse->s_port;
	}
	else if ((sin.sin_port = htons((short)atoi(service)))==0) 
	{
		FERROR("Connec_client:: could not get service=[%s]\n",service);
		return -1;
	}


	if (!hostname) 
	{
		FERROR("Connect_client:: there should be a hostname!\n");
		return -1;
	}
	else
	{
		if ( (phe = (struct hostent *)gethostbyname(hostname) ) != NULL ) 
		{
			DEBUG("Gethostbyname used\n");
			memcpy(&sin.sin_addr, phe->h_addr, phe->h_length);
		}
		else if( ( phe = gethostbyname2( hostname, AF_INET6 ) ) != NULL )
		{
			DEBUG("Gethostbyname2 used\n");
			memcpy(&sin.sin_addr, phe->h_addr, phe->h_length);
		}
		else if ( (sin.sin_addr.s_addr = inet_addr(hostname)) == INADDR_NONE) 
		{
			FERROR( "Connect_client:: could not get host=[%s]\n", hostname);
			return -1;
		}
	}

	if ((ppe = getprotobyname(protocol)) == 0) 
	{
		FERROR( "Connect_client:: could not get protocol=[%s]\n", protocol);
		return -1;
	}
     
	if ((sockfd = socket(PF_INET, socktype, ppe->p_proto)) < 0) 
	{  
		FERROR( "Connect_client:: could not open socket\n");
		return -1;
	}

	if (connect(sockfd,(struct sockaddr *)&sin, sizeof(sin)) < 0) 
	{
		close( sockfd );
		FERROR( "Connect_client:: could not connect to host=[%s]\n", hostname);
		return -1;
	}
	
	DEBUG("Connection created with host: %s\n", hostname );

	return sockfd;            
}

#define h_addr h_addr_list[0] // for backward compatibility 

/**
 * Setup connection with another server
 *
 * @param sock pointer to socket which will setup connection with another server
 * @param host internet address
 * @return socket descriptor
 */

int SocketConnect( Socket* sock, const char *host )
{
	if( sock == NULL )
	{
		FERROR("[SocketConnect] Socket is NULL..\n");
		return 0;
	}
	
	if( sock->s_SSLEnabled == TRUE )
	{
		SystemBase *lsb = (SystemBase *)sock->s_SB;
		LoadCertificates( sock->s_Ctx, lsb->RSA_SERVER_CERT, lsb->RSA_SERVER_KEY );
	}
	struct addrinfo hints, *res, *p;
	int n;

	bzero( &hints, sizeof(struct addrinfo ) );
	hints.ai_family =AF_UNSPEC;//  AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	//hints.ai_flags  =  AI_NUMERICHOST;
	
	char prt[ 64 ];
	sprintf( prt, "%d", sock->port );
	
	if( ( n = SocketConnectClient( host, prt, AF_UNSPEC, SOCK_STREAM ) ) < 0 )
	{
		FERROR("Cannot setup connection with : %s\n", host );
		return -1;
	}

	//DEBUG("Connected with SSLSocket %d\n", sock->s_SSLEnabled );
	
	if( sock->s_SSLEnabled == TRUE )
	{
		X509                *cert = NULL;
		X509_NAME       *certname = NULL;
		
		SocketSetBlocking( sock, TRUE );
		
		while( TRUE )
		{
			if ( ( n = SSL_connect( sock->s_Ssl ) ) != 1 )
			{
				FERROR("Cannot create SSL connection %d!\n", SSL_get_error( sock->s_Ssl, n ));
				
				
				//if( err <= 0 )
				{
					int error = SSL_get_error( sock->s_Ssl, n );
				
					FERROR( "[SocketAccept] We experienced an error %d.\n", error );
				
					switch( error )
					{
						case SSL_ERROR_NONE:
							// NO error..
							FERROR( "[SocketAccept] No error\n" );
							break;
							//return incoming;
						case SSL_ERROR_ZERO_RETURN:
							FERROR("[SocketAccept] SSL_ACCEPT error: Socket closed.\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							//return NULL;
						case SSL_ERROR_WANT_READ:
							FERROR( "[SocketAccept] Error want read, retrying\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							//return NULL;
						case SSL_ERROR_WANT_WRITE:
							FERROR( "[SocketAccept] Error want write, retrying\n" );
							break;
						case SSL_ERROR_WANT_ACCEPT:
							FERROR( "[SocketAccept] Want accept\n" );
							break;
						case SSL_ERROR_WANT_X509_LOOKUP:
							FERROR( "[SocketAccept] Want 509 lookup\n" );
							break;
						case SSL_ERROR_SYSCALL:
							FERROR( "[SocketAccept] Error syscall!\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							return -2;
						default:
							FERROR( "[SocketAccept] Other error.\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							return -3;
					}
				}
				return -1;
			}
			else
			{
				break;
			}
		}
		
		cert = SSL_get_peer_certificate( sock->s_Ssl );
		if (cert == NULL)
		{
			FERROR( "Error: Could not get a certificate from: \n" );
		}
		else
		{
			DEBUG( "Retrieved the server's certificate from: .\n");
			char *line;
			line  = X509_NAME_oneline( X509_get_subject_name( cert ), 0, 0 );
			DEBUG("%s\n", line );
			free( line );
			line = X509_NAME_oneline( X509_get_issuer_name( cert ), 0, 0 );
			DEBUG("%s\n", line );
			free( line );
			X509_free( cert );
		}
		// ---------------------------------------------------------- *
		// extract various certificate information                    *
		// -----------------------------------------------------------
		//certname = X509_NAME_new( );
		//certname = X509_get_subject_name( cert );

		// ---------------------------------------------------------- *
		// display the cert subject here                              *
		// -----------------------------------------------------------
		//DEBUG( "Displaying certname: %s the certificate subject data:\n", certname );
		//X509_NAME_print_ex(outbio, certname, 0, 0);
		//DEBUG( "\n" );
	}
	
	//DEBUG("Connection set\n");
	
	return 0;
}

/**
 * Setup connection with another server
 *
 * @param sb pointer to SystemBase
 * @param ssl set to TRUE if you want to setup secured conne
 * @param host internet address
 * @param port internet port number
 * @return pointer to new Socket object or NULL when error appear
 */

Socket* SocketConnectHost( void *sb, FBOOL ssl, char *host, unsigned short port )
{
	Socket *sock = NULL;
	SystemBase *lsb = (SystemBase *)SLIB;
	
	struct addrinfo hints, *res, *p;
	int fd;

	bzero( &hints, sizeof(struct addrinfo ) );
	hints.ai_family =AF_UNSPEC;//  AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	//hints.ai_flags  =  AI_NUMERICHOST;
	
	char prt[ 64 ];
	sprintf( prt, "%d", port );
	
	if( ( fd = SocketConnectClient( host, prt, AF_UNSPEC, SOCK_STREAM ) ) < 0 )
	{
		FERROR("Cannot setup connection with : %s\n", host );
		return NULL;
	}
	
	if( ( sock = (Socket*) FCalloc( 1, sizeof( Socket ) ) ) != NULL )
	{
		sock->fd = fd;
		sock->port = port;
	}
	else
	{
		FERROR("Cannot allocate memory for socket!\n");
		return NULL;
	}
		
	sock->s_Timeouts = 0;
	sock->s_Timeoutu = lsb->sl_SocketTimeout;
	sock->s_SSLEnabled = ssl;
		
	if( sock->s_SSLEnabled == TRUE )
	{
		OpenSSL_add_all_ciphers();
		OpenSSL_add_all_algorithms();

		sock->s_BIO = BIO_new(BIO_s_mem());

		sock->s_Meth = SSLv23_client_method();
		if( sock->s_Meth  == NULL )
		{
			FERROR("Cannot create SSL client method!\n");
			SocketClose( sock );
			return NULL;
		}
 
		// Create a SSL_CTX structure 
		sock->s_Ctx = SSL_CTX_new( sock->s_Meth );
		if( sock->s_Ctx  == NULL )
		{
			FERROR("Cannot create SSL context!\n");
			SocketClose( sock );
			return NULL;
		}
			
		SSL_CTX_set_mode( sock->s_Ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_AUTO_RETRY );
		SSL_CTX_set_session_cache_mode( sock->s_Ctx, SSL_SESS_CACHE_BOTH ); // for now
		SSL_CTX_set_options( sock->s_Ctx, SSL_OP_NO_SSLv3 | SSL_OP_NO_SSLv2 | SSL_OP_NO_TICKET | SSL_OP_ALL | SSL_OP_NO_COMPRESSION );
	    SSL_CTX_set_session_id_context( sock->s_Ctx, (void *)&ssl_session_ctx_id, sizeof(ssl_session_ctx_id) );
	    SSL_CTX_set_cipher_list( sock->s_Ctx, "HIGH:!aNULL:!MD5:!RC4" );
		
		sock->s_Ssl = SSL_new( sock->s_Ctx );
		if( sock->s_Ssl == NULL )
		{
			SocketClose( sock );
			FERROR("Cannot create new SSL connection\n");
			return NULL;
		}
		SSL_set_fd( sock->s_Ssl, sock->fd );
			
		//SSL_CTX_set_session_cache_mode( sock->s_Ctx, SSL_SESS_CACHE_BOTH );
		int cache = SSL_CTX_get_session_cache_mode( sock->s_Ctx );
		INFO("Cache mode set to: ");
		switch( cache )
		{
			case SSL_SESS_CACHE_OFF:
				INFO("off\n");
				break;
			case SSL_SESS_CACHE_CLIENT:
				INFO("client only\n");
				break;
			case SSL_SESS_CACHE_SERVER:
				INFO("server only\n" );
				break;
			case SSL_SESS_CACHE_BOTH:
				INFO("server and client\n");
				break;
			default:
				INFO("undefined\n");
		}
		
		DEBUG("Loading certificates\n");
		
		LoadCertificates( sock->s_Ctx, lsb->RSA_SERVER_CERT, lsb->RSA_SERVER_KEY );
			
		X509                *cert = NULL;
		X509_NAME       *certname = NULL;
		
		SocketSetBlocking( sock, FALSE );
		int n=0;
		
		DEBUG("SSLConnect\n");
		
		while( TRUE )
		{
			DEBUG("inside loop\n");
			
			if ( ( n = SSL_connect( sock->s_Ssl ) ) != 1 )
			{
				FERROR("Cannot create SSL connection %d!\n", SSL_get_error( sock->s_Ssl, n ));
				//if( err <= 0 )
				{
					int error = SSL_get_error( sock->s_Ssl, n );
				
					FERROR( "[SocketConnect] We experienced an error %d.\n", error );
				
					switch( error )
					{
						case SSL_ERROR_NONE:
							// NO error..
							FERROR( "[SocketConnect] No error\n" );
							break;
							//return incoming;
						case SSL_ERROR_ZERO_RETURN:
							FERROR("[SocketConnect] SSL_ACCEPT error: Socket closed.\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							//return NULL;
						case SSL_ERROR_WANT_READ:
							FERROR( "[SocketConnect] Error want read, retrying\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							//return NULL;
						case SSL_ERROR_WANT_WRITE:
							FERROR( "[SocketConnect] Error want write, retrying\n" );
							break;
						case SSL_ERROR_WANT_ACCEPT:
							FERROR( "[SocketConnect] Want accept\n" );
							break;
						case SSL_ERROR_WANT_X509_LOOKUP:
							FERROR( "[SocketConnect] Want 509 lookup\n" );
							break;
						case SSL_ERROR_SYSCALL:
							FERROR( "[SocketConnect] Error syscall!\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							return NULL;
						default:
							FERROR( "[SocketConnect] Other error.\n" );
							//shutdown( fd, SHUT_RDWR );
							//close( fd );
							//SSL_free( incoming->s_Ssl );
							//free( incoming );
							return NULL;
					}
				}
				SocketClose( sock );
				return NULL;
			}
			else
			{
				break;
			}
			
		}
		DEBUG("get peer certificates\n");
		
		cert = SSL_get_peer_certificate( sock->s_Ssl );
		if (cert == NULL)
		{
			FERROR( "Error: Could not get a certificate from: \n" );
		}
		else
		{
			DEBUG( "Retrieved the server's certificate from: .\n");
			char *line;
			line  = X509_NAME_oneline( X509_get_subject_name( cert ), 0, 0 );
			DEBUG("%s\n", line );
			free( line );
			line = X509_NAME_oneline( X509_get_issuer_name( cert ), 0, 0 );
			DEBUG("%s\n", line );
			free( line );
			X509_free( cert );
		}
		// ---------------------------------------------------------- *
		// extract various certificate information                    *
		// -----------------------------------------------------------
		//certname = X509_NAME_new( );
		//certname = X509_get_subject_name( cert );

		// ---------------------------------------------------------- *
		// display the cert subject here                              *
		// -----------------------------------------------------------
		//DEBUG( "Displaying certname: %s the certificate subject data:\n", certname );
		//X509_NAME_print_ex(outbio, certname, 0, 0);
		//DEBUG( "\n" );
	}
	DEBUG("Socket returned\n");
	
	return sock;
}

/**
 * Make a socket blocking/non-blocking
 *
 * @param sock pointer to Socket
 * @param block set to TRUE if you want to set provided Socket as blocked
 * @return Returns TRUE on success and FALSE on failure.
 */

int SocketSetBlocking( Socket* sock, FBOOL block )
{
 	int flags, s = 0;
	
	if( sock == NULL )
	{
		FERROR("Cannot set socket as blocking, socket = NULL!!\n");
		return 0;
	}
	
	flags = fcntl( sock->fd, F_GETFL, 0 );
	if( flags < 0 )
	{
		char *errmsg = strerror( errno ); 
		FERROR( "[SOCKET] ERROR fcntl, problem: %s\n", errmsg );
		return 0;
	}
	
	if( !block )
	{
		flags |= O_NONBLOCK;
	}
	else
	{
		flags &= ~O_NONBLOCK;
	}

	if( pthread_mutex_lock( &sock->mutex ) == 0 )
	{
		sock->nonBlocking = !block;
		s = fcntl( sock->fd, F_SETFL, flags );
		pthread_mutex_unlock( &sock->mutex );
	}
	
	if( s < 0 )
	{
		FERROR( "[SocketSetBlocking] ERROR fcntl\n" );
		return 0;
	}
	
	return 1;
}

/**
 * Accepts an incoming connection
 *
 * @param sock pointer to Socket
 * @return Returns a new Socket_t object if the connection was accepted. Returns NULL if the connection was rejected, or an error occured.
 */

Socket* SocketAccept( Socket* sock )
{
	// Don't bother with non-listening sockets
	if( sock == NULL )
	{
		FERROR("[SocketAccept] Cannot accept socket set as NULL\n");
		return NULL;
	}
	
	if( sock->s_SSLEnabled == TRUE )
	{
		if( sock->s_Ctx == NULL )
		{
			FERROR( "[SocketAccept] SSL not properly setup on socket!\n" );
			return NULL;
		}
	}
	
	// Accept
	struct sockaddr_in6 client;
	socklen_t clientLen = sizeof( client );
               
	//DEBUG( "[SocketAccept] Accepting on socket\n" );
	int fd = accept( sock->fd, ( struct sockaddr* )&client, &clientLen );
	//DEBUG( "[SocketAccept] Done accepting file descriptor (%d, %s)  fd %d\n", errno, strerror( errno ), fd );
	
	if( fd == -1 ) 
	{
		// Get some info about failure..
		switch( errno )
		{
			case EAGAIN:
				DEBUG( "[SocketAccept] We have processed all incoming connections OR O_NONBLOCK is set for the socket file descriptor and no connections are present to be accepted.\n" );
				break;
			case EBADF:
				DEBUG( "[SocketAccept] The socket argument is not a valid file descriptor.\n" );
				break;
			case ECONNABORTED:
				DEBUG( "[SocketAccept] A connection has been aborted.\n" );
				break;
			case EINTR:
				DEBUG( "[SocketAccept] The accept() function was interrupted by a signal that was caught before a valid connection arrived.\n" );
				break;
			case EINVAL:
				DEBUG( "[SocketAccept] The socket is not accepting connections.\n" );
				break;
			case ENFILE:
				DEBUG( "[SocketAccept] The maximum number of file descriptors in the system are already open.\n" );
				break;
			case ENOTSOCK:
				DEBUG( "[SocketAccept] The socket argument does not refer to a socket.\n" );
				break;
			case EOPNOTSUPP:
				DEBUG( "[SocketAccept] The socket type of the specified socket does not support accepting connections.\n" );
				break;
			default: 
				DEBUG("[SocketAccept] Accept return bad fd\n");
				break;
		}
		return NULL;
	}

	//DEBUG("Allocate memory for Socket\n");
	Socket* incoming = (Socket*)FCalloc( 1, sizeof( Socket ) );
	if( incoming != NULL )
	{
		incoming->fd = fd;
		incoming->port = ntohs( client.sin6_port );
		incoming->ip = client.sin6_addr;
		incoming->s_SSLEnabled = sock->s_SSLEnabled;
		incoming->s_SB = sock->s_SB;
		
		pthread_mutex_init( &incoming->mutex, NULL );
	}
	else
	{
		FERROR("[SocketAccept] Cannot allocate memory for socket!\n");
		return NULL;
	}
	
	if( sock->s_SSLEnabled == TRUE )
	{
		//DEBUG( "Going into SSL\n" );
		incoming->s_Ssl = SSL_new( sock->s_Ctx ); 
		//DEBUG( "We are into SSL\n" );
		if( incoming->s_Ssl == NULL )
		{
			FERROR("[SocketAccept] Cannot accept SSL connection\n");
			shutdown( fd, SHUT_RDWR );
			close( fd );
			free( incoming );
			return NULL;
		}

		// Make a unique session id here
		/*const unsigned char *unique = FCalloc( 255, sizeof( const unsigned char ) );
		sprintf( unique, "friendcore_%p%d", incoming, rand()%999+rand()%999 );
		SSL_set_session_id_context( sock->s_Ssl, unique, strlen( unique ) );
		FFree( unique );*/
		
		SSL_set_accept_state( incoming->s_Ssl );
		//srl = SSL_set_fd( incoming->s_Ssl, incoming->fd );

		//DEBUG( "Further\n" );
		int srl = SSL_set_fd( incoming->s_Ssl, incoming->fd );
		if( srl != 1 )
		{
			FERROR( "[SocketAccept] Could not set fd\n" );
			shutdown( fd, SHUT_RDWR );
			close( fd );
			SSL_free( incoming->s_Ssl );
			free( incoming );
			return NULL;
		}
		
		int err = 0;
		while( 1 )
		{
			//DEBUG("Mutex locked\n");
			err = SSL_accept( incoming->s_Ssl );
			DEBUG("Connection accepted %d\n", err );
		
			if( err <= 0 )
			{
				int error = SSL_get_error( incoming->s_Ssl, err );
			
				FERROR( "[SocketAccept] We experienced an error %d.\n", error );
			
				switch( error )
				{
					case SSL_ERROR_NONE:
						// NO error..
						FERROR( "[SocketAccept] No error\n" );
						return incoming;
					case SSL_ERROR_ZERO_RETURN:
						FERROR("[SocketAccept] SSL_ACCEPT error: Socket closed.\n" );
						shutdown( fd, SHUT_RDWR );
						close( fd );
						SSL_free( incoming->s_Ssl );
						free( incoming );
						return NULL;
					case SSL_ERROR_WANT_READ:
						FERROR( "[SocketAccept] Error want read, retrying\n" );
						shutdown( fd, SHUT_RDWR );
						close( fd );
						SSL_free( incoming->s_Ssl );
						free( incoming );
						return NULL;
					case SSL_ERROR_WANT_WRITE:
						FERROR( "[SocketAccept] Error want write, retrying\n" );
						break;
					case SSL_ERROR_WANT_ACCEPT:
						FERROR( "[SocketAccept] Want accept\n" );
						break;
					case SSL_ERROR_WANT_X509_LOOKUP:
						FERROR( "[SocketAccept] Want 509 lookup\n" );
						break;
					case SSL_ERROR_SYSCALL:
						FERROR( "[SocketAccept] Error syscall!\n" );
						shutdown( fd, SHUT_RDWR );
						close( fd );
						SSL_free( incoming->s_Ssl );
						free( incoming );
						return NULL;
					default:
						FERROR( "[SocketAccept] Other error.\n" );
						shutdown( fd, SHUT_RDWR );
						close( fd );
						SSL_free( incoming->s_Ssl );
						free( incoming );
						return NULL;
				}
			}
			else
			{
				//DEBUG("SSLconnection is ok, return\n");
				break;
			}
		}
	}

	DEBUG( "[SocketAccept] Accepting incoming!\n" );
	return incoming;
}

/**
 * Accepts an incoming connection
 *
 * @param sock pointer to Socket
 * @param p AcceptPair structure
 * @return Returns a new Socket_t object if the connection was accepted. Returns NULL if the connection was rejected, or an error occured.
 */

Socket* SocketAcceptPair( Socket* sock, struct AcceptPair *p )
{
	//DEBUG("Socket accept pair\n");
	// Don't bother with non-listening sockets
	if( sock == NULL )
	{
		DEBUG("sock = NULL\n");
		FERROR("[SocketAcceptPair] Cannot accept socket set as NULL\n");
		return NULL;
	}

	//DEBUG("Socket %d\n", sock->s_SSLEnabled );
	
	if( sock->s_SSLEnabled )
	{
		if( !sock->s_Ctx )
		{
			FERROR( "[SocketAcceptPair] SSL not properly setup on socket!\n" );
			return NULL;
		}
	}
	else
	{
		//DEBUG("Socket not secured!\n");
	}
	
	int fd = p->fd;
	int retries = 0, srl = 0;
	
	//DEBUG( "[SocketAcceptPair] Creating incoming socket.\n" );
	Socket* incoming = ( Socket *)FCalloc( 1, sizeof( Socket ) );
	//DEBUG("Allocate memory for incomming socket %p\n", incoming );
	if( incoming != NULL )
	{
		incoming->fd = fd;
		incoming->port = ntohs( p->client.sin6_port );
		incoming->ip = p->client.sin6_addr;
		incoming->s_SSLEnabled = sock->s_SSLEnabled;
		incoming->s_SB = sock->s_SB;
		
		// Not blocking
		SocketSetBlocking( incoming, FALSE );
		
		pthread_mutex_init( &incoming->mutex, NULL );
		//DEBUG("Incoming socket set\n");
	}
	else
	{
		FERROR("[SocketAcceptPair] Cannot allocate memory for socket!\n");
		return NULL;
	}
	//DEBUG("After checking incomming socket %d\n", sock->s_SSLEnabled );
	
	if( sock->s_SSLEnabled == TRUE )
	{
		//DEBUG( "[SocketAcceptPair] Creating s_Ssl on incoming.\n" );
		incoming->s_Ssl = SSL_new( sock->s_Ctx );
		 
		if( incoming->s_Ssl == NULL )
		{
			FERROR("[SocketAcceptPair] Cannot accept SSL connection\n");
			shutdown( fd, SHUT_RDWR );
			close( fd );
			pthread_mutex_destroy( &incoming->mutex );
			free( incoming );
			return NULL;
		}

		//DEBUG( "[SocketAcceptPair] Setting accept state.\n" );
		srl = SSL_set_fd( incoming->s_Ssl, incoming->fd );
		SSL_set_accept_state( incoming->s_Ssl );
	
		//DEBUG("Accept state on fd %d\n", fd );
	
		if( srl != 1 )
		{
			int error = SSL_get_error( incoming->s_Ssl, srl );

			FERROR( "[SocketAcceptPair] Could not set fd, error: %d fd: %d\n", error, incoming->fd );
			shutdown( fd, SHUT_RDWR );
			close( fd );
			pthread_mutex_destroy( &incoming->mutex );
			SSL_free( incoming->s_Ssl );
			free( incoming );
			return NULL;
		}
		
		//DEBUG("Before loop\n");

		 // setup SSL session 
		int err = 0;
		int retries = 0;
		while( 1 )
		{
			//DEBUG( "[SocketAcceptPair] Going into SSL_accept..\n" );
			
			
			if( ( err = SSL_accept( incoming->s_Ssl ) ) == 1 )
			{
				//DEBUG("Connection accepted %p\n", incoming );
				break;
			}

			//DEBUG( "[SocketAcceptPair] SSL_accept..\n" );
			if( err <= 0 || err == 2 )
			{
				int error = SSL_get_error( incoming->s_Ssl, err );
		
				//DEBUG("Error %d\n", error );
	
				switch( error )
				{
					case SSL_ERROR_NONE:
						// NO error..
						FERROR( "[SocketAcceptPair] No error\n" );
						return incoming;
					case SSL_ERROR_ZERO_RETURN:
						FERROR("[SocketAcceptPair] SSL_ACCEPT error: Socket closed.\n" );
						SocketClose( incoming );
						return NULL;
					case SSL_ERROR_WANT_READ:
						//FERROR( "[SocketAcceptPair] Error want read.\n" );
						/*
						if( retries++ > 10000 )
						{
							SocketClose( incoming );
							return NULL;
						}
						usleep( 50 );
						*/
						return incoming;
					case SSL_ERROR_WANT_WRITE:
						//FERROR( "[SocketAcceptPair] Error want write, retrying\n" );
						/*
						if( retries++ > 10000 )
						{
							SocketClose( incoming );
							return NULL;
						}
						usleep( 50 );
						*/
						return incoming;
					case SSL_ERROR_WANT_ACCEPT:
						FERROR( "[SocketAcceptPair] Want accept\n" );
						SocketClose( incoming );
						return NULL;
					case SSL_ERROR_WANT_X509_LOOKUP:
						FERROR( "[SocketAcceptPair] Want 509 lookup\n" );
						SocketClose( incoming );
						return NULL;
					case SSL_ERROR_SYSCALL:
						FERROR( "[SocketAcceptPair] Error syscall.\n" ); //. Goodbye! %s.\n", ERR_error_string( ERR_get_error(), NULL ) );
						SocketClose( incoming );
						return NULL;
					case SSL_ERROR_SSL:
						FERROR( "[SocketAcceptPair] SSL_ERROR_SSL: %s.\n", ERR_error_string( ERR_get_error(), NULL ) );
						SocketClose( incoming );
						return NULL;
				}
			}
			usleep( 0 );
		}
		//DEBUG("Before end, fd %d\n", fd );
	}

	//DEBUG( "[SocketAcceptPair] Accepting incoming.\n" );
	
	return incoming;
}

/**
 * Read data from socket
 *
 * @param sock pointer to Socket on which read function will be called
 * @param data pointer to char table where data will be stored
 * @param length size of char table
 * @param pass (obsolete?)
 * @return number of bytes readed from socket
 */

int SocketRead( Socket* sock, char* data, unsigned int length, unsigned int pass )
{
	if( sock == NULL )
	{
		FERROR("Cannot read from socket, socket = NULL!\n");
		return 0;
	}
	
	if( data == NULL )
	{
		FERROR( "Can not read into empty buffer.\n" );
		return 0;
	}

	if( sock->s_SSLEnabled == TRUE )
	{
		if( !sock->s_Ssl )
		{
			FERROR( "Problem with SSL!\n" );
			return 0;
		}
		unsigned int read = 0;
		int res = 0, err = 0, buf = length;
		fd_set rd_set, wr_set;
		int retries = 0;
		int read_retries = 0;
		struct timeval timeout;
		fd_set fds;
		do
		{
			pthread_yield();
			
			if( read + buf > length ) buf = length - read;
			
			if( ( res = SSL_read( sock->s_Ssl, data + read, buf ) ) > 0 )
			{
#ifndef NO_VALGRIND_STUFF	
				VALGRIND_MAKE_MEM_DEFINED( data + read, res );
#endif
				read += res;
				read_retries = retries = 0;
				
				// After reading 100kb we will start sleeping, we're probably
				// uploading, and we do not want denial of service..
				if( read > 100000 )
				{
					usleep( 500 );
				}
			}
			else
			{
				err = SSL_get_error( sock->s_Ssl, res );
				
				switch( err )
				{
					// The TLS/SSL I/O operation completed. 
					case SSL_ERROR_NONE:
						FERROR( "[SocketRead] Completed successfully.\n" );
						return read;
					// The TLS/SSL connection has been closed. Goodbye!
					case SSL_ERROR_ZERO_RETURN:
						FERROR( "[SocketRead] The connection was closed.\n" );
						return SOCKET_CLOSED_STATE;
					// The operation did not complete. Call again.
					case SSL_ERROR_WANT_READ:
						// Retires happen because of bad connection
						if( read == 0 && read_retries++ < 400 ) 
						{
							usleep( 50000 );
							FERROR( "[SocketRead] Want read, trying a retry (%d/400)\n", read_retries );
							continue;
						}
						DEBUG("[SocketRead] Want read TIMEOUT....\n");
						return read;
					// The operation did not complete. Call again.
					case SSL_ERROR_WANT_WRITE:
						if( pthread_mutex_lock( &sock->mutex ) == 0 )
						{
							FERROR( "[SocketRead] Want write.\n" );
							FD_ZERO( &fds );
							FD_SET( sock->fd, &fds );
							
							pthread_mutex_unlock( &sock->mutex );
						}
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
						
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						
						if( err > 0 )
						{
							usleep( 50000 );
							FERROR("[SocketRead] want write\n");
							continue; // more data to read...
						}
						else if( err == 0 ) 
						{
							FERROR("[SocketRead] want write TIMEOUT....\n");
							return read;
						}
						FERROR("[SocketRead] want write everything read....\n");
						return read;
					case SSL_ERROR_SYSCALL:
						FERROR("[SocketRead] Error syscall, bufsize = %d.\n", buf );	
						if( err > 0 )
						{
							if( errno == 0 )
							{
							    FERROR(" [SocketRead] Connection reset by peer.\n" );
							    return SOCKET_CLOSED_STATE;
							}
							else FERROR( "[SocketRead] Error syscall error: %s\n", strerror( errno ) );
						}
						else if( err == 0 )
						{
							FERROR( "[SocketRead] Error syscall no error? return.\n" );
							return read;
						}
						FERROR( "[SocketRead] Error syscall other error. return.\n" );
						return read;
					// Don't retry, just return read
					default:
						return read;
				}
			}
		}
		while( read < length );
		
		//INFO( "[SocketRead] Done reading (%d bytes of %d ).\n", read, length );
		return read;
	}
	// Read in a non-SSL way
	else
	{
		unsigned int bufLength = length, read = 0;
		int retries = 0, res = 0;
	    
		while( 1 )
		{			
			res = recv( sock->fd, data + read, bufLength - read, MSG_DONTWAIT );
			if( res > 0 )
			{ 
				read += res;
				retries = 0;
				//if( read >= length )
				{
					DEBUG( "[SocketRead] Done reading %d/%d\n", read, length );
					return read;
				}
			}
			else if( res == 0 ) return read;
			// Error
			else if( res < 0 )
			{
				// Resource temporarily unavailable...
				//if( errno == EAGAIN )
				if( read == 0 )
				{
					if( errno == EAGAIN && retries++ < 25 )
					{
						// Approx successful header
						usleep( 50000 );
						FERROR( "[SocketRead] Resource temporarily unavailable.. Read %d/%d (retries %d)\n", read, length, retries );
						continue;
					}
					else
					{
						break;
					}
				}
				else
				{
					DEBUG( "[SocketRead] Read %d/%d\n", read, length );
					return SOCKET_CLOSED_STATE;
				}
			}
			DEBUG( "[SocketRead] Read %d/%d\n", read, length );
		}
		// DEBUG( "[SocketRead] Done reading %d/%d (errno: %d)\n", read, length, errno );
		return read;
	}
	return 0;
}

/**
 * Read data from socket with timeout option
 *
 * @param sock pointer to Socket on which read function will be called
 * @param data pointer to char table where data will be stored
 * @param length size of char table
 * @param pass (obsolete?)
 * @param sec number of timeout seconds
 * @return number of bytes readed from socket
 */

int SocketWaitRead( Socket* sock, char* data, unsigned int length, unsigned int pass, int sec )
{
	if( sock == NULL )
	{
		FERROR("[SocketWaitRead] Cannot read from socket, socket = NULL!\n");
		return 0;
	}
	
	DEBUG2("[SocketWaitRead] Socket wait for message\n");
	
	int n;
	fd_set wset, rset;
	struct timeval tv;
	/*
	 FD_ZERO( &(app->readfd) );
			FD_ZERO( &(app->writefd) );
			FD_SET( app->infd[0] , &(app->readfd) );
			FD_SET( app->outfd[1] , &(app->writefd) );
	 */
		
	if( pthread_mutex_lock( &sock->mutex ) == 0 )
	{
		FD_ZERO( &rset );
		//FD_SET( 0,  &rset );
		FD_SET( sock->fd,  &rset );
		//wset = rset;
		
		pthread_mutex_unlock( &sock->mutex );
		
	}
	
	SocketSetBlocking( sock, TRUE );
		
	tv.tv_sec = sec;
	tv.tv_usec = 0;
		
	if( ( n = select( sock->fd+1, &rset, NULL, NULL, &tv ) ) == 0 )
	{
		FERROR("[SocketWaitRead] Connection timeout\n");
		SocketSetBlocking( sock, FALSE );
		return 0;
		
	}
	else if( n < 0 )
	{
		FERROR("[SocketWaitRead] Select error\n");
	}
	
	SocketSetBlocking( sock, FALSE );
	
	DEBUG2("[SocketWaitRead] Socket message appear %d\n", n);

	if( sock->s_SSLEnabled == TRUE )
	{
		unsigned int read = 0;
		int res = 0, err = 0, buf = length;
		fd_set rd_set, wr_set;
		int retries = 0;
		
		do
		{
			pthread_yield();
			
			INFO( "[SocketWaitRead] Start of the voyage.. %p\n", sock );
			
			if( read + buf > length ) buf = length - read;
			
			if( ( res = SSL_read( sock->s_Ssl, data + read, buf ) ) >= 0 )
			{
				read += res;
				
				FULONG *rdat = (FULONG *)data;
				if( ID_FCRE == rdat[ 0 ] )
				{
					//printf("\n\n\n[SocketWaitRead] ---------%ld--\n\n\n", rdat[ 1 ] );
					if( read >= rdat[ 1 ] )
					{
						return read;
					}
				}
				else
				{

					return res;
				}
			}
				
			/*
			else
			{
				//DEBUG( "[SocketWaitRead] Could not read mutex!\n" );
				return 0;
			}*/
			
			struct timeval timeout;
			fd_set fds;
			
			if( res <= 0 )
			{
				err = SSL_get_error( sock->s_Ssl, res );
				switch( err )
				{
					// The TLS/SSL I/O operation completed. 
					case SSL_ERROR_NONE:
						FERROR( "[SocketWaitRead] Completed successfully.\n" );
						return read;
					// The TLS/SSL connection has been closed. Goodbye!
					case SSL_ERROR_ZERO_RETURN:
						FERROR( "[SocketWaitRead] The connection was closed, return %d\n", read );
						return SOCKET_CLOSED_STATE;
					// The operation did not complete. Call again.
					case SSL_ERROR_WANT_READ:
						// no data available right now, wait a few seconds in case new data arrives...
						//printf("SSL_ERROR_WANT_READ %i\n", count);
						
						if( pthread_mutex_lock( &sock->mutex ) == 0 )
						{
							FD_ZERO( &fds );
							FD_SET( sock->fd, &fds );
							
							pthread_mutex_unlock( &sock->mutex );
						}
						
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
						
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							continue; // more data to read...
						}
						
						if( err == 0 ) 
						{
							FERROR("[SocketWaitRead] want read TIMEOUT....\n");
							return read;
						}
						else 
						{
							FERROR("[SocketWaitRead] want read everything read....\n");
							return read;
						}
						
						//if( read > 0 ) return read;
						//usleep( 0 );
						FERROR("want read\n");
						continue;
					// The operation did not complete. Call again.
					case SSL_ERROR_WANT_WRITE:
						FERROR( "[SocketWaitRead] Want write.\n" );
						
						if( pthread_mutex_lock( &sock->mutex ) == 0 )
						{
							FD_ZERO( &fds );
							FD_SET( sock->fd, &fds );
							
							pthread_mutex_unlock( &sock->mutex );
						}
						
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
						
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							continue; // more data to read...
						}
						
						if( err == 0 ) 
						{
							FERROR("[SocketWaitRead] want read TIMEOUT....\n");
							return read;
						}
						else 
						{
							FERROR("[SocketWaitRead] want read everything read....\n");
							return read;
						}
						//return read;
					case SSL_ERROR_SYSCALL:
						return read;
					default:
						//return read;
						usleep( 0 );
						if( retries++ > 500 ) 
						{
							return read;
						}
						continue;
				}
			}
		}
		while( read < length );
		
		//INFO( "[SocketRead] Done reading (%d bytes of %d ).\n", read, length );
		return read;
	}
	// Read in a non-SSL way
	else
	{
		unsigned int bufLength = length, read = 0;
		int retries = 0, res = 0;
	    
		while( 1 )
		{
			res = recv( sock->fd, data + read, bufLength - read, MSG_DONTWAIT );
			
			if( res > 0 )
			{ 
				read += res;
				retries = 0;
				//if( read >= length )
				{
					DEBUG( "[SocketWaitRead] Done reading %d/%d\n", read, length );
					return read;
				}
			}
			else if( res == 0 ) return read;
			// Error
			else if( res < 0 )
			{
				// Resource temporarily unavailable...
				//if( errno == EAGAIN )
				if( errno == EAGAIN && retries++ < 25 )
				{
					// Approx successful header
					usleep( 0 );
					FERROR( "[SocketWaitRead] Resource temporarily unavailable.. Read %d/%d (retries %d)\n", read, length, retries );
					continue;
				}
				DEBUG( "[SocketWaitRead] Read %d/%d\n", read, length );
				return read;
			}
			DEBUG( "[SocketWaitRead] Read %d/%d\n", read, length );
		}
	    DEBUG( "[SocketWaitRead] Done reading %d/%d (errno: %d)\n", read, length, errno );
		return read;
		
	}
}

/**
 * Read DataForm package from socket
 *
 * @param sock pointer to Socket on which read function will be called
 * @return BufString structure
 */

BufString *SocketReadPackage( Socket *sock )
{
	BufString *bs = BufStringNew();
	int locbuffersize = 8192;
	char locbuffer[ locbuffersize ];
	int fullPackageSize = 0;
	unsigned int read = 0;
	
	DEBUG2("[SocketReadPackage] Socket message appear , sock ptr %p\n", sock );
	
	if( sock->s_SSLEnabled == TRUE )
	{
		unsigned int read = 0;
		int res = 0, err = 0;//, buf = length;
		fd_set rd_set, wr_set;
		int retries = 0;
		
		do
		{
			pthread_yield();
			
			INFO( "[SocketReadPackage] Start of the voyage.. %p\n", sock );
			if( pthread_mutex_lock( &sock->mutex ) == 0 )
			{
				//if( read + buf > length ) buf = length - read;
				if( ( res = SSL_read( sock->s_Ssl, locbuffer, locbuffersize ) ) >= 0 )
				{
					read += (unsigned int)res;
				
					FULONG *rdat = (FULONG *)locbuffer;
					if( ID_FCRE == rdat[ 0 ] )
					{
						fullPackageSize = rdat[ 1 ];
					}
				
					BufStringAddSize( bs, locbuffer, res );
				
					if( fullPackageSize > 0 && read >= (unsigned int) fullPackageSize )
					{
						pthread_mutex_unlock( &sock->mutex );	
						return bs;
					}
				}
				pthread_mutex_unlock( &sock->mutex );	
			}
		
			struct timeval timeout;
			fd_set fds;
		
			if( res <= 0 )
			{
				err = SSL_get_error( sock->s_Ssl, res );
				switch( err )
				{
					// The TLS/SSL I/O operation completed. 
					case SSL_ERROR_NONE:
						FERROR( "[SocketReadPackage] Completed successfully.\n" );
						return bs;
						// The TLS/SSL connection has been closed. Goodbye!
					case SSL_ERROR_ZERO_RETURN:
						FERROR( "[SocketReadPackage] The connection was closed, return %d\n", read );
						return bs;
						// The operation did not complete. Call again.
					case SSL_ERROR_WANT_READ:
						// no data available right now, wait a few seconds in case new data arrives...
						//printf("SSL_ERROR_WANT_READ %i\n", count);
					
						FD_ZERO( &fds );
						FD_SET( sock->fd, &fds );
					
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
					
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							return NULL; // more data to read...
						}
					
						if( err == 0 ) 
						{
							FERROR("[SocketReadPackage] want read TIMEOUT....\n");
							return bs;
						}
						else 
						{
							FERROR("[SocketReadPackage] want read everything read....\n");
							return bs;
						}
					
						FERROR("want read\n");
						return NULL;
						// The operation did not complete. Call again.
					case SSL_ERROR_WANT_WRITE:
						FERROR( "[SocketReadPackage] Want write.\n" );
						FD_ZERO( &fds );
						FD_SET( sock->fd, &fds );
					
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
					
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							return NULL; // more data to read...
						}
					
						if( err == 0 ) 
						{
							FERROR("[SocketReadPackage] want read TIMEOUT....\n");
							return bs;
						}
						else 
						{
							FERROR("[SocketReadPackage] want read everything read....\n");
							return bs;
						}
						//return read;
					case SSL_ERROR_SYSCALL:
						return bs;
					default:
					
						usleep( 0 );
						if( retries++ > 500 ) 
						{
							return bs;
						}
						return NULL;
				}
			}
		}
		while( TRUE );
		DEBUG("[SocketReadPackage]  readed\n");
		
		return bs;
	}
	
	//
	// Read in a non-SSL way
	//
	
	else
	{
		int retries = 0, res = 0;
		DEBUG("[SocketReadPackage] nonSSL read\n");
		
		while( 1 )
		{
			res = recv( sock->fd, locbuffer, locbuffersize, MSG_DONTWAIT );
			
			if( res > 0 )
			{ 
				read += res;
				retries = 0;
				
				FULONG *rdat = (FULONG *)locbuffer;
				if( ID_FCRE == rdat[ 0 ] )
				{
					fullPackageSize = rdat[ 1 ];
					DEBUG("[SocketReadPackage] package size %lu\n", fullPackageSize );
				}
				
				BufStringAddSize( bs, locbuffer, res );
				
				DEBUG("readed %d package %d curr read %d\n", read, (int)fullPackageSize, res );
				
				if( fullPackageSize > 0 && read >= (unsigned int)fullPackageSize )
				{
					DEBUG("[SocketReadPackage] got full package\n");
					
					return bs;
				}
			}
			else if( res == 0 )
			{
				DEBUG("Timeout\n");
				return bs;
			}
			// Error
			else if( res < 0 )
			{
				if( errno == EAGAIN && retries++ < 25 )
				{
					// Approx successful header
					//usleep( 0 );
					FERROR( "[SocketReadPackage] Resource temporarily unavailable.. Read %d/ (retries %d)\n", read, retries );
					//return NULL;
					continue;
				}
				DEBUG( "[SocketReadPackage] Read %d  res < 0/\n", read );
				return bs;
			}
			DEBUG( "[SocketReadPackage] Read %d/\n", read );
			pthread_yield();
		}
		
		DEBUG( "[SocketReadPackage] Done reading %d/ (errno: %d)\n", read, errno );

	}
	return bs;
}

/**
 * Read data from socket till end of stream
 *
 * @param sock pointer to Socket on which read function will be called
 * @param pass (obsolete?)
 * @param sec timeout value in seconds
 * @return BufString structure
 */

BufString *SocketReadTillEnd( Socket* sock, unsigned int pass, int sec )
{
	if( sock == NULL )
	{
		FERROR("[SocketReadTillEnd] Cannot read from socket, socket = NULL!\n");
		return NULL;
	}
	
	DEBUG2("[SocketReadTillEnd] Socket wait for message\n");
	
	int n;
	fd_set wset, rset;
	struct timeval tv;
	/*
	 FD_ZERO( &(app->readfd) );
			FD_ZERO( &(app->writefd) );
			FD_SET( app->infd[0] , &(app->readfd) );
			FD_SET( app->outfd[1] , &(app->writefd) );
	 */
	
	if( pthread_mutex_lock( &sock->mutex ) == 0 )
	{
		FD_ZERO( &rset );
		//FD_SET( 0,  &rset );
		FD_SET( sock->fd,  &rset );
		//wset = rset;
		
		pthread_mutex_unlock( &sock->mutex );
	}
	
	SocketSetBlocking( sock, TRUE );
		
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	FBOOL quit = FALSE;
	int locbuffersize = 8192;
	char locbuffer[ locbuffersize ];
	int fullPackageSize = 0;
	unsigned int read = 0;
	
	BufString *bs = BufStringNew();
	
	while( quit != TRUE )
	{
		pthread_yield();
		
		if( ( n = select( sock->fd+1, &rset, NULL, NULL, &tv ) ) == 0 )
		{
			FERROR("[SocketReadTillEnd] Connection timeout\n");
			SocketSetBlocking( sock, FALSE );
			//BufStringDelete( bs );
			return NULL;
		
		}
		else if( n < 0 )
		{
			FERROR("[SocketReadTillEnd] Select error\n");
		}
	
		SocketSetBlocking( sock, FALSE );
	
		if( sock->s_SSLEnabled == TRUE )
		{
			unsigned int read = 0;
			int res = 0, err = 0;//, buf = length;
			fd_set rd_set, wr_set;
			int retries = 0;
			
			//do
			//{
			INFO( "[SocketReadTillEnd] Start of the voyage.. %p\n", sock );

			//if( read + buf > length ) buf = length - read;
			if( ( res = SSL_read( sock->s_Ssl, locbuffer, locbuffersize ) ) >= 0 )
			{
				read += (unsigned int)res;

				FULONG *rdat = (FULONG *)locbuffer;
				if( ID_FCRE == rdat[ 0 ] )
				{
					fullPackageSize = rdat[ 1 ];
				}
				BufStringAddSize( bs, locbuffer, res );

				if( fullPackageSize > 0 && read >= (unsigned int) fullPackageSize )
				{
					return bs;
				}
			}
			//pthread_mutex_unlock( &sock->mutex );	
			//}
			
			struct timeval timeout;
			fd_set fds;
			
			if( res <= 0 )
			{
				err = SSL_get_error( sock->s_Ssl, res );
				switch( err )
				{
					err = SSL_get_error( sock->s_Ssl, res );
					
					switch( err )
					{
						// The TLS/SSL I/O operation completed. 
						case SSL_ERROR_NONE:
							FERROR( "[SocketReadTillEnd] Completed successfully.\n" );
							return bs;
						// The TLS/SSL connection has been closed. Goodbye!
						case SSL_ERROR_ZERO_RETURN:
							FERROR( "[SocketReadTillEnd] The connection was closed, return %d\n", read );
							return bs;
							// The operation did not complete. Call again.
						case SSL_ERROR_WANT_READ:
						// no data available right now, wait a few seconds in case new data arrives...
						//printf("SSL_ERROR_WANT_READ %i\n", count);

						if( pthread_mutex_lock( &sock->mutex ) == 0 )
						{
							FD_ZERO( &fds );
							FD_SET( sock->fd, &fds );

							pthread_mutex_unlock( &sock->mutex );
						}

						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;

						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							return NULL; // more data to read...
						}
						
						if( err == 0 ) 
						{
							FERROR("[SocketReadTillEnd] want read TIMEOUT....\n");
							return bs;
						}
						else 
						{
							FERROR("[SocketReadTillEnd] want read everything read....\n");
							return bs;
						}
						
						FERROR("want read\n");
						return NULL;
						// The operation did not complete. Call again.

						case SSL_ERROR_WANT_WRITE:
							FERROR( "[SocketReadTillEnd] Want write.\n" );
							
						if( pthread_mutex_lock( &sock->mutex ) == 0 )
						{
							FD_ZERO( &fds );
							FD_SET( sock->fd, &fds );
							
							pthread_mutex_unlock( &sock->mutex );
						}
						
						timeout.tv_sec = sock->s_Timeouts;
						timeout.tv_usec = sock->s_Timeoutu;
						
						err = select( sock->fd+1, &fds, NULL, NULL, &timeout );
						if( err > 0 )
						{
							return NULL; // more data to read...
						}
						
						if( err == 0 ) 
						{
							FERROR("[SocketReadTillEnd] want read TIMEOUT....\n");
							return bs;
						}
						else 
						{
							FERROR("[SocketReadTillEnd] want read everything read....\n");
							return bs;
						}
						//return read;
					case SSL_ERROR_SYSCALL:
						return bs;
					default:
						
						usleep( 0 );
						if( retries++ > 500 ) 
						{
							return bs;
						}
						return NULL;
				}
			}
			//while( read < (unsigned int)fullPackageSize );
			DEBUG("[SocketReadTillEnd]  readed\n");
			
			return bs;
		}
	}
		
		//
		// Read in a non-SSL way
		//
		
		else
		{
			int retries = 0, res = 0;
			DEBUG("[SocketReadTillEnd] nonSSL read\n");
			
			//while( 1 )
			{
				res = recv( sock->fd, locbuffer, locbuffersize, MSG_DONTWAIT );
				
				if( res > 0 )
				{
					read += res;
					retries = 0;
					
					FULONG *rdat = (FULONG *)locbuffer;
					if( ID_FCRE == rdat[ 0 ] )
					{
						fullPackageSize = rdat[ 1 ];
						DEBUG("[SocketReadTillEnd] package size %lu\n", fullPackageSize );
					}
					
					BufStringAddSize( bs, locbuffer, res );
					
					DEBUG("readed %d package %d curr read %d\n", read, (int)fullPackageSize, res );
					
					if( fullPackageSize > 0 && read >= (unsigned int)fullPackageSize )
					{
						DEBUG("[SocketReadTillEnd] got full package\n");
						/*
						 *			int j=0;
						 *			printf("received from socket----\n");
						 *			char *t = (char *)bs->bs_Buffer;
						 *			for( j; j < (int)bs->bs_Size ; j++ )
						 *			{
						 *				if( t[j] >= 'A' && t[j] <= 'Z' )
						 *				{
						 *					printf(" %c ", t[j] );
					}
					else
					{
					printf(" _ " );
					}
					}
					printf("\n");*/
						
						return bs;
					}
				}
				else if( res == 0 )
				{
					DEBUG("Timeout\n");
					return bs;
				}
				// Error
				else if( res < 0 )
				{
					if( errno == EAGAIN && retries++ < 25 )
					{
						// Approx successful header
						//usleep( 0 );
						FERROR( "[SocketReadTillEnd] Resource temporarily unavailable.. Read %d/ (retries %d)\n", read, retries );
						//return NULL;
						continue;
					}
					DEBUG( "[SocketReadTillEnd] Read %d  res < 0/\n", read );
					return bs;
				}
				DEBUG( "[SocketReadTillEnd] Read %d/\n", read );
			}
			
			DEBUG( "[SocketReadTillEnd] Done reading %d/ (errno: %d)\n", read, errno );
			
		}
		
	}	// QUIT != TRUE
	return NULL;
}

/**
 * Write data to socket
 *
 * @param sock pointer to Socket on which write function will be called
 * @param data pointer to char table which will be send
 * @param length length of data which will be send
 * @return number of bytes writen to socket
 */

int SocketWrite( Socket* sock, char* data, unsigned int length )
{
	if( sock->s_SSLEnabled == TRUE )
	{
		//INFO( "SSL Write length: %d (sock: %p)\n", length, sock );
		
		int left = length;
		unsigned int written = 0;
		int res = 0;
		int errors = 0;
		
		int retries = 0;
		
		unsigned int bsize = length;
		
		int err = 0;		
		// Prepare to get fd state
		struct timeval timeoutValue = { 1, 0 };
		int sResult = 0; 
		fd_set fdstate;
		DEBUG("SocketWrite SSL %d\n", length  );
		
		while( written < length )
		{
			if( bsize + written > length ) bsize = length - written;
			
			if( sock->s_Ssl == NULL )
			{
				FERROR( "[ERROR] The ssl connection was dropped on this file descriptor!\n" );
				break;
			}
			
			res = SSL_write( sock->s_Ssl, data + written, bsize );
			
			if( res < 0 )
			{
				if( pthread_mutex_lock( &sock->mutex ) == 0 )
				{
					FD_ZERO( &fdstate );
					FD_SET( sock->fd, &fdstate );
					
					pthread_mutex_unlock( &sock->mutex );
				}
				
				err = SSL_get_error( sock->s_Ssl, res );
				
				switch( err )
				{
					// The operation did not complete. Call again.
					case SSL_ERROR_WANT_WRITE:
					{
						sResult = select( sock->fd + 1, NULL, &fdstate, NULL, &timeoutValue );
						int ch = FD_ISSET( sock->fd, &fdstate );
						// We're not gonna write now..
						if( ch == 0 ) usleep( 20000 );
						break;
					}
					default:
						DEBUG("Cannot write %d\n", err );
						return 0;
				}
			}
			else
			{	
				retries = 0;
				written += res;
				//DEBUG( "[SocketWrite] Wrote %d/%d\n", written, length );
			}
		}
		return written;
	}
	else
	{
		unsigned int written = 0, bufLength = length;
		int retries = 0, res = 0;

		do
		{
			if( bufLength > length - written ) bufLength = length - written;
			res = send( sock->fd, data + written, bufLength, MSG_DONTWAIT );
			DEBUG("SocketWrite result %d buffer %d\n", res, bufLength );
			
			if( res > 0 ) 
			{
				written += res;
				retries = 0;
			}
			else if( res < 0 )
			{
				// Error, temporarily unavailable..
				if( errno == 11 )
				{
					usleep( 400 ); // Perhaps allow full throttle?
					if( ++retries > 10 ) usleep( 20000 );
					continue;
				}
				DEBUG( "Failed to write: %d, %s\n", errno, strerror( errno ) );
				break;
			}
		}
		while( written < length );
		
		DEBUG("end write %d/%d (had %d retries)\n", written, length, retries );
		return written;
	}
}


/**
 * Abort write function
 *
 * @param sock pointer to Socket
 */

void SocketAbortWrite( Socket* sock )
{
	if( sock == NULL )
	{
		return;
	}

}

/**
 * Forcefully close a socket and free the socket object.
 *
 * @param sock pointer to Socket
 */

void SocketFree( Socket *sock )
{
	if( !sock )
	{
		FERROR("Passed socket structure is empty\n");
		return;
	}
	if( pthread_mutex_lock( &sock->mutex ) == 0 )
	{
		if( sock->s_SSLEnabled == TRUE )
		{
			if( sock->s_Ssl )
			{
				SSL_free( sock->s_Ssl );
				sock->s_Ssl = NULL;
			}
			if( sock->s_Ctx )
			{
				SSL_CTX_free( sock->s_Ctx );
				sock->s_Ctx = NULL;
			}
		}
		pthread_mutex_unlock( &sock->mutex );
	}
	
	pthread_mutex_destroy( &sock->mutex );
	
	free( sock );
}

/**
 * Close socket and release it
 *
 * @param sock pointer to Socket
 */

void SocketClose( Socket* sock )
{
	DEBUG("Close socket\n");
	if( sock == NULL || sock->fd == 0 )
	{
		FERROR("Socket: sock == NULL!\n");
		return;
	}
	
	if( sock->s_Users > 0 )
	{
		return;
	}

	if( pthread_mutex_lock( &sock->mutex ) == 0 )
	{
		if( sock->s_SSLEnabled == TRUE )
		{
			if( sock->s_Ssl )
			{
				while( SSL_shutdown( sock->s_Ssl ) == 0 )
				{
					usleep( 0 );
				}
				SSL_clear( sock->s_Ssl );
			}

			if( sock->s_BIO )
			{
				BIO_free( sock->s_BIO );;
			}
			sock->s_BIO = NULL;
		}
		// default
		if( sock->fd )
		{
		    shutdown( sock->fd, SHUT_RDWR );
			close( sock->fd );
			sock->fd = 0;
		}
		pthread_mutex_unlock( &sock->mutex );
		SocketFree( sock );
		sock = NULL;
	}
	//DEBUG( "[SocketClose] Freed socket.\n" );
}

