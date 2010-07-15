/* \\/ Westermo OnTime -- Multicast Generator v2
 *
 * Copyright (C) 2007, 2008  Westermo OnTime AS
 * Copyright (C) 2010  Joachim Nilsson <troglobit@gmail.com>
 *
 * Generation 2 of mcgen is based on the multiblast tool, but is
 * heavily refactored.
 *
 * Authors: Jakob Eriksson (multiblast) <jakob.eriksson@westermo.se>, 
 *          Joachim Nilsson (mcgen) <joachim.nilsson@westermo.se>
 *
 * Description:
 * This program is capable of generating high multicast loads.  It does
 * not expect anything in return, so it is a sender-only tool.
 *
 * License:
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-
 * INFRINGEMENT.  IN NO EVENT SHALL WESTERMO TELEINDUSTRI, WESTERMO
 * ONTIME AS, OR ANY OF ITS AFFILIATES BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * Except as contained in this notice, the name of Westermo OnTime, or
 * Westermo Teleindustri shall not be used in advertising or otherwise
 * to promote the sale, use or other dealings in this Software without
 * prior written authorization from Westermo Teleindustri AB.
 */

#include <arpa/inet.h>          /* for sockaddr_in */
#include <ctype.h>              /* for uint32_t */
#include <errno.h>
#include <getopt.h>
#include <net/if.h>             /* if_nametoindex() */
#include <netinet/in.h>         /* for address structs */
#include <stdio.h>              /* for printf() */
#include <stdlib.h>             /* for atoi() */
#include <string.h>             /* for strlen() */
#include <sys/ioctl.h>
#include <sys/socket.h>         /* for socket API function calls */
#include <sys/time.h>           /* gettimeofday() */
#include <time.h>               /* gettimeofday() */
#include <unistd.h>

#ifdef UNITTEST
#include "otn/test.h"
#endif

#define DEBUG(fmt, ...) {if (verbose) { printf (fmt, ## __VA_ARGS__);}}
#define UDP_PORT        18246
int verbose = 0;

/* Program meta data */
char *progname;                 /* argv[0] */
#define PROGRAM_VERSION "2.00"
const char *program_version = "v" PROGRAM_VERSION;
const char *program_bug_address = "<support@westermo.com>";
const char *doc = "Multicast generator ";

static inline void nsleep (int nsec)
{
   struct timespec req;

   req.tv_sec = 0;
   req.tv_nsec = nsec;
   nanosleep (&req, NULL);
}

static inline void throttle (int v)
{
   int i;

   for (i = 0; i < v; i++)
   {
      nsleep (1);
   }
}

int throttle_calibrate (int rate)
{
   uint32_t i, usec, calib, laps = 5000;
   struct timeval tv1, tv2;

   DEBUG("Calibrating rate... \n");

   gettimeofday (&tv1, NULL);
   for (i = 0; i < laps; i++)
   {
      nsleep (1);
   }
   gettimeofday (&tv2, NULL);
   usec = (tv2.tv_sec - tv1.tv_sec) * 1000000.0 + (tv2.tv_usec - tv1.tv_usec);
   calib = (int)((1000000.0 / rate) * (5000.0 / usec));

   DEBUG ("start = %u; stop = %u; ", (uint32_t)tv1.tv_usec, (uint32_t)tv2.tv_usec);
   DEBUG ("benchmark: %d laps in %u usecs => ", laps, usec);
   DEBUG ("calib = %u\n", calib);

   return calib;
}

static int udp_socket_init (char *iface, uint8_t ttl, uint8_t qos)
{
   int sd, result;

   /* create a socket for sending to the multicast address */
   sd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (sd < 0)
   {
      perror ("Failed to create datagram socket");
      return -1;
   }

   /* set the TTL (time to live/hop count) for the send */
   result = setsockopt (sd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof (ttl));
   if (result < 0)
   {
      perror ("Failed setting, SO_BROADCAST, broadcast option on socket");
      close (sd);

      return -1;
   }

   if (qos > 0)
   {
      if (geteuid ())
      {
         fprintf (stderr, "Cannot set IP QoS field, must be root.\n");
         close (sd);

         return -1;
      }

      result = setsockopt (sd, IPPROTO_IP, IP_TOS, &qos, sizeof (qos));
      if (result < 0)
      {
         perror ("Failed setting IP_TOS, qualitiy of service field");
         close (sd);

         return -1;
      }
   }

   if (iface)
   {
      struct ifreq ifr;

      if (geteuid ())
      {
         fprintf (stderr, "Cannot bind to %s, must be root, try sudo.\n", iface);
         close (sd);

         return -1;
      }

      strncpy(ifr.ifr_name, iface, sizeof(ifr.ifr_name));
      result = ioctl(sd, SIOCGIFFLAGS, &ifr);
      if (result < 0)
      {
         fprintf (stderr, "Failed reading iface %s status: %s\n", iface, strerror(errno));
         close (sd);

         return -1;
      }

      DEBUG("The result of SIOCGIFFLAGS on %s is 0x%x.\n", ifr.ifr_name, ifr.ifr_flags);
      if (!(ifr.ifr_flags & IFF_UP)) 
      {
         fprintf (stderr, "Interface %s is not up, aborting!\n", ifr.ifr_name);
         close (sd);

         return -1;
      }

      result = setsockopt (sd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface));
      if (result < 0)
      {
         fprintf (stderr, "Failed binding to interface %s: %s\n", iface, strerror(errno));
         close (sd);

         return -1;
      }

      DEBUG("Bound socket %d to interface %s\n", sd, iface);
   }

   return sd;
}

