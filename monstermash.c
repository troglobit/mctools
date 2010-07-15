#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <net/if.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

/*
 * mcasta <=> mash <-> mash <=> mcastb
 *
 *
 *    rport tport        tport rport 
 * 
 * mash receives:
 *  anything in on rport multicast, and unicasts it from tport to
 * lport/other mash (host)
 * and receives:
 * anything unicast in on tport/myaddress, multicast from lport to 
 * rport/multicast (group)
 */

#define MULTICAST

#define HOST_NAME_SIZE	128

#define BUFFER_SIZE	1024
#define RECEIVE_PORT	2000
#define TRANSMIT_PORT	2000

#ifndef INADDR_NONE
#define INADDR_NONE     0xffffffff      /* should be in <netinet/in.h> */
#endif

int   r_port; /* we receive multicast on and send multicast to*/
int   t_port; /* we send unicast from and to... */
struct dh {
	int sockfd;
	char *host_name;
	unsigned int port;
	struct sockaddr_in their_u_address;
} unicast_sites[FD_SETSIZE];
int nunicast_sites;

char* group_name;

int   bufsiz;
int   unicast = -1, multicast = -1;
char* buf;
unsigned char ttl = 1; /* default... */

void check(int n);

void get_options(argc,argv)
int argc;
char *argv[];
 /*
  * Get the options from the command line, and convert them into
  * meaningful parameters.
  *
  *     Input:
  *        -r <receive_port (multicast) >
  *        -T <ttl distance>
  *        -t <transmit_port (unicast) >
  *        -b <buffer_size>
  *        -M turn off multicast reception
  *        -U turn off unicast reception
  */
{
  void usage();
  void parse_frame_file();

  char *name = *argv;

  while(--argc > 0 && ((*++argv)[0] == '-')) { 
    switch(*++(*argv)) { 
      case 'r':         /* receive port number */
        r_port = atoi(++*argv);
        break;

      case 't':         /* transmit port number */
        t_port = atoi(++*argv);
        break;

      case 'b':		/* bufer size */
        bufsiz = atoi(++*argv);
        break;

      case 'T':		/* bufer size */
        ttl = atoi(++*argv);
        break;

      case 'U':		/* uni to multi off... */
        unicast = 0;
        break;

      case 'M':		/* multi to uni off */
        multicast = 0;
        break;

      default:
        usage(name);
        exit(1);
    }
  }

  if (argc < 2) {
    usage(name);
    exit (1);
  }
  while (argc-- > 1) {
    if(sscanf(*argv, "%s %d", unicast_sites[nunicast_sites].host_name,
		&unicast_sites[nunicast_sites].port) != 2 ) {
	fprintf(stderr, "bad unicast args\n" );
	exit(-1);
    }
    argv++;
    nunicast_sites++;
  }
  strcpy(group_name, *argv);
}


void set_defaults()
{
  int i;
  for(i=0; i<FD_SETSIZE; i++) {
	  unicast_sites[i].host_name = (char *)malloc(HOST_NAME_SIZE);
	  bzero(unicast_sites[i].host_name, HOST_NAME_SIZE);
	  bzero( (char *)&unicast_sites[i].their_u_address, sizeof(struct sockaddr_in) );
  }
  group_name = (char *)malloc(HOST_NAME_SIZE);
  bzero(group_name, HOST_NAME_SIZE);

  r_port = RECEIVE_PORT;
  t_port = TRANSMIT_PORT; /* private pipe port */
  bufsiz = BUFFER_SIZE;
}

