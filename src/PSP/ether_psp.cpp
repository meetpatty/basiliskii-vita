/*
 *  ether_psp.cpp - Ethernet device driver, PSP specific stuff
 *
 *  Basilisk II (C) 1997-2005 Christian Bauer
 *  Portions written by Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "sysdeps.h"


#include <pspsdk.h>
#include <psputility_netmodules.h>
#include <psputility_netparam.h>
#include <pspwlan.h>
#include <pspnet.h>
#include <pspnet_apctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "macos_util.h"
#include "ether.h"
#include "ether_defs.h"


#define DEBUG 0
#include "debug.h"

#define MONITOR 0


// Global variables
static int read_thread;						// Packet reception thread
static bool ether_thread_active = true;		// Flag for quitting the reception thread

static int fd = -1;							// UDP socket fd
static bool udp_tunnel = false;


/*
 *  Initialization
 */

bool ether_init(void)
{
	// Do nothing as PSP only does udptunnel
	return false;
}


/*
 *  Deinitialization
 */

void ether_exit(void)
{
	// Do nothing as PSP only does udptunnel
}


/*
 *  Reset
 */

void ether_reset(void)
{
	// Do nothing as PSP only does udptunnel
}


/*
 *  Add multicast address
 */

int16 ether_add_multicast(uint32 pb)
{
	// Do nothing as PSP only does udptunnel
	return noErr;
}


/*
 *  Delete multicast address
 */

int16 ether_del_multicast(uint32 pb)
{
	// Do nothing as PSP only does udptunnel
	return noErr;
}


/*
 *  Attach protocol handler
 */

int16 ether_attach_ph(uint16 type, uint32 handler)
{
	// Do nothing as PSP only does udptunnel
	return noErr;
}


/*
 *  Detach protocol handler
 */

int16 ether_detach_ph(uint16 type)
{
	// Do nothing as PSP only does udptunnel
	return noErr;
}


/*
 *  Transmit raw ethernet packet
 */

int16 ether_write(uint32 wds)
{
	// Do nothing as PSP only does udptunnel
	return noErr;
}


/*
 *  Packet reception thread (UDP)
 */

static int receive_proc_udp(SceSize args, void *argp)
{
	while (ether_thread_active)
	{
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(fd, &readfds);
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		select(fd+1, &readfds, NULL, NULL, &timeout);
		if (FD_ISSET(fd, &readfds))
		{
			D(bug(" packet received, triggering Ethernet interrupt\n"));
			SetInterruptFlag(INTFLAG_ETHER);
			TriggerInterrupt();
		}
	}
	sceKernelExitDeleteThread(0);
	return 0;
}


/*
 *  Start UDP packet reception thread
 */

bool ether_start_udp_thread(int socket_fd)
{
	fd = socket_fd;
	udp_tunnel = true;
	ether_thread_active = true;
	read_thread = sceKernelCreateThread("UDP Receiver", receive_proc_udp, 0x14, 0x1800, PSP_THREAD_ATTR_USER, NULL);
	if(read_thread < 0)
        return false;
	sceKernelStartThread(read_thread, 0, NULL);
	//read_thread = spawn_thread(receive_proc_udp, "UDP Receiver", B_URGENT_DISPLAY_PRIORITY, NULL);
	//resume_thread(read_thread);
	return true;
}


/*
 *  Stop UDP packet reception thread
 */

void ether_stop_udp_thread(void)
{
	ether_thread_active = false;
	//status_t result;
	//wait_for_thread(read_thread, &result);
	sceKernelDelayThread(1*1000*1000);
}


/*
 *  Ethernet interrupt - activate deferred tasks to call IODone or protocol handlers
 */

void EtherInterrupt(void)
{
	D(bug("EtherIRQ\n"));
	EthernetPacket ether_packet;
	uint32 packet = ether_packet.addr();

	if (udp_tunnel)
	{
		ssize_t length;

		// Read packets from socket and hand to ether_udp_read() for processing
		while (true)
		{
			struct sockaddr_in from;
			socklen_t from_len = sizeof(from);
            sceKernelDelayThread(10);
			length = recvfrom(fd, Mac2HostAddr(packet), 1514, 0, (struct sockaddr *)&from, &from_len);
			if (length < 14)
				break;
			ether_udp_read(packet, length, &from);
		}

	}
	else
	{
		// Do nothing as PSP only does udptunnel
	}
	D(bug(" EtherIRQ done\n"));
}