static int send_to_addresses (int sd, in_addr_t address, int num, const char *data, size_t len)
{
   int i;
   struct sockaddr_in sin;

   int mkaddr (in_addr_t address, struct sockaddr_in *sin)
   {
      memset (sin, 0, sizeof (struct sockaddr_in));
      memcpy (&sin->sin_addr.s_addr, &address, sizeof (address));
      sin->sin_family = AF_INET;
      sin->sin_port = htons (12345);

      return 0;
   }

   address = ntohl (address);
   for (i = 0; i < num; i++)
   {
      mkaddr (htonl (address + i), &sin);
      if ((ssize_t)len != sendto (sd, data, len, 0, (struct sockaddr *)&sin, sizeof (sin)))
      {
         perror("Failed sending packet");

         return 1;
      }
   } 

   return 0;
}


/**
 * send_loop - Sends multicast packets
 * @iface: Egress interface
 * @address: Starting multicast address.
 * @num: Number of multicast addresses to generate.
 * @count: Number of bursts, loop forever if 0.
 * @ttl: Time to live (hop count), adjust if routing multicast.
 * @qos: IP Quality of Service, diffserv setting.
 * @rate: Packets per second.
 *
 * ... add neat documentation of functionality here ...
 *
 * Returns:
 * Zero (0) on success, non-zero otherwise.
 */
static int send_loop (char *iface, uint32_t address, int num, int count,
                      uint8_t ttl, uint8_t qos, int rate, const char *data, size_t len)
{
   int sd, delay;

   int loop (void)
   {
      static int n = -1;

      /* Initialize on first entry */
      if (-1 == n && count)
         n = count;

      /* Any requested laps? */
      if (count)
         return n--;

      /* Nope, loop forever... */
      return 1;
   }

   sd = udp_socket_init (iface, ttl, qos);
   if (sd < 0)
   {
      return 1;
   }

   delay = throttle_calibrate (rate);

   while (loop ())
   {
      if (send_to_addresses (sd, address, num, data, len))
      {
         return 1;
      }

      throttle (delay);
   }

   return 0; 
}

#ifndef UNITTEST
int usage (char *name)
{
   printf ("%s %s\n"
            "-------------------------------------------------------------------------------\n"
           "Usage: %s [-i iface] [-c count] [-Q tos] [-r rate] [-s size] group [-n num]\n"
           "\n"
           " -h, --help                 This help.\n"
           " -v, --version              Show program version.\n"
           " -V, --verbose              Verbose output, trace operations."
           " -i, --interface=iface      Interface to send on.\n"
           " -n, --number-groups=num    Number of groups to send in each burst.\n"
           " -c, --count=num            Number of packets to send, in total.\n"
           " -p, --payload=0XAA         Payload, repeated --size times.\n"
           " -Q, --tos=tos              Set Quality of Service-related bits.\n"
           " -r, --rate=rate            Packets per second.\n"
           " -s, --size=len             Payload size, in bytes.\n"
           " -t, --ttl=ttl              Set IP Time to Live.\n"
           "-------------------------------------------------------------------------------\n"
           "Example:\n"
           "         %s -c 25 225.1.2.3\n", doc, program_version, name, name);

   return 0;
}


