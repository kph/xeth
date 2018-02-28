/* XETH notifier
 *
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <net/rtnetlink.h>
#include "xeth.h"

static inline int xeth_notifier(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *nd = netdev_notifier_info_to_dev(ptr);
	int i;

	switch (event) {
	case NETDEV_UNREGISTER:
		for (i = 0; i < xeth.n.iflinks; i++) {
			struct net_device *iflink = xeth_iflinks(i);
			if (nd == iflink) {
				xeth_reset_iflinks(i);
				netdev_rx_handler_unregister(iflink);
				dev_put(iflink);
			}
		}
		break;
		/* FIXME add PRECHANGEMTU */
	}
	return NOTIFY_DONE;
}

void xeth_notifier_init(void)
{
	xeth.notifier.notifier_call = xeth_notifier;
	register_netdevice_notifier(&xeth.notifier);
}

void xeth_notifier_exit(void)
{
	unregister_netdevice_notifier(&xeth.notifier);
	xeth.notifier.notifier_call = NULL;
}