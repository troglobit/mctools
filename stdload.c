/* stdload - a mulitcast traffic generator designed to mimic an IETF broadcast.
 * 
 * Copyright (c) 1993  Pittsburgh Supercomputing Center, Matthew B Mathis.
 * Copyright (c) 2010  Joachim Nilsson <troglobit at gmail.com>
 *
 * All rights reserved.   See COPYRIGHT.h for additional information.
 *
 * This program is very dangerous!
 *
 * The command I am using to generate the IETF load is 
 * %  stdload -s 7 -c
 *                                 --Jamshid
 */
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>

#define MAXSES	10 /* maximum number of sessions */
#define DEFG	"224.2.200.68"	/* default group (UGH!) */
#define DEFP	12341		/* default port */
#define TIMEBASE 20000		/* 20 mS = 50 Hz */

#define NOERROR(val,msg) {if (((int)(val)) < 0) {perror(msg);exit(1);}}
#define NOTNULL(val,msg) {if (!(val)) {fprintf(stderr,msg);exit(1);}}
#define MEG 1000000
#define TIMEADJ(tv) \
{ if ((tv)->tv_usec >= MEG) {(tv)->tv_usec -= MEG; (tv)->tv_sec++;} \
  else if ((tv)->tv_usec < 0) {(tv)->tv_usec += MEG; (tv)->tv_sec--;};}
#define deltaU(start,stop) \
        (((stop).tv_sec - (start).tv_sec) * MEG + \
        ((stop).tv_usec - (start).tv_usec))

#define TV2TS(tv) ((((tv)->tv_usec*0x431C) >> 15) + ((tv)->tv_sec << 16))

char usage[] =
"Usage: stdload [-s <sess>] [-t <ttl>] [-m] [-c] [<group>]\n\
    -s <n>       Use only selected sessions\n\
    -t <ttl>     Clamp all signals to <ttl>\n\
    -m           Margin test, raise rates by 5%% for each -m\n\
    -c           Chop mode,  5 sec on/off (sync to GMT)\n\
    <group>      Multicast group, defaults to %s\n";

struct rtp_head {
/*  int ver:2, flow:6, P:1, S:1, for:6, seq16; */
  int unsigned bits:16, seq:16;
  long tstamp;
};
/*
 * No - we must not fragment on tunnels either....
 * #define MPAY (1500 - 20 - 8 - sizeof(struct rtp_head))
 */
#define MPAY 1400

struct session {
  /* parameters */
  int ttl, payload;
  int raten, rated;
  char *name;

  /* state */
  int seq;
  int ratediv;
  int port;
} s[] = {
  {.ttl = 255, .payload =  320, .raten = 1, .rated = 4, .name = "GSM Audio 1"},
  {.ttl = 223, .payload =  320, .raten = 1, .rated = 4, .name = "GSM Audio 2"},
  {.ttl = 191, .payload =  160, .raten = 1, .rated = 1, .name = "PCM Audio 1"},
  {.ttl = 159, .payload =  160, .raten = 1, .rated = 1, .name = "PCM Audio 2"},
  {.ttl = 191, .payload =   50, .raten = 1, .rated = 1, .name = "Assorted control and listener messages"},
  {.ttl = 127, .payload =  512, .raten = 1, .rated = 2, .name = "Video 1"},
  {.ttl =  95, .payload =  512, .raten = 1, .rated = 2, .name = "Video 2"},
  {.ttl =  63, .payload = MPAY, .raten = 1, .rated = 1, .name = "Test Application1"},
  {.ttl =  63, .payload = MPAY, .raten = 1, .rated = 1, .name = "Test Application2"}
};
#define NSES (sizeof(s)/sizeof(struct session))
#define DNS 4
size_t ns = DNS;

void ring(int signo);
int ttlclamp=255, margin=0, chop=0;
struct sockaddr_in grsin;
struct itimerval timebase;
struct timeval now;
int os;
int pkts, bytes, errors;
int tick, silent;

struct {
  struct rtp_head h;
  char data[MPAY];
} outbuf;

