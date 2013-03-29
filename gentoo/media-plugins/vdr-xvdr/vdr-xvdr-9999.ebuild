# Copyright 1999-2013 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI="5"

inherit vdr-plugin-2 git-2 flag-o-matic

EGIT_REPO_URI="git://github.com/pipelka/vdr-plugin-xvdr.git"

DESCRIPTION="VDR plugin: XVDR Streamserver Plugin"
HOMEPAGE="https://github.com/pipelka/vdr-plugin-xvdr"
SRC_URI=""
KEYWORDS=""
LICENSE="GPL-2"
SLOT="0"
IUSE="debug tools"

DEPEND=">=media-video/vdr-1.6"
RDEPEND="${DEPEND}"

S="${WORKDIR}/${PN}-plugin"

src_prepare() {
	vdr-plugin-2_src_prepare

	fix_vdr_libsi_include "${S}"/src/live/livepatfilter.h
}

src_compile() {
	if use debug; then
		BUILD_PARAMS="DEBUG=1"
		append-flags -g
	fi

	vdr-plugin-2_src_compile

	if use tools ; then
		cd "${S}/tools"
		emake || die "emake failed for tools"
	fi
}

src_install() {
	vdr-plugin-2_src_install

	insinto /etc/vdr/plugins/xvdr
	doins xvdr/*.conf
	diropts -gvdr -ovdr

	if use tools ; then
		exeinto /usr/bin
		newexe "${S}/tools/serviceref" xvdr-serviceref
	fi
}

pkg_postinst() {
	vdr-plugin-2_pkg_postinst

	if use tools ; then
		elog
		elog "The 'serviceref' tool has been installed as"
		elog "\t/usr/bin/xvdr-serviceref"
	fi
}
