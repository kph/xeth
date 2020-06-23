/**
 * Platina Systems MK1 top of rack switch.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

enum {
	xeth_platina_mk1_top_xid = 3999,
	xeth_platina_mk1_n_ports = 32,
	xeth_platina_mk1_n_rxqs = 1,
	xeth_platina_mk1_n_txqs = 1,
};

static int xeth_platina_mk1_provision[xeth_platina_mk1_n_ports];
static int xeth_platina_mk1_n_provision;
module_param_array_named(platina_mk1_provision,
			 xeth_platina_mk1_provision, int,
			 &xeth_platina_mk1_n_provision, 0644);
MODULE_PARM_DESC(platina_mk1_provision,
		 "*1, 2, or 4 subports per port");

static int xeth_platina_mk1_init(void);
static void xeth_platina_mk1_exit(void);
static void xeth_platina_mk1_et_port_cb(struct ethtool_link_ksettings *ks);
static void xeth_platina_mk1_et_subport_cb(struct ethtool_link_ksettings *ks);

static const char * const xeth_platina_mk1_eth1_akas[] = {
	"eth1", "enp3s0f0", NULL
};

static const char * const xeth_platina_mk1_eth2_akas[] = {
	"eth2", "enp3s0f1", NULL
};

static const char * const * const xeth_platina_mk1_lower_akas[] = {
	xeth_platina_mk1_eth1_akas,
	xeth_platina_mk1_eth2_akas,
	NULL,
};

enum {
      xeth_platina_mk1_lower_list_sz = ARRAY_SIZE(xeth_platina_mk1_lower_akas),
};

struct net_device *xeth_platina_mk1_lowers[xeth_platina_mk1_lower_list_sz];

static const char * const xeth_platina_mk1_et_flag_names[] = {
	"copper",
	"fec74",
	"fec91",
	NULL,
};

static const int xeth_platina_mk1_qsfp_bus[] = {
	 2,  3,  4,  5,  6,  7,  8,  9,
	11, 12, 13, 14, 15, 16, 16, 18,
	20, 21, 22, 23, 24, 25, 26, 27,
	29, 30, 31, 32, 33, 34, 35, 36,
	-1,
};

int xeth_platina_mk1_probe(struct pci_dev *pci_dev,
		       const struct pci_device_id *id)
{
	int i, j;

	xeth_debug("vendor 0x%x, device 0x%x", id->vendor, id->device);

	for (i = 0; i < ARRAY_SIZE(xeth_platina_mk1_provision); i++) {
		if (i >= xeth_platina_mk1_n_provision) {
			xeth_platina_mk1_provision[i] = 1;
		} else {
			int n = xeth_platina_mk1_provision[i];
			if (n != 1 && n != 2 && n != 4)
				return -EINVAL;
		}
	}

	for (i = 0; xeth_platina_mk1_lower_akas[i]; i++) {
		for (j = 0; !xeth_platina_mk1_lowers[i]; j++) {
			const char *aka = xeth_platina_mk1_lower_akas[i][j];
			if (!aka)
				return -EPROBE_DEFER;
			xeth_platina_mk1_lowers[i] =
				dev_get_by_name(&init_net, aka);
		}
	}

	xeth_vendor_init = xeth_platina_mk1_init;
	xeth_vendor_exit = xeth_platina_mk1_exit;
	xeth_vendor_lowers = xeth_platina_mk1_lowers;

	xeth_upper_set_et_flag_names(xeth_platina_mk1_et_flag_names);

	xeth_upper_eto_get_module_info = xeth_qsfp_get_module_info;
	xeth_upper_eto_get_module_eeprom = xeth_qsfp_get_module_eeprom;

	return 0;
}

/* If successful, this returns -EBUSY to release the device for vfio_pci */
static int xeth_platina_mk1_init(void)
{
	u64 ea;
	u8 first_port;
	int port, subport, err = 0;
	char name[IFNAMSIZ];
	struct net_device *nd;

	xeth_qsfp_bus = xeth_platina_mk1_qsfp_bus;
	ea = xeth_onie_mac_base();
	if (ea)		/* 1st port after bmc, eth0, eth1, and eth2 */
		ea += 4;
	first_port = xeth_onie_device_version() > 0 ? 1 : 0;
	for (port = 0; err >= 0 && port < xeth_platina_mk1_n_ports; port++) {
		int qsfp_bus = xeth_platina_mk1_qsfp_bus[port];
		if (xeth_platina_mk1_provision[port] > 1) {
			void (*cb)(struct ethtool_link_ksettings *) =
				xeth_platina_mk1_et_subport_cb;
			for (subport = 0;
			     subport < xeth_platina_mk1_provision[port] &&
				     err >= 0;
			     subport++) {
				u32 o = xeth_platina_mk1_n_ports * subport;
				u32 xid = xeth_platina_mk1_top_xid - port - o;
				u64 pea = ea ? ea + port + o : 0;
				scnprintf(name, IFNAMSIZ, "xeth%d-%d",
					  port + first_port,
					  subport + first_port);
				nd = new_xeth_upper(name, xid, pea, cb);
				if (IS_ERR(nd))
					err = PTR_ERR(nd);
				else if (subport == 0)
					xeth_upper_set_qsfp_bus(nd, qsfp_bus);
			}
		} else {
			u32 xid = xeth_platina_mk1_top_xid - port;
			u64 pea = ea ? ea + port : 0;
			void (*cb)(struct ethtool_link_ksettings *) =
				xeth_platina_mk1_et_port_cb;
			scnprintf(name, IFNAMSIZ, "xeth%d", port + first_port);
			nd = new_xeth_upper(name, xid, pea, cb);
			if (IS_ERR(nd))
				err = PTR_ERR(nd);
			else
				xeth_upper_set_qsfp_bus(nd, qsfp_bus);
		}
	}
	if (err < 0)
		return (int)err;
	err = xeth_qsfp_register_driver();
	if (err < 0)
		return (int)err;
	return -EBUSY;
}

