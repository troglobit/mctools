/* \\/ Westermo OnTime -- Simple Multicast Blaster
 *
 * Copyright (C) 2007 Westermo OnTime AS
 *
 * The multiblast tool was written by Jakob Eriksson, 2007
 *
 * Multiblast is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * Multiblast is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Multiblast; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <sys/socket.h>          /* for socket API function calls */
#include <netinet/in.h>          /* for address structs */
#include <arpa/inet.h>           /* for sockaddr_in */
#include <stdio.h>               /* for printf() */
#include <stdlib.h>              /* for atoi() */
#include <string.h>              /* for strlen() */
#include <ctype.h>               /* for uint32_t */

#define UTEST 1
#define NO_FLIP_BOOL
#include <otn/test.h>
#include <otn/dbc.h>

/* return 1 if argument is a multicast address in network byte order */
static int is_multicast_address (uint32_t v)
{
   return *(unsigned char*)&v >> 4 == 14;
}

static int self_test (void)
{
   ok (is_multicast_address (inet_addr ("224.1.1.1")));
   ok (!is_multicast_address (inet_addr ("223.1.1.1")));
   ok (is_multicast_address (inet_addr ("239.1.1.1")));
   ok (!is_multicast_address (inet_addr ("240.1.1.1")));
   ok (!is_multicast_address (inet_addr ("240.1.1.224")));
   ok (is_multicast_address (inet_addr ("238.255.255.255")));
   
   return 0;
}

/*!
pre:
dbcok (is_multicast_address (address));
dbcok (a);

post:
!*/
static int mk_address (uint32_t address, struct sockaddr_in *a)
{
   memset (a, 0, sizeof (*a));
   a->sin_family = PF_INET; /* PF_INET ? */
   a->sin_addr.s_addr = address;
   a->sin_port = htons (1234);

   return 0;
}

/*!
pre:
dbcok (fd);

post:
dbcok (ret == 1 || ret == 2 || 0 == ret);
!*/
static int create_socket (int *fd)
{
   unsigned char ttl;          /* time to live (hop count) */
   int n;

   /* create a socket for sending to the multicast address */
   n = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (n < 0)
   {
      return 1;
   }

   /* set the TTL (time to live/hop count) for the send */
   ttl = 1;
   if ((setsockopt (n, IPPROTO_IP, IP_MULTICAST_TTL,
                    (void *)&ttl, sizeof (ttl))) < 0)
   {
      close (n);

      return 2;
   }

   *fd = n;

   return 0;
}

/*!
pre:
dbcok (fd >= 0);
dbcok (is_multicast_address (address));
dbcok (num_addresses);

post:
dbcok (ret == 1 || 0 == ret);
!*/
static int send_to_addresses (int fd, uint32_t address, int num_addresses, uint32_t data)
{
   int i;
   struct sockaddr_in a;

   address = ntohl (address);
   for (i = 0; i < num_addresses; i++)
   {
      mk_address (htonl (address + i), &a);
      if (sizeof (data) != sendto (fd, &data, sizeof (data), 0, (struct sockaddr *)&a, sizeof (a)))
      {
         return 1;
      }
   } 

   return 0;
}


/**
   Loop forever if num_bursts == 0
*/
/*!
pre:
dbcok (fd >= 0);
dbcok (is_multicast_address (address));
dbcok (num_addresses);
dbcok (delay >= 0);
dbcok (num_bursts >= 0);

post:
dbcok (ret == 1 || 0 == ret);
!*/
static int send_loop (int fd, uint32_t address, int num_addresses, int delay, int num_bursts)
{
   uint32_t i = 0;

   do
   {
      if (send_to_addresses (fd, address, num_addresses, i))
      {
         return 1;
      }
      sleep (delay);
      i++;

      num_bursts--;
      if (num_bursts < 0)
      {
         num_bursts = -1;
      }
   } while (num_bursts);

   return 0; /* Never reached. */
}

int main (int argc, char *argv[])
{
   int fd;
   uint32_t start_address;
   int num_addresses;
   int delay;
   int num_bursts;

   if (2 == argc && !strcmp ("self_test", argv[1]))
   {
      return run (self_test);
   }


   /* validate number of arguments */
   if (5 != argc)
   {
      fprintf (stderr,
               "Usage: %s <multicast address> <number of addresses> <delay between bursts> <number of bursts>\n\n"
               "This program sends a bunch of multicast messages (port 1234) "
               "to destinations given on the commandline. Number of bursts can be 0, which means infinite loop.\n",
               argv[0]);

      return 1;
   }

   start_address = inet_addr (argv[1]);
   if (!is_multicast_address (start_address))
   {
      fprintf (stderr, "Not a multicast address.\n");
         
      return 1;
   }

   num_addresses = atoi (argv[2]);
   if (num_addresses <= 0)
   {
      fprintf (stderr, "Number of addresses must be larger than zero.\n");

      return 1;
   }
   
   if (!is_multicast_address (htonl (ntohl (start_address) + num_addresses)))
   {
      fprintf (stderr, "Multicast number of addresses is out of range\n");
         
      return 1;
   }


   delay = atoi (argv[3]);
   if (delay < 0)
   {
      fprintf (stderr, "Delay cannot be a negative.\n");
         
      return 1;
   }

   num_bursts = atoi (argv[4]);
   if (delay < 0)
   {
      fprintf (stderr, "Bursts cannot be a negative.\n");
         
      return 1;
   }

   if (create_socket (&fd))
   {
      fprintf (stderr, "Failed creating socket.\n");
      
      return 1;
   }

   if (send_loop (fd, start_address, num_addresses, delay, num_bursts))
   {
      fprintf (stderr, "send failed.\n");

      return 1;
   }

   return 0;
}
/**
 * Local Variables:
 *  compile-command: "make multiblast && ./multiblast self_test"
 *  version-control: t
 *  kept-new-versions: 2
 *  c-file-style: "ellemtel"
 * End:
 */