/*
Usage: %s <multicast address> <number of addresses> <delay between bursts> <number of bursts>
       This program sends a bunch of multicast messages (port 1234)
       to destinations given on the commandline. Number of bursts can be 0, which means infinite loop.
*/
int main (int argc, char *argv[])
{
   in_addr_t start_address;
   int num = 1;
   int count = 0;
   int ttl = 1;                 /* Default Time to Live, don't route. */
   int qos = 0;                 /* No QoS by default. */
   int rate = 1;                /* Default: 1 pps */
   int c;
   size_t len = 22;             /* Default to 64 byte packets */
   int payload = 0xA5;
   char *iface = NULL;  /* No default iface, rely on routing table. */
   struct option long_options[] = {
      /* {"verbose", 0, 0, 'V'}, */
      {"verbose", 0, 0, 'V'},
      {"version", 0, 0, 'v'},
      {"interface", 1, 0, 'i'},
      {"number-groups", 1, 0, 'n'},
      {"count", 1, 0, 'c'},
      {"tos", 1, 0, 'Q'},
      {"rate", 1, 0, 'r'},
      {"size", 1, 0, 's'},
      {"ttl", 1, 0, 't'},
      {"payload", 1, 0, 'p'},
      {"help", 0, 0, '?'},
      {0, 0, 0, 0}
    };

   while ((c = getopt_long (argc, argv, "i:c:p:Q:r:s:t:vVh?", long_options, NULL)) != EOF)
   {
      switch (c)
      {
         case 'i':              /* --interface */
            iface = strdup (optarg);
            DEBUG("Iface: %s\n", iface);
            break;

         case 'c':              /* --count */
            count = strtoul (optarg, NULL, 0);
            DEBUG("Count: %u\n", num);
            break;

         case 'Q':              /* --tos */
            qos = strtoul (optarg, NULL, 0);
            DEBUG("QoS: %d\n", qos);
            break;

         case 'r':              /* --rate */
            rate = strtoul (optarg, NULL, 0);
            DEBUG("Rate: %d packets/second\n", rate);
            /* Sanity check... */
            rate = rate < 1 ? 1 : rate;
            break;

         case 's':              /* --size */
            len = strtoul (optarg, NULL, 0);
            DEBUG("Size: %zu bytes payload\n", len);
            /* Adjust for MAC+UDP header */
            len = len > 64 ? len - 42 : 22; /* At least 64 bytes */
            break;

         case 't':              /* --ttl */
            ttl = strtoul (optarg, NULL, 0);
            DEBUG("Size: %u bytes payload\n", ttl);
            break;

         case 'p':              /* --payload */
            payload = strtoul (optarg, NULL, 0);
            DEBUG("Size: %d bytes payload\n", payload);
            break;

         case 'v':              /* --version */
            printf ("%s %s\n", doc, program_version);
            return 0;

         case 'V':              /* --verbose */
            verbose = 1;
            break;

         case 'h':
         case '?':
         default:
            return usage (argv[0]);
      }
   }

   /* At least one argument needed. */
   if ((optind + 1) < argc)
   {
      return (usage(argv[0]));
   }

   DEBUG("Multicast starting address %s, num %zu\n", argv[optind], len);
   start_address = inet_addr (argv[optind]);
   if (!IN_MULTICAST (htonl(start_address)))
   {
      fprintf (stderr, "Not a valid multicast address.\n");
         
      return 1;
   }
/*
   num = atoi (argv[2]);
   if (num <= 0)
   {
      fprintf (stderr, "Number of addresses must be larger than zero.\n");

      return 1;
   }
   
   if (!IN_MULTICAST (htonl (ntohl (start_address) + num)))
   {
      fprintf (stderr, "Multicast number of addresses is out of range\n");
         
      return 1;
   }


   delay = atoi (argv[3]);
   if (delay < 0)
   {
      fprintf (stderr, "Delay can not be a negative.\n");
         
      return 1;
   }

   count = atoi (argv[4]);
   if (delay < 0)
   {
      fprintf (stderr, "Delay can not be a negative.\n");
         
      return 1;
   }
*/
   {
      const char data[1458] = { [0 ... 1457] = payload };
      return send_loop (iface, start_address, num, count, ttl, qos, rate, data, len);
   }
}
#endif  /* !UNITTEST */

/******************************** UNIT TESTS ********************************/
#ifdef UNITTEST
static int self_test (void)
{
   in_addr_t addr;

   ok (IN_MULTICAST (htonl(inet_addr ("224.1.1.1"))));
   ok (!IN_MULTICAST (htonl(inet_addr ("223.1.1.1"))));
   ok (IN_MULTICAST (htonl(inet_addr ("239.1.1.1"))));
   ok (!IN_MULTICAST (htonl(inet_addr ("240.1.1.1"))));
   ok (!IN_MULTICAST (htonl(inet_addr ("240.1.1.224"))));
   ok (IN_MULTICAST (htonl(inet_addr ("238.255.255.255"))));
   
   return 0;
}

int main (void)
{
   return self_test ();
}

#endif  /* UNITTEST */
/**
 * Local Variables:
 *  compile-command: "gcc -g -I../common/src -o unittest -DUTEST=1 -DUNITTEST mcgen.c"
 *  version-control: t
 *  kept-new-versions: 2
 *  c-file-style: "ellemtel"
 * End:
 */
