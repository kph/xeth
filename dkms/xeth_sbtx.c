/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

#include <net/nexthop.h>

static void xeth_sbtx_msg_set(void *data, enum xeth_msg_kind kind)
{
	struct xeth_msg *msg = data;
	msg->header.z64 = 0;
	msg->header.z32 = 0;
	msg->header.z16 = 0;
	msg->header.version = XETH_MSG_VERSION;
	msg->header.kind = kind;
}

static inline u64 xeth_sbtx_ns_inum(struct net_device *nd)
{
	struct net *ndnet = dev_net(nd);
	return net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
}

int xeth_sbtx_break(struct net_device *mux)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_break *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_BREAK);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_change_upper(struct net_device *mux, u32 upper_xid, u32 lower_xid,
			   bool linking)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_change_upper_xid *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_CHANGE_UPPER_XID);
	msg->upper = upper_xid;
	msg->lower = lower_xid;
	msg->linking = linking ? 1 : 0;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_et_flags(struct net_device *mux, u32 xid, u32 flags)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_flags *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_ETHTOOL_FLAGS);
	msg->xid = xid;
	msg->flags = flags;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

static int xeth_sbtx_et_link_modes(struct net_device *mux,
				   enum xeth_msg_kind kind, u32 xid,
				   const volatile unsigned long *addr)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_link_modes *msg;
	int bit;
	const unsigned bits = min(__ETHTOOL_LINK_MODE_MASK_NBITS, 64);

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, kind);
	msg->xid = xid;
	for (bit = 0; bit < bits; bit++)
		if (test_bit(bit, addr))
			msg->modes |= 1ULL<<bit;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

/* Note, this sends speed 0 if autoneg, regardless of base.speed This is to
 * cover controller (e.g. vnet) restart where in it's earlier run it has sent
 * SPEED to note the auto-negotiated speed to ethtool user, but in subsequent
 * run, we don't want the controller to override autoneg.
 */
int xeth_sbtx_et_settings(struct net_device *mux, u32 xid,
			  const struct ethtool_link_ksettings *ks)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ethtool_settings *msg;
	const enum xeth_msg_kind kadv =
		XETH_MSG_KIND_ETHTOOL_LINK_MODES_ADVERTISING;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_ETHTOOL_SETTINGS);
	msg->xid = xid;
	msg->speed = ks->base.autoneg ?  0 : ks->base.speed;
	msg->duplex = ks->base.duplex;
	msg->port = ks->base.port;
	msg->phy_address = ks->base.phy_address;
	msg->autoneg = ks->base.autoneg;
	msg->mdio_support = ks->base.mdio_support;
	msg->eth_tp_mdix = ks->base.eth_tp_mdix;
	msg->eth_tp_mdix_ctrl = ks->base.eth_tp_mdix_ctrl;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return xeth_sbtx_et_link_modes(mux, kadv, xid,
				       ks->link_modes.advertising);
}

static const char * const xeth_sbtx_fib_event_names[] = {
	[FIB_EVENT_ENTRY_REPLACE] "replace",
	[FIB_EVENT_ENTRY_APPEND] "append",
	[FIB_EVENT_ENTRY_ADD] "add",
	[FIB_EVENT_ENTRY_DEL] "del",
};

