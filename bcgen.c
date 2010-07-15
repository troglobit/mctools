/* \\/ Westermo OnTime AS - Broadcast generator 
 * 
 * Copyright (C) 2001-2008  Westermo OnTime AS
 * Copyright (C) 2010  Joachim Nilsson <troglobit@gmail.com>
 *
 * Author(s): Øyvind Holmeide <oeyvind.holmeide@westermo.se>
 *            Joachim Nilsson <joachim.nilsson@westermo.se>
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

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <net/if.h>             /* if_nametoindex() */
#include <netdb.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <tgmath.h>
#include <time.h>
#include <unistd.h>

#define DEBUG(fmt, ...) {if (verbose) { printf (fmt, ## __VA_ARGS__);}}
#define UDP_PORT        18246
int verbose = 0;

/* Program meta data */
char *progname;                 /* argv[0] */
#define PROGRAM_VERSION "2.00"
const char *program_version = "v" PROGRAM_VERSION;
const char *program_bug_address = "<support@westermo.com>";
const char *doc = "Broadcast generator";


void nsleep (int nsec)
{
   struct timespec req;
   struct timespec rem;

   req.tv_sec = 0;
   req.tv_nsec = nsec;
   while (nanosleep (&req, &rem))
   {
      /* Interrupted while sleeping, retrying. */
      req = rem;
   }
}

void throttle (int v)
{
   int i;

   for (i = 0; i < v; i++)
   {
      nsleep (1);
   }
}

int throttle_calibrate (int rate)
{
   uint32_t i, usec, calib, laps = 50000;
   struct timeval tv1, tv2;

   gettimeofday (&tv1, NULL);
   for (i = 0; i < laps; i++)
   {
      nsleep (1);
   }
   gettimeofday (&tv2, NULL);
   usec = (tv2.tv_sec - tv1.tv_sec) * 100000.0 + (tv2.tv_usec - tv1.tv_usec);
   calib = (int)((1000000.0 / rate) * (50000.0 / usec));

   DEBUG ("start = %u; stop = %u; ", (uint32_t)tv1.tv_usec, (uint32_t)tv2.tv_usec);
   DEBUG ("benchmark: %d laps in %u usecs => ", laps, usec);
   DEBUG ("calib = %u\n", calib);

   return calib;
}

static int udp_socket_init (char *iface, short port, char *dst, struct sockaddr_in *sin, int qos)
{
   struct in_addr target;
   struct hostent *hostinfo;
   int sd, result;
   int val;

   if (!dst)
   {
      fprintf (stderr, "Invalid broadcast IP address.\n");
      return -1;
   }

   if (!inet_aton (dst, &target))
   {
      /* Maybe it's a name instead of an IP address? */
      hostinfo = gethostbyname (dst);
      if (!hostinfo)
      {
         perror ("Failed gethostbyname()");
         return -1;
      }

      memcpy (&target, hostinfo->h_addr, hostinfo->h_length);
   }

   /* Create socket for transmission. */
   sd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (sd < 0)
   {
      perror ("Failed to create datagram socket");
      return -1;
   }

   memset (sin, 0, sizeof (struct sockaddr_in));
   memcpy (&sin->sin_addr.s_addr, &target, sizeof (target));
   sin->sin_addr = target;
   sin->sin_family = AF_INET;
   sin->sin_port = htons (port);

   val = 1;
   result = setsockopt (sd, SOL_SOCKET, SO_BROADCAST, &val, sizeof (val));
   if (result < 0)
   {
      perror ("Failed setting, SO_BROADCAST, broadcast option on socket");
      return -1;
   }

   if (qos > 0 && geteuid ())
   {
      fprintf (stderr, "Cannot set IP QoS field, must be root.\n");
   }
   else
   {
      result = setsockopt (sd, IPPROTO_IP, IP_TOS, &qos, sizeof (qos));
      if (result < 0)
      {
         perror ("Failed setting IP_TOS, qualitiy of service field");

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

int usage (void)
{
   /* " -p, --payload=0XAA55       Extra payload.\n" */
   printf ("%s\n"
           "-------------------------------------------------------------------------------\n"
           "Usage: %s [-i iface] [-c count] [-Q tos] [-r rate] [-s size] destination\n"
           "\n"
           " -i, --interface=iface      Interface to send on, bypasses routing rules.\n"
           " -c, --count=num            Number of packets to send, in total.\n"
           " -Q, --tos=tos              Set Quality of Service-related bits.\n"
           " -r, --rate=rate            Packets per second.\n"
           " -s, --size=len             Payload size.\n"
            "-------------------------------------------------------------------------------\n"
           "Example:\n"
           "         %s -r 500 192.168.13.255\n", doc, progname, progname);

   return 0;
}


int main (int argc, char *argv[])
{
   size_t count = 0;    /* Forever */
   char *iface = NULL;  /* No default iface, rely on routing table. */
   int rate = 500;
   int qos = 0;                 /* No Prio */
   int sd, dly, c;
   size_t len = 22;             /* Default to 64 byte packets */
   int payload = 0xA5;
   struct sockaddr_in sin;
   struct option long_options[] = {
      /* {"verbose", 0, 0, 'V'}, */
      {"verbose", 0, 0, 'V'},
      {"version", 0, 0, 'v'},
      {"interface", 1, 0, 'i'},
      {"count", 1, 0, 'c'},
      {"tos", 1, 0, 'Q'},
      {"rate", 1, 0, 'r'},
      {"size", 1, 0, 's'},
      {"payload", 1, 0, 'p'},
      {"help", 0, 0, '?'},
      {0, 0, 0, 0}
    };

   progname = argv[0];

   while ((c = getopt_long (argc, argv, "i:c:p:Q:r:s:vVh?", long_options, NULL)) != EOF)
   {
      switch (c)
      {
         case 'i':              /* --interface */
            iface = strdup (optarg);
            DEBUG("Iface: %s\n", iface);
            break;

         case 'c':              /* --count */
            count = strtoul (optarg, NULL, 0);
            DEBUG("Count: %zu\n", count);
            break;

         case 'Q':              /* --tos */
            qos = strtoul (optarg, NULL, 0);
            DEBUG("QoS: %d\n", qos);
            break;

         case 'r':              /* --rate */
            rate = strtoul (optarg, NULL, 0);
            DEBUG("Rate: %d packets/second\n", rate);
            break;

         case 's':              /* --size */
            len = strtoul (optarg, NULL, 0);
            DEBUG("Size: %zu bytes payload\n", len);
            /* Adjust for MAC+UDP header */
            len = len > 64 ? len - 42 : 22; /* At least 64 bytes */
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
            return usage ();
      }
   }

   /* At least one argument needed. */
   if (argc < 2)
   {
      return usage();
   }

   DEBUG("Broadcast address %s, size %zu\n", argv[optind], len);
   
   sd = udp_socket_init (iface, UDP_PORT, argv[optind], &sin, qos);
   if (sd < 0)
   {
      if (iface)
      {
         free (iface);
      }
      return 1;
   }

   dly = throttle_calibrate (rate);

   while (1)
   {
      const char data[1458] = { [0 ... 1457] = payload };

      if (sendto (sd, data, len, 0, (struct sockaddr *)&sin, sizeof (sin)) < 0)
      {
         perror ("Failed sending packet");
         if (iface)
         {
            free (iface);
         }
         return 1;
      }

      throttle (dly);
   }

   if (iface)
   {
      free (iface);
   }

   return 0;
}
