# Copyright 2026 seaslug
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit git-r3 toolchain-funcs

DESCRIPTION="A TUI file manager designed to remove the need for GUI alternatives"
HOMEPAGE="https://github.com/seaslugr/quickfish"
EGIT_REPO_URI="https://github.com/seaslugr/quickfish.git"

LICENSE="GPL-2"
SLOT="0"
IUSE="test"
RESTRICT="!test? ( test )"

DEPEND="sys-libs/ncurses:=[unicode(+)]"
RDEPEND="
	${DEPEND}
	x11-misc/xdg-utils
"
BDEPEND="virtual/pkgconfig"

src_compile() {
	emake CC="$(tc-getCC)" PKG_CONFIG="$(tc-getPKG_CONFIG)" build/release/quickfish
}

src_test() {
	emake CC="$(tc-getCC)" PKG_CONFIG="$(tc-getPKG_CONFIG)" test
}

src_install() {
	dobin build/release/quickfish
	dodoc README.md docs/keybinds.md
}

pkg_postinst() {
	elog "Install sys-apps/bat for syntax highlighted file previews."
}