int main(int argc, char *argv[])
{
  char *sv, **av = argv, *name = DEFG;
  struct hostent *hp;
  int port = DEFP;
  int r, sz, Tr, Tbw;
  int sockbuf=32767;
  size_t i;

  argc--, av++;
  while (argc > 0 && *av[0] == '-') {
    sv = *av++; argc--;
    while (*++sv) switch (*sv) {
    case 's':
      if ((argc > 0) &&
	  ((ns = atoi(*av++)) > 0) &&
	  (ns <= NSES)) {
	argc--;
        break;
      }
      printf("  -s <n> to include sessions 1 through <n> of:\n");
/*           "d)  ddd  ddd  dddd   ddd   dddd   dddd nnnnnnnn"  */
      printf("    ttl  pps  size  kb/S  T pps T kb/S\n");
      Tr = Tbw = 0;
      for (i=0; i<NSES; i++) {
	r = 5000*s[i].raten/s[i].rated;		/* pay attention to roundoff */
	sz = 20+8+sizeof(struct rtp_head)+s[i].payload;
	Tr += r; Tbw += r*sz;
	printf("%zu)  %3d  %3d  %4d %4d   %4d   %4d %s\n",
	       i + 1, s[i].ttl, r/100, sz, r*sz*8/100000, Tr/100, Tbw*8/100000,
	       s[i].name);
      }
      printf("(Defaults to 1 through %d)\n", DNS);
      exit(1);
    case 't':
      if ((argc > 0) &&
          ((ttlclamp = atoi(*av++)) >= 0) &&
	  (ttlclamp < 256)) {
        argc--;
        break;
      }
      printf(usage, DEFG);
      printf("Invalid argument to -%c\n", *sv);
      exit(1);
    case 'm':
      margin++;
      break;
    case 'c':
      chop++;
      break;
    default:
      printf(usage, DEFG);
      printf("Unknown switch: -%c\n", *sv);
      exit(1);
    }
  }
  if (*av) name = *av; 
  if (isdigit(*name)) {
    grsin.sin_addr.s_addr = inet_addr(name);
  } else if ((hp = gethostbyname(name))) {
    memcpy(hp->h_addr, &grsin.sin_addr, hp->h_length);
  } else {
    printf("I Don't understand name %s\n",name);
    exit(1);
  }
  grsin.sin_family = AF_INET;
  NOERROR(os = socket(AF_INET, SOCK_DGRAM, 0), "socket");
  NOERROR(setsockopt(os, SOL_SOCKET, SO_SNDBUF, &sockbuf, sizeof(int)),
	  "SO_SNDBUF");
  for (i=0; i<NSES; i++) {
    s[i].port = htons(port++);
  }

  gettimeofday(&now, 0);
  tick = now.tv_sec-1;
  silent = chop;
  if (SIG_ERR == signal(SIGALRM, ring))
  {
    perror("signal");
    exit(1);
  }

  bzero(&timebase, sizeof(timebase));
  timebase.it_value.tv_usec = timebase.it_interval.tv_usec =
    TIMEBASE-(TIMEBASE*margin/20);
  NOERROR(setitimer(ITIMER_REAL, &timebase, 0), "setitimer");
  printf("pkts kb/S errors\n");
  for (;;) pause();
}

void ring(int signo __attribute__ ((unused)))
{
  int tsnow;
  size_t i;

  if (SIG_ERR == signal(SIGALRM, ring))
  {
    perror("signal");
    exit(1);
  }
  gettimeofday(&now, 0);
  if (tick != now.tv_sec) {
    if (silent) {
     putc('.', stdout);
     if ((tick%10) == 9) printf("\n");
     fflush(stdout);
    } else
      printf("%3d %5d %d\n", pkts, bytes*8/1000, errors);
    pkts = bytes = errors =0;
    tick++;
    if (chop) silent = (tick%10) >= 5;
  }
  if (silent) return;
  tsnow = TV2TS(&now);

  for (i = 0; i < ns; i++) {
    unsigned char ttl;
    s[i].ratediv += s[i].raten;
    while (s[i].ratediv >= s[i].rated) {
      s[i].ratediv -= s[i].rated;

      outbuf.h.bits = htons(0x4040);
      outbuf.h.seq = htons(s[i].seq++);
      outbuf.h.tstamp = htonl(tsnow);
      ttl = (s[i].ttl > ttlclamp)?ttlclamp:s[i].ttl;
      NOERROR(setsockopt(os, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, 1), "ttl");
      grsin.sin_port = s[i].port;
      if (sendto(os, (char *)&outbuf, sizeof(struct rtp_head)+s[i].payload, 0,
                 (struct sockaddr *)&grsin, sizeof(grsin)) > 0) {
	pkts++;
	bytes += 20 + 8 + sizeof(struct rtp_head)+s[i].payload;
      } else
	errors++;
    }
  }
}