int xeth_sbtx_fib_entry(struct net_device *mux, struct net *net,
			struct fib_entry_notifier_info *feni,
			unsigned long event)
{
	int i, nhs;
	struct xeth_sbtxb *sbtxb;
	struct xeth_next_hop *nh;
	struct xeth_msg_fibentry *msg;
	size_t n = sizeof(*msg);

	nhs = fib_info_num_path(feni->fi);
	if (nhs > 0)
		n += (nhs * sizeof(struct xeth_next_hop));
	sbtxb = xeth_mux_alloc_sbtxb(mux, n);
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	nh = (typeof(nh))&msg->nh[0];
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_FIBENTRY);
	msg->net = net_eq(net, &init_net) ? 1 : net->ns.inum;
	msg->address = htonl(feni->dst);
	msg->mask = inet_make_mask(feni->dst_len);
	msg->event = (u8)event;
	msg->nhs = nhs;
	msg->tos = feni->tos;
	msg->type = feni->type;
	msg->table = feni->tb_id;
	rcu_read_lock();
	for(i = 0; i < msg->nhs; i++) {
		struct fib_nh_common *nhc = fib_info_nhc(feni->fi, i);
		nh[i].ifindex = nhc->nhc_dev ? nhc->nhc_dev->ifindex : 0;
		nh[i].weight = nhc->nhc_weight;
		nh[i].flags = nhc->nhc_flags;
		nh[i].gw = nhc->nhc_gw.ipv4;
		nh[i].scope = nhc->nhc_scope;
	}
	rcu_read_unlock();
	no_xeth_debug("%s %pI4/%d w/ %d nexhop(s)",
		      xeth_sbtx_fib_event_names[event],
		      &msg->address, feni->dst_len, nhs);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_fib6_nh_entry(struct net_device *mux, struct net *net,
			    struct fib6_entry_notifier_info *feni,
			    struct fib6_info *f6i, unsigned long event)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_fib6entry *msg;
	struct xeth_next_hop6 *sibling;
	struct nh_group *nhg;
	struct nh_grp_entry *nhge;
	struct nh_info *nhi;
	struct fib_nh_common *nhc;
	size_t i, sz = sizeof(*msg), nsiblings = 0;

	if (f6i->nh->is_group) {
		nhg = rcu_dereference_rtnl(f6i->nh->nh_grp);
		if (nhg->mpath) {
			if (nhg->num_nh > 0) {
				nsiblings = nhg->num_nh - 1;
				sz += nsiblings * sizeof(*sibling);
			}
		}
	}
	sbtxb = xeth_mux_alloc_sbtxb(mux, sz);
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_FIB6ENTRY);
	msg->net = net_eq(net, &init_net) ? 1 : net->ns.inum;
	memcpy(msg->address, &f6i->fib6_dst.addr, 16);
	msg->length = f6i->fib6_dst.plen;
	msg->event = (u8)event;
	msg->nsiblings = nsiblings;
	msg->type = f6i->fib6_type;
	msg->table = f6i->fib6_table->tb6_id;
	if (f6i->nh->is_group) {
		for (i = 0, sibling = &msg->siblings[0]; i < nhg->num_nh; i++) {
			nhge = &nhg->nh_entries[i];
			nhi = rcu_dereference_rtnl(nhge->nh->nh_info);
			nhc = &nhi->fib_nhc;
			if (i == 0) {
				msg->nh.ifindex = nhc->nhc_dev ?
					nhc->nhc_dev->ifindex : 0;
				msg->nh.weight = nhc->nhc_weight;
				msg->nh.flags = nhc->nhc_flags;
				memcpy(msg->nh.gw, &nhc->nhc_gw.ipv6, 16);
			} else {
				sibling->ifindex = nhc->nhc_dev ?
					nhc->nhc_dev->ifindex : 0;
				sibling->weight = nhc->nhc_weight;
				sibling->flags = nhc->nhc_flags;
				memcpy(sibling->gw, &nhc->nhc_gw.ipv6, 16);
				sibling++;
			}
		}
	} else {
		nhi = rcu_dereference_rtnl(f6i->nh->nh_info);
		nhc = &nhi->fib_nhc;
		msg->nh.ifindex = nhc->nhc_dev ? nhc->nhc_dev->ifindex : 0;
		msg->nh.weight = nhc->nhc_weight;
		msg->nh.flags = nhc->nhc_flags;
		memcpy(msg->nh.gw, &nhc->nhc_gw.ipv6, 16);
	}
	xeth_debug("fib6 %s %pI6c/%d w/ %zd nexthop(s)",
		   xeth_sbtx_fib_event_names[event], msg->address, msg->length,
		   1 + nsiblings);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_fib6_entry(struct net_device *mux, struct net *net,
			 struct fib6_entry_notifier_info *feni,
			 unsigned long event)
{
	struct fib6_info *f6i = xeth_nd_prif_ptr_err(mux, feni->rt);
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_fib6entry *msg;
	struct fib6_info *iter;
	struct fib6_nh *nh;
	size_t sz = sizeof(*msg), nsiblings;

	if (IS_ERR(f6i))
		return PTR_ERR(f6i);
	if (f6i->nh)
		return xeth_sbtx_fib6_nh_entry(mux, net, feni, f6i, event);
	nsiblings = f6i->fib6_nsiblings;
	if (nsiblings > 0)
		sz += nsiblings * sizeof(struct xeth_next_hop6 *);
	sbtxb = xeth_mux_alloc_sbtxb(mux, sz);
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_FIB6ENTRY);
	msg->net = net_eq(net, &init_net) ? 1 : net->ns.inum;
	memcpy(msg->address, &f6i->fib6_dst.addr, 16);
	msg->length = f6i->fib6_dst.plen;
	msg->event = (u8)event;
	msg->nsiblings = nsiblings;
	msg->type = f6i->fib6_type;
	msg->table = f6i->fib6_table->tb6_id;
	nh = f6i->fib6_nh;
	msg->nh.ifindex = nh->fib_nh_dev ? nh->fib_nh_dev->ifindex : 0;
	msg->nh.weight = nh->fib_nh_weight;
	msg->nh.flags = nh->fib_nh_flags;
	memcpy(msg->nh.gw, &nh->fib_nh_gw6, 16);
	if (nsiblings > 0) {
		int i = 0;
		struct xeth_next_hop6 *sibling = &msg->siblings[0];
		rcu_read_lock();
		list_for_each_entry(iter, &f6i->fib6_siblings, fib6_siblings) {
			if (i >= nsiblings)
				break;
			sibling->ifindex = iter->fib6_nh->fib_nh_dev ?
				iter->fib6_nh->fib_nh_dev->ifindex : 0;
			sibling->weight = iter->fib6_nh->fib_nh_weight;
			sibling->flags = iter->fib6_nh->fib_nh_flags;
			memcpy(sibling->gw, &iter->fib6_nh->fib_nh_gw6, 16);
			i++;
			sibling++;
		}
		rcu_read_unlock();
	}
	no_xeth_debug("fib6 %s %pI6c/%d w/ %zd nexthop sibling(s)",
		      xeth_sbtx_fib_event_names[event],
		      msg->address, msg->length,
		      nsiblings);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifa(struct net_device *mux, struct in_ifaddr *ifa,
		  unsigned long event, u32 xid)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifa *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFA);
	msg->xid = xid;
	msg->event = event;
	msg->address = ifa->ifa_address;
	msg->mask = ifa->ifa_mask;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifa6(struct net_device *mux, struct inet6_ifaddr *ifa6,
		   unsigned long event, u32 xid)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifa6 *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFA6);
	msg->xid = xid;
	msg->event = event;
	memcpy(msg->address, &ifa6->addr, 16);
	msg->length = ifa6->prefix_len;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_ifinfo(struct xeth_proxy *proxy, unsigned iff,
		     enum xeth_msg_ifinfo_reason reason)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_ifinfo *msg;

	if (!proxy->xid || !proxy->mux)
		return 0;
	sbtxb = xeth_mux_alloc_sbtxb(proxy->mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_IFINFO);
	strlcpy(msg->ifname, proxy->nd->name, IFNAMSIZ);
	msg->net = xeth_sbtx_ns_inum(proxy->nd);
	msg->ifindex = proxy->nd->ifindex;
	msg->xid = proxy->xid;
	switch (proxy->kind) {
	case XETH_DEV_KIND_UNSPEC:
	case XETH_DEV_KIND_PORT:
		break;
	case XETH_DEV_KIND_VLAN:
		msg->kdata = xeth_mux_encap(proxy->mux);
		break;
	case XETH_DEV_KIND_BRIDGE:
	case XETH_DEV_KIND_LAG:
		break;
	case XETH_DEV_KIND_LB:
		msg->kdata = xeth_lb_chan(proxy->nd);
		break;
	}
	msg->flags = iff ? iff : proxy->nd->flags;
	memcpy(msg->addr, proxy->nd->dev_addr, ETH_ALEN);
	msg->kind = proxy->kind;
	msg->reason = reason;
	msg->features = proxy->nd->features;
	xeth_mux_queue_sbtx(proxy->mux, sbtxb);
	return 0;
}

