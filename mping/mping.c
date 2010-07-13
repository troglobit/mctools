/* Simple multicast ping program 
 *
 * Taken from the book "Multicast Sockets - Practical Guide for Programmers"
 * written by David Makofske and Kevin Almeroth.  For online information see
 * http://www.nmsl.cs.ucsb.edu/MulticastSocketsBook/
 */

#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "mping.h"

int main (int argc, char **argv)
{
   int c;                       /* hold command-line args */
   int rcvflag = 0;             /* receiver flag */
   int sndflag = 0;             /* sender flag */
   extern int getopt ();        /* for getopt */
   extern char *optarg;         /* for getopt */

   /* parse command-line arguments */
   while ((c = getopt (argc, argv, "vrsa:p:t:")) != -1)
   {
      switch (c)
      {
         case 'r':
            /* mping receiver */
            rcvflag = 1;
            break;
         case 's':
            /* mping sender */
            sndflag = 1;
            break;
         case 'v':
            verbose = 1;
            break;
         case 'a':
            /* mping address override */
            strcpy (arg_mcaddr_str, optarg);
            break;
         case 'p':
            /* mping port override */
            arg_mcport = atoi (optarg);
            break;
         case 't':
            /* mping ttl override */
            arg_ttl = atoi (optarg);
            break;
         case '?':
            usage ();
            break;
      }
   }

   /* verify one and only one send or receive flag */
   if (((!rcvflag) && (!sndflag)) || ((rcvflag) && (sndflag)))
   {
      usage ();
   }

   printf ("mping version %d.%d\n", VERSION_MAJOR, VERSION_MINOR);

   init_socket ();

   get_local_host_info ();

   if (sndflag)
   {
      printf ("mpinging %s/%d with ttl=%d:\n\n", arg_mcaddr_str, arg_mcport, arg_ttl);

      /* catch interrupts with clean_exit() */
      signal (SIGINT, clean_exit);

      /* catch alarm signal with send_mping() */
      signal (SIGALRM, send_mping);

      /* send an alarm signal now */
      send_mping (SIGALRM);

      /* listen for response packets */
      sender_listen_loop ();

   }
   else
   {
      receiver_listen_loop ();
   }
   exit (0);
}