int main(int argc, char *argv[])
{
  int r_fd;
  int t_fd;
  struct dh *us;
  int nread = 0, nwrite;

/* at least 4 addresses */
/* bound r_fd, r_port receive (and send to) multicast */
  struct sockaddr_in our_m_address;       
  struct sockaddr_in their_m_address;       

/* bound t_fd, receive and send from) unicast  */
  struct sockaddr_in our_u_address;	  /* our unicast addr */

  unsigned i1, i2, i3, i4, g1, g2, g3, g4, z1;
  struct ip_mreq imr;

  unsigned long inaddr;
  struct hostent *host_info;               /* From gethostbyname   */
  unsigned char no = 0;
  int udest;

  /*
   * Initialise data structures
   */
  bzero( (char *)&our_m_address, sizeof(our_m_address) );

  set_defaults();
  get_options(argc, argv);
  buf = (char *)malloc(bufsiz);
  bzero(buf, bufsiz);

  /*
   * Create a socket to receive on
   */
  if ( (r_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
    perror("Couldn't create the socket");
    exit(1);
  }

  /* Create a socket to transmit on */
  if ( (t_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 )
  { 
    perror("Couldn't create the socket");
    exit(1);
  }
  for(udest = 0, us = unicast_sites; udest <nunicast_sites; udest++, us++) {
	/* Now associate some information with it.  */
	us->their_u_address.sin_family = AF_INET;
	us->their_u_address.sin_port   = htons(us->port);
	if ((inaddr = inet_addr(us->host_name)) != INADDR_NONE) { 
/* dotted-decimal */
		bcopy((char *)&inaddr, 
			(char *)&us->their_u_address.sin_addr, sizeof(inaddr));
	} else { 
		if ( (host_info = gethostbyname(us->host_name)) == NULL) { 
			perror("Host name not found");
			exit(1);
		}
		bcopy(host_info->h_addr, (char *)&us->their_u_address.sin_addr,
		  host_info->h_length);
	}
	printf("Transmitting to machine %s, port %d\n", us->host_name, us->port);
  }
/* 
 * now join appropriate group
 */
  if(sscanf( group_name, "%u.%u.%u.%u %u.%u.%u.%u %u",
		&g1, &g2, &g3, &g4, &i1, &i2, &i3, &i4, &z1 ) != 8 ) {
	fprintf(stderr, "bad group args %s\n", group_name);
	exit(-1);
  }
  imr.imr_multiaddr.s_addr = (g1<<24) | (g2<<16) | (g3<<8) | g4;
  imr.imr_multiaddr.s_addr = htonl(imr.imr_multiaddr.s_addr);
  imr.imr_interface.s_addr = (i1<<24) | (i2<<16) | (i3<<8) | i4;
  imr.imr_interface.s_addr = htonl(imr.imr_interface.s_addr);
  if(setsockopt(r_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
		&imr, sizeof(struct ip_mreq) ) == -1 ) {
	perror( "can't join group" );
	exit(-1);
  } else {
	printf( "group joined\n" );
  }
  if (multicast)
	setsockopt(r_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &no, sizeof(no));

  their_m_address.sin_family      = AF_INET;
  their_m_address.sin_port        = htons(r_port);
  bcopy(&imr.imr_multiaddr.s_addr, &their_m_address.sin_addr, 4);

  our_m_address.sin_family      = AF_INET;
  if (multicast)
	our_m_address.sin_port        = htons(r_port);
  else
	our_m_address.sin_port = 0;
  our_m_address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(r_fd, (struct sockaddr *)&our_m_address, sizeof(our_m_address)) < 0) { 
    perror("Couldn't bind the multicast socket");
    exit(1);
  }
  /* Set the TTL of multicast packets to 32  */
  {
    setsockopt(r_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  }

  our_u_address.sin_family      = AF_INET;

  if (unicast)
	our_u_address.sin_port        = htons(t_port);
  else
	our_u_address.sin_port        = 0;
  our_u_address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(t_fd, (struct sockaddr *)&our_u_address, sizeof(our_u_address)) < 0) { 
    perror("Couldn't bind the unicast socket");
    exit(1);
  }

  printf("Listening on port %d\n", r_port);
  printf("Bufersize = %d\n", bufsiz);
  
  /*
   * And bounce the input
   * not a simple echo, rather a UDP router:
   * in on r_fd (multicast) gets sent on t_fd (unicast)
   * in on t_fd (unicast) gets multicast on r_fd
   */
  do { 
    socklen_t msg_len;
    int sel;
    fd_set sofl;

    FD_ZERO  (&sofl);
    if (multicast)
	    FD_SET(r_fd, &sofl);
    if (unicast)
	    FD_SET(t_fd, &sofl);
    sel = select(FD_SETSIZE, &sofl, (fd_set *)0, (fd_set *)0, NULL);
    if (sel < 0) {
	perror("select");
	exit(-1);
    } else if (sel > 0) {
	if (FD_ISSET(r_fd, &sofl)) {
#ifdef DEBUG
	fprintf(stderr, "m");
#endif
	    nread = recvfrom(r_fd, buf, bufsiz, 0, NULL, &msg_len);
	    check(nread);
	    for(udest = 0, us = unicast_sites; udest <nunicast_sites; udest++, us++) {
		    nwrite = sendto(t_fd, buf, nread, 0, 
                                    (struct sockaddr *)&(us->their_u_address), 
                                    sizeof(struct sockaddr_in));
		    check(nwrite);
	    }
	}
	if (FD_ISSET(t_fd, &sofl)) {
#ifdef DEBUG
fprintf(stderr, "u");
#endif
	    nread = recvfrom(t_fd, buf, bufsiz, 0, NULL, &msg_len);
	    check(nread);
	    nwrite = sendto(r_fd, buf, nread, 0, 
                            (struct sockaddr *)&their_m_address, sizeof(their_m_address));
	    check(nwrite);
	}
    } else { /* assert (sel == 0) - but timeout infinite! */
	fprintf(stderr, "Timeout after infinity!\n");
	exit(-1);
    }
  } while (nread > 0);

  /*
   * Finally, close the sockets.
   */
  close(r_fd);
  close(t_fd);

  exit(0);
}


void usage(name)
char *name;
{
  fprintf(stderr, "Usage %s: [-r <receive_port>] [-t <transmit_port>] [-b <bufer_size>] [-T ttl] [-U] [-M] {\"other_host port\"}+ <\"group_name\">\n", name);
}

void check(int n)
{
	if (n < 0) {
		perror("io");
		exit(-1);
	}
}
