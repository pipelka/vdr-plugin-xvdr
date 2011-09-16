# Copyright 1999-2010 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=2

RESTRICT="strip"

inherit vdr-plugin git-2

EGIT_REPO_URI="git://github.com/pipelka/vdr-plugin-xvdr.git"
PATCHES=""

SRC_URI=""
S=${WORKDIR}/${PN}

DESCRIPTION="VDR plugin: XVDR Streamserver Plugin"
HOMEPAGE="https://github.com/pipelka/vdr-plugin-xvdr"

LICENSE="GPL-2"
SLOT="0"

KEYWORDS="~x86 ~amd64"
IUSE=""

DEPEND=">=media-video/vdr-1.6"
RDEPEND="${DEPEND}"

src_prepare() {
        vdr-plugin_src_prepare

        fix_vdr_libsi_include recplayer.c
        fix_vdr_libsi_include receiver.c
}

src_install() {
        vdr-plugin_src_install

        insinto /etc/vdr/plugins/xvdr
        doins xvdr/allowed_hosts.conf
        diropts -gvdr -ovdr
}

