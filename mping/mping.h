/* Simple multicast ping program 
 *
 * Taken from the book "Multicast Sockets - Practical Guide for Programmers"
 * written by David Makofske and Kevin Almeroth.  For online information see
 * http://www.nmsl.cs.ucsb.edu/MulticastSocketsBook/
 */

#include <sys/types.h>           /* for type definitions */
#include <sys/socket.h>          /* for socket calls */
#include <netinet/in.h>          /* for address structs */
#include <arpa/inet.h>           /* for sockaddr_in */
#include <unistd.h>              /* for symbolic constants */
#include <errno.h>               /* for system error messages */
#include <sys/time.h>            /* for timeval and gettimeofday */
#include <netdb.h>               /* for hostname calls */
#include <signal.h>              /* for signal calls */
#include <stdlib.h>              /* for close and getopt calls */

#define MAX_BUF_LEN      1024    /* size of receive buffer */
#define MAX_HOSTNAME_LEN  256    /* size of host name buffer */
#define MAX_PINGS           5    /* number of pings to send */

#define VERSION_MAJOR 1          /* mping version major */
#define VERSION_MINOR 0          /* mping version minor */

#define SENDER   's'             /* mping sender identifier */
#define RECEIVER 'r'             /* mping receiver identifier */

/* mping packet structure */
struct mping_struct
{
   unsigned short version_major;
   unsigned short version_minor;
   unsigned char type;
   unsigned char ttl;
   struct in_addr src_host;
   struct in_addr dest_host;
   unsigned int seq_no;
   pid_t pid;
   struct timeval tv;
} mping_packet;

/* pointer to mping packet buffer */
struct mping_struct *rcvd_pkt;

int sock;                       /* socket descriptor */
pid_t pid;                      /* pid of mping program */

struct sockaddr_in mc_addr;     /* socket address structure */
struct ip_mreq mc_request;      /* multicast request structure */

struct in_addr localIP;         /* address struct for local IP */

/* counters and statistics variables */
int packets_sent = 0;
int packets_rcvd = 0;
double rtt_total = 0;
double rtt_max = 0;
double rtt_min = 999999999.0;

/* default command-line arguments */
char arg_mcaddr_str[16] = "239.255.255.1";
int arg_mcport = 10000;
unsigned char arg_ttl = 1;

int verbose = 0;

/* function prototypes */
void init_socket ();
void get_local_host_info ();
void send_mping ();
void send_packet (struct mping_struct *packet);
void sender_listen_loop ();
void receiver_listen_loop ();
void subtract_timeval (struct timeval *val, const struct timeval *sub);
double timeval_to_ms (const struct timeval *val);
int process_mping_packet (char *packet, int recv_len, unsigned char type);
void clean_exit ();
void usage ();
