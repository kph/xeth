/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#ifndef __NET_ETHERNET_XETH_BRIDGE_H
#define __NET_ETHERNET_XETH_BRIDGE_H

#include <net/rtnetlink.h>

extern struct rtnl_link_ops xeth_bridge_lnko;

static inline bool is_xeth_bridge(struct net_device *nd)
{
	return nd->rtnl_link_ops == &xeth_bridge_lnko;
}

#endif /* __NET_ETHERNET_XETH_BRIDGE_H */