int xeth_sbtx_neigh_update(struct net_device *mux, struct neighbour *neigh)
{
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_neigh_update *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, XETH_MSG_KIND_NEIGH_UPDATE);
	msg->ifindex = neigh->dev->ifindex;
	msg->net = xeth_sbtx_ns_inum(neigh->dev);
	msg->ifindex = neigh->dev->ifindex;
	msg->family = neigh->ops->family;
	msg->len = neigh->tbl->key_len;
	memcpy(msg->dst, neigh->primary_key, neigh->tbl->key_len);
	read_lock_bh(&neigh->lock);
	if ((neigh->nud_state & NUD_VALID) && !neigh->dead) {
		char ha[MAX_ADDR_LEN];
		neigh_ha_snapshot(ha, neigh, neigh->dev);
		if ((neigh->nud_state & NUD_VALID) && !neigh->dead)
			memcpy(&msg->lladdr[0], ha, ETH_ALEN);
	}
	read_unlock_bh(&neigh->lock);
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}

int xeth_sbtx_netns(struct net_device *mux, struct net *ndnet, bool add)
{
	uint64_t net = net_eq(ndnet, &init_net) ? 1 : ndnet->ns.inum;
	struct xeth_sbtxb *sbtxb;
	struct xeth_msg_netns *msg;

	sbtxb = xeth_mux_alloc_sbtxb(mux, sizeof(*msg));
	if (!sbtxb)
		return -ENOMEM;
	msg = xeth_sbtxb_data(sbtxb);
	xeth_sbtx_msg_set(msg, add ?
			  XETH_MSG_KIND_NETNS_ADD : XETH_MSG_KIND_NETNS_DEL);
	msg->net = net;
	xeth_mux_queue_sbtx(mux, sbtxb);
	return 0;
}
