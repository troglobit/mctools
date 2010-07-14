/*
 * Program to dump the contents of all packets arriving on a specified
 * multicast address and port.  The dump gives both the hex and ASCII
 * equivalents of the packet payload.
 * 
 * Author: J.P.Knight@lut.ac.uk
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.
 *
 * Note: this program is based on the sd_listen program by Tom Pusateri
 * (pusateri@cs.duke.edu) and developed by Jon Knight (J.P.Knight@lut.ac.uk).
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * $Id: mdump.c,v 1.2 1994/10/04 16:25:15 jon Exp jon $
 *
 * $Log: mdump.c,v $
 * Revision 1.2  1994/10/04  16:25:15  jon
 * Fixed bug that caused junk to appear at the end of packets that weren't
 * exactly divisible by the display WIDTH.
 *
 * Revision 1.1  1994/09/27  13:39:45  jon
 * Initial revision
 *
 *
 */


#define MULTICAST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#define DEFAULT_GROUP	0xe0027fff
#define DEFAULT_PORT		9876
#define MAXPDU 4096
#define WIDTH 16

u_long groupaddr = DEFAULT_GROUP;
u_short groupport = DEFAULT_PORT;

void dump(buf, buflen)
char *buf;
int buflen;
{
	int i,j;
	unsigned char c;
	char text[WIDTH];
	printf("\nBuffer length: %d\n",buflen);
	for (i=0; i<buflen/WIDTH; i++) {
		for (j=0; j<WIDTH; j++ ) {
			c=buf[i*WIDTH+j];
			printf("%02x ",c);
			text[j]=((c<32)||(c>126))?'.':c;
		}
		printf("\t%s\n",text);
	}
	
	for (i=0; i<buflen%WIDTH; i++ ) {
		c=buf[buflen-buflen%WIDTH+i];
		printf("%02x ",c);
		text[i]=((c<32)||(c>126))?'.':c;
	}
	for (i=buflen%WIDTH; i<WIDTH; i++ ) {
		printf("   ");
		text[i]=' ';
	}
	printf("\t%s\n",text);
}

int main(argc, argv)
int argc;
char *argv[];
{
    int i, j;
    int sock, length, origlen;
    char buf[MAXPDU];
    struct sockaddr_in name;
    struct ip_mreq imr;
    char *interface = NULL;
    char debug=0;
    fd_set fds;
    char tmpstr[100];
    int ttl;
    int port1, port2;
    u_long time1, time2;
    struct in_addr source;

    if (argc > 4) {
	printf("usage: %s [group [port [interface]]]\n", argv[0]);
	exit(1);
    }

    if (argc > 1) {
	groupaddr = inet_addr(argv[1]);
    }

    if (argc > 2) {
	groupport = (u_short)atoi(argv[2]);
    }

    if (argc == 4) {
	interface=argv[3];
    }    

    if((sock=socket( AF_INET, SOCK_DGRAM, 0 )) < 0) {
        perror("socket");
        exit(1);
    }

    imr.imr_multiaddr.s_addr = groupaddr;
    imr.imr_multiaddr.s_addr = htonl(imr.imr_multiaddr.s_addr);
    if (interface!=NULL) {
        imr.imr_interface.s_addr = inet_addr(interface);
    } else {
	imr.imr_interface.s_addr = htonl(INADDR_ANY);
    }
    imr.imr_interface.s_addr = htonl(imr.imr_interface.s_addr);

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
	&imr, sizeof(struct ip_mreq)) < 0 ) {
	perror("setsockopt - IP_ADD_MEMBERSHIP");
	exit(1);
    }

	/*
	 *	Use INADDR_ANY if your multicast port doesn't allow
	 *	binding to a multicast address.
	 *
	 */

    name.sin_family = AF_INET;
#ifndef CANT_MCAST_BIND
    name.sin_addr.s_addr = htonl(groupaddr);
#else
    name.sin_addr.s_addr = INADDR_ANY;
#endif
    name.sin_port = htons(groupport);
    if (bind(sock, (struct sockaddr *)&name, sizeof(name))) {
	perror("bind");
	exit(1);
    }

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    while (select(sock + 1, &fds, 0, 0, 0) > 0) {
	j = 0;
	if ((length = recv(sock, (char *) buf, sizeof(buf), 0)) < 0) {
		perror("recv");
		exit(1);
	}
	dump(buf,length);
    }    
    close(sock);
    return(0);
}