static void xeth_platina_mk1_exit(void)
{
	int port, subport;

	xeth_qsfp_unregister_driver();

	for (port = 0; port < xeth_platina_mk1_n_ports; port++) {
		if (xeth_platina_mk1_provision[port] > 1) {
			for (subport = 0;
			     subport < xeth_platina_mk1_provision[port];
			     subport++) {
				u32 o = xeth_platina_mk1_n_ports * subport;
				u32 xid = xeth_platina_mk1_top_xid - port - o;
				xeth_upper_delete_port(xid);
			}
		} else
			xeth_upper_delete_port(xeth_platina_mk1_top_xid - port);
	}
}

static void xeth_platina_mk1_et_port_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 100000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported,
					     100000baseLR4_ER4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
	/* disable FEC_NONE so that the vner-platina-mk1 interprets
	 * FEC_RS|FEC_BASER as FEC_AUTO */
	ethtool_link_ksettings_del_link_mode(ks, advertising, FEC_NONE);
}

static void xeth_platina_mk1_et_subport_cb(struct ethtool_link_ksettings *ks)
{
	ks->base.speed = 0;
	ks->base.duplex = DUPLEX_FULL;
	ks->base.autoneg = AUTONEG_ENABLE;
	ks->base.port = PORT_OTHER;
	ethtool_link_ksettings_zero_link_mode(ks, supported);
	ethtool_link_ksettings_add_link_mode(ks, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKX4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 10000baseR_FEC);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseMLD2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 20000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseCR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseKR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 25000baseSR_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseKR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseCR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseSR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 40000baseLR4_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseCR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseKR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, 50000baseSR2_Full);
	ethtool_link_ksettings_add_link_mode(ks, supported, TP);
	ethtool_link_ksettings_add_link_mode(ks, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_NONE);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_RS);
	ethtool_link_ksettings_add_link_mode(ks, supported, FEC_BASER);
	bitmap_copy(ks->link_modes.advertising, ks->link_modes.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
}
