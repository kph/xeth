// Copyright © 2018-2020 Platina Systems, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package xeth

import (
	"net"

	"github.com/platinasystems/xeth/v3/go/xeth/internal"
)

type DevKind uint8
type DevNew Xid
type DevDel Xid
type DevUp Xid
type DevDown Xid
type DevDump Xid
type DevUnreg Xid
type DevReg Xid
type DevFeatures Xid

func RxIfInfo(msg *internal.MsgIfInfo) (note interface{}) {
	xid := Xid(msg.Xid)
	note = DevDump(xid)
	l := LinkOf(xid)
	if l == nil {
		l = new(Link)
		l.xid = xid
		Links.Store(xid, l)
	}
	l.IfInfoKdata(msg.Kdata)
	if len(l.IfInfoName()) == 0 {
		note = DevNew(xid)
		name := make([]byte, internal.SizeofIfName)
		for i, c := range msg.Ifname[:] {
			if c == 0 {
				name = name[:i]
				break
			} else {
				name[i] = byte(c)
			}
		}
		l.IfInfoName(string(name))
		l.IfInfoDevKind(DevKind(msg.Kind))
		ha := make(net.HardwareAddr, internal.SizeofEthAddr)
		copy(ha, msg.Addr[:])
		l.IfInfoHardwareAddr(ha)
	}
	l.IfInfoIfIndex(msg.Ifindex)
	l.IfInfoNetNs(NetNs(msg.Net))
	l.IfInfoFlags(net.Flags(msg.Flags))
	l.IfInfoFeatures(msg.Features)
	return note
}

func (xid Xid) RxUp() DevUp {
	up := DevUp(xid)
	if l := expectLinkOf(xid, "admin-up"); l != nil {
		flags := l.IfInfoFlags()
		flags |= net.FlagUp
		l.IfInfoFlags(flags)
	}
	return up
}

func (xid Xid) RxDown() DevDown {
	down := DevDown(xid)
	if l := expectLinkOf(xid, "admin-down"); l != nil {
		flags := l.IfInfoFlags()
		flags &^= net.FlagUp
		l.IfInfoFlags(flags)
	}
	return down
}

func (xid Xid) RxReg(netns NetNs, ifindex int32) DevReg {
	reg := DevReg(xid)
	if l := LinkOf(xid); l != nil {
		l.IfInfoNetNs().Xid(l.IfInfoIfIndex(), 0)
		l.IfInfoNetNs(netns)
		l.IfInfoIfIndex(ifindex)
	}
	netns.Xid(ifindex, xid)
	return reg
}

func (xid Xid) RxUnreg(newIfindex int32) (unreg DevUnreg) {
	unreg = DevUnreg(xid)
	if l := expectLinkOf(xid, "RxUnreg"); l != nil {
		l.IfInfoNetNs().Xid(l.IfInfoIfIndex(), 0)
		l.IfInfoNetNs(DefaultNetNs)
		l.IfInfoIfIndex(newIfindex)
	}
	return unreg
}

func (xid Xid) RxFeatures(features uint64) (note DevFeatures) {
	note = DevFeatures(xid)
	if l := expectLinkOf(xid, "RxFeatures"); l != nil {
		l.IfInfoFeatures(features)
	}
	return note
}