void init_socket ()
{
   int flag_on = 1;

   /* create a UDP socket */
   if ((sock = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
   {
      perror ("receive socket() failed");
      exit (1);
   }

   /* set reuse port to on to allow multiple binds per host */
   if ((setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &flag_on, sizeof (flag_on))) < 0)
   {
      perror ("setsockopt() failed");
      exit (1);
   }

   /* construct a multicast address structure */
   memset (&mc_addr, 0, sizeof (mc_addr));
   mc_addr.sin_family = AF_INET;
   mc_addr.sin_addr.s_addr = inet_addr (arg_mcaddr_str);
   mc_addr.sin_port = htons (arg_mcport);

   /* bind to multicast address to socket */
   if ((bind (sock, (struct sockaddr *)&mc_addr, sizeof (mc_addr))) < 0)
   {
      perror ("bind() failed");
      exit (1);
   }

   /* construct a IGMP join request structure */
   mc_request.imr_multiaddr.s_addr = inet_addr (arg_mcaddr_str);
   mc_request.imr_interface.s_addr = htonl (INADDR_ANY);

   /* send an ADD MEMBERSHIP message via setsockopt */
   if ((setsockopt (sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    (void *)&mc_request, sizeof (mc_request))) < 0)
   {
      perror ("setsockopt() failed");
      exit (1);
   }
}

void get_local_host_info ()
{
   char hostname[MAX_HOSTNAME_LEN];
   struct hostent *hostinfo;

   /* lookup local hostname */
   gethostname (hostname, MAX_HOSTNAME_LEN);

   if (verbose)
      printf ("Localhost is %s, ", hostname);

   /* use gethostbyname to get host's IP address */
   if ((hostinfo = gethostbyname (hostname)) == NULL)
   {
      perror ("gethostbyname() failed");
   }
   localIP.s_addr = *((unsigned long *)hostinfo->h_addr_list[0]);

   if (verbose)
      printf ("%s\n", inet_ntoa (localIP));

   pid = getpid ();
}

void send_mping ()
{
   static int current_ping = 0;
   struct timeval now;

   /* increment count, check if done */
   if (current_ping++ >= MAX_PINGS)
   {
      clean_exit ();
   }

   /* clear send buffer */
   memset (&mping_packet, 0, sizeof (mping_packet));

   /* populate the mping packet */
   mping_packet.type = SENDER;
   mping_packet.version_major = htons (VERSION_MAJOR);
   mping_packet.version_minor = htons (VERSION_MINOR);
   mping_packet.seq_no = htonl (current_ping);
   mping_packet.src_host.s_addr = localIP.s_addr;
   mping_packet.dest_host.s_addr = inet_addr (arg_mcaddr_str);
   mping_packet.ttl = arg_ttl;
   mping_packet.pid = pid;

   gettimeofday (&now, NULL);
   mping_packet.tv.tv_sec = htonl (now.tv_sec);
   mping_packet.tv.tv_usec = htonl (now.tv_usec);

   /* send the outgoing packet */
   send_packet (&mping_packet);

   /* set another alarm call to send in 1 second */
   signal (SIGALRM, send_mping);
   alarm (1);
}

void send_packet (struct mping_struct *packet)
{
   int pkt_len;

   pkt_len = sizeof (struct mping_struct);

   /* send string to multicast address */
   if ((sendto (sock, packet, pkt_len, 0,
                (struct sockaddr *)&mc_addr, sizeof (mc_addr))) != pkt_len)
   {
      perror ("sendto() sent incorrect number of bytes");
      exit (1);
   }
   packets_sent++;
}

void sender_listen_loop ()
{
   char recv_packet[MAX_BUF_LEN + 1];   /* buffer to receive packet */
   int recv_len;                /* len of packet received */
   struct timeval current_time; /* time value structure */
   double rtt;                  /* round trip time */

   while (1)
   {

      /* clear the receive buffer */
      memset (recv_packet, 0, sizeof (recv_packet));

      /* block waiting to receive a packet */
      if ((recv_len = recvfrom (sock, recv_packet, MAX_BUF_LEN, 0, NULL, 0)) < 0)
      {
         if (errno == EINTR)
         {
            /* interrupt is ok */
            continue;
         }
         else
         {
            perror ("recvfrom() failed");
            exit (1);
         }
      }

      /* get current time */
      gettimeofday (&current_time, NULL);

      /* process the received packet */
      if (process_mping_packet (recv_packet, recv_len, RECEIVER) == 0)
      {

         /* packet processed successfully */

         /* calculate round trip time in milliseconds */
         subtract_timeval (&current_time, &rcvd_pkt->tv);
         rtt = timeval_to_ms (&current_time);

         /* keep rtt total, min and max */
         rtt_total += rtt;
         if (rtt > rtt_max)
            rtt_max = rtt;
         if (rtt < rtt_min)
            rtt_min = rtt;

         /* output received packet information */
         printf ("%d bytes from %s: seqno=%d ttl=%d time=%.3f ms\n",
                 recv_len, inet_ntoa (rcvd_pkt->src_host),
                 rcvd_pkt->seq_no, rcvd_pkt->ttl, rtt);
      }
   }
}

void receiver_listen_loop ()
{
   char recv_packet[MAX_BUF_LEN + 1];   /* buffer to receive string */
   int recv_len;                /* len of string received */

   printf ("Listening on %s/%d:\n\n", arg_mcaddr_str, arg_mcport);

   while (1)
   {

      /* clear the receive buffer */
      memset (recv_packet, 0, sizeof (recv_packet));

      /* block waiting to receive a packet */
      if ((recv_len = recvfrom (sock, recv_packet, MAX_BUF_LEN, 0, NULL, 0)) < 0)
      {
         perror ("recvfrom() failed");
         exit (1);
      }

      /* process the received packet */
      if (process_mping_packet (recv_packet, recv_len, SENDER) == 0)
      {

         /* packet processed successfully */
         printf ("Replying to mping from %s bytes=%d seqno=%d ttl=%d\n",
                 inet_ntoa (rcvd_pkt->src_host), recv_len,
                 rcvd_pkt->seq_no, rcvd_pkt->ttl);

         /* populate mping response packet */
         mping_packet.type = RECEIVER;
         mping_packet.version_major = htons (VERSION_MAJOR);
         mping_packet.version_minor = htons (VERSION_MINOR);
         mping_packet.seq_no = htonl (rcvd_pkt->seq_no);
         mping_packet.dest_host.s_addr = rcvd_pkt->src_host.s_addr;
         mping_packet.src_host.s_addr = localIP.s_addr;
         mping_packet.ttl = rcvd_pkt->ttl;
         mping_packet.pid = rcvd_pkt->pid;
         mping_packet.tv.tv_sec = htonl (rcvd_pkt->tv.tv_sec);
         mping_packet.tv.tv_usec = htonl (rcvd_pkt->tv.tv_usec);

         /* send response packet */
         send_packet (&mping_packet);
      }
   }
}

void subtract_timeval (struct timeval *val, const struct timeval *sub)
{
   /* subtract sub from val and leave result in val */

   if ((val->tv_usec -= sub->tv_usec) < 0)
   {
      val->tv_sec--;
      val->tv_usec += 1000000;
   }
   val->tv_sec -= sub->tv_sec;
}

double timeval_to_ms (const struct timeval *val)
{
   /* return the timeval converted to a number of milliseconds */

   return (val->tv_sec * 1000.0 + val->tv_usec / 1000.0);
}

int process_mping_packet (char *packet, int recv_len, unsigned char type)
{

   /* validate packet size */
   if (recv_len < sizeof (struct mping_struct))
   {
      if (verbose)
         printf ("Discarding packet: too small (%d)\n", strlen (packet));
      return (-1);
   }

   /* cast data to mping_struct */
   rcvd_pkt = (struct mping_struct *)packet;

   /* convert required fields to host byte order */
   rcvd_pkt->version_major = ntohs (rcvd_pkt->version_major);
   rcvd_pkt->version_minor = ntohs (rcvd_pkt->version_minor);
   rcvd_pkt->seq_no = ntohl (rcvd_pkt->seq_no);
   rcvd_pkt->tv.tv_sec = ntohl (rcvd_pkt->tv.tv_sec);
   rcvd_pkt->tv.tv_usec = ntohl (rcvd_pkt->tv.tv_usec);

   /* validate mping version matches */
   if ((rcvd_pkt->version_major != VERSION_MAJOR) ||
       (rcvd_pkt->version_minor != VERSION_MINOR))
   {
      if (verbose)
         printf ("Discarding packet: version mismatch (%d.%d)\n",
                 rcvd_pkt->version_major, rcvd_pkt->version_minor);
      return (-1);
   }

   /* validate mping packet type (sender or receiver) */
   if (rcvd_pkt->type != type)
   {
      if (verbose)
      {
         switch (rcvd_pkt->type)
         {
            case SENDER:
               printf ("Discarding sender packet\n");
               break;
            case RECEIVER:
               printf ("Discarding receiver packet\n");
               break;
            case '?':
               printf ("Discarding packet: unknown type(%c)\n", rcvd_pkt->type);
               break;
         }
      }
      return (-1);
   }

   /* if response packet, validate pid */
   if (rcvd_pkt->type == RECEIVER)
   {
      if (rcvd_pkt->pid != pid)
      {
         if (verbose)
            printf ("Discarding packet: pid mismatch (%d/%d)\n",
                    (int)pid, (int)rcvd_pkt->pid);
         return (-1);
      }
   }

   /* packet validated, increment counter */
   packets_rcvd++;

   return (0);
}

void clean_exit ()
{
   /* send a DROP MEMBERSHIP message via setsockopt */
   if ((setsockopt (sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                    (void *)&mc_request, sizeof (mc_request))) < 0)
   {
      perror ("setsockopt() failed");
      exit (1);
   }

   /* close the socket */
   close (sock);

   /* output statistics and exit program */
   printf ("\n--- mping statistics ---\n");
   printf ("%d packets transmitted, %d packets received\n",
           packets_sent, packets_rcvd);
   if (packets_rcvd == 0)
      printf ("round-trip min/avg/max = NA/NA/NA ms\n");
   else
      printf ("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
              rtt_min, (rtt_total / packets_rcvd), rtt_max);
   exit (0);
}

void usage ()
{
   printf ("Usage: mping -r|-s [-v] [-a address]");
   printf (" [-p port] [-t ttl]\n\n");
   printf ("-r|-s        Receiver or sender. Required argument,\n");
   printf ("             mutually exclusive\n");
   printf ("-a address   Multicast address to listen/send on,\n");
   printf ("             overrides the default.\n");
   printf ("-p port      Multicast port to listen/send on,\n");
   printf ("             overrides the default of 10000.\n");
   printf ("-t ttl       Multicast Time-To-Live to send,\n");
   printf ("             overrides the default of 1.\n");
   printf ("-v           Verbose mode\n");
   exit (1);
}
