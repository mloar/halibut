# David Leonard <dleonard%vintela.com>, 2004.
# This file given to Simon Tatham to be released under the MIT licence
# of the Halibut distribution.
#
# SGT: I have no RedHat machine on which to test this file, so I
# cannot guarantee that it won't become out of date as the main
# Halibut code develops. It was submitted to me on 2004-11-17.

Name: halibut
Version: 0.9
Release: 1
Source: http://www.chiark.greenend.org.uk/~sgtatham/halibut/%{name}-%{version}.tar.gz
Group: Applications/Text
Summary: TeX-like software manual tool
License: MIT
URL: http://www.chiark.greenend.org.uk/~sgtatham/halibut.html
BuildRoot: %{_tmppath}/%{name}-%{version}-buildroot

%package vim
Group: Applications/Editors
Summary: Syntax file for the halibut manual tool
PreReq: vim

%description
Halibut is yet another text formatting system, intended primarily for
writing software documentation. It accepts a single source format and
outputs a variety of formats, planned to include text, HTML, Texinfo,
Windows Help, Windows HTMLHelp, PostScript and PDF. It has comprehensive
indexing and cross-referencing support, and generates hyperlinks within
output documents wherever possible.

%description vim
This package provides vim syntax support for Halibut input files (*.but).

%prep
%setup

%build
gmake VERSION="%{version}"
(cd doc && gmake)

%install
mkdir -p %{buildroot}%{_bindir}
install -m 555 build/halibut %{buildroot}%{_bindir}/halibut
mkdir -p %{buildroot}%{_mandir}/man1
install -m 444 doc/halibut.1 %{buildroot}%{_mandir}/man1/halibut.1

VIMSYNTAX=%{_prefix}/share/vim/current/syntax
mkdir -p $RPM_BUILD_ROOT/$VIMSYNTAX
install -m 444 misc/halibut.vim %{buildroot}$VIMSYNTAX/halibut.vim

%files
%{_bindir}/halibut
%{_mandir}/man1/halibut.1*
%doc doc/halibut.txt doc/*.but doc/*.html LICENCE

%files vim
%{_prefix}/share/vim/current/syntax/halibut.vim
