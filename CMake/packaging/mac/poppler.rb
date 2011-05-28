# This file contains a formula for installing Poppler on Mac OS X using the
# Homebrew package manager:
#
#     http://mxcl.github.com/homebrew
#
# To install Poppler using this formula:
#
#     brew install --with-qt4 --enable-xpdf-headers path/to/this/poppler.rb
#
# Changes compared to Homebrew's standard Poppler formula:
#
#   - TeXworks-specific patches are applied to help Qt apps find the
#     poppler-data directory.
#
#   - Poppler is patched to use native OS X libraries for font handling instead
#     of Fontconfig. This removes any dependencies on the presence of X11
#     fonts.
#
#   - Poppler is configured to use as few dependencies as possible. This
#     reduces the number of dylibs that must be added to TeXworks.app when it
#     is packaged for distribution.
TEXWORKS_SOURCE_DIR = Pathname.new(__FILE__).realpath.dirname.join('../../..')
TEXWORKS_PATCH_DIR = TEXWORKS_SOURCE_DIR + 'lib-patches'

require 'formula'

class PopplerData < Formula
  url 'http://poppler.freedesktop.org/poppler-data-0.4.4.tar.gz'
  md5 'f3a1afa9218386b50ffd262c00b35b31'
end

class Poppler < Formula
  url 'http://poppler.freedesktop.org/poppler-0.16.5.tar.gz'
  homepage 'http://poppler.freedesktop.org/'
  md5 '2b6e0c26b77a943df3b9bb02d67ca236'

  depends_on 'pkg-config' => :build
  depends_on "qt" if ARGV.include? "--with-qt4"

  def patches
    {
      :p1 => [
        TEXWORKS_PATCH_DIR + 'poppler-qt4-globalparams.patch',
        TEXWORKS_PATCH_DIR + 'poppler-mac-remove-iconv.patch',
        TEXWORKS_PATCH_DIR + 'poppler-mac-font-handling.patch',
        TEXWORKS_PATCH_DIR + 'poppler-debug-mac-font-handling.patch'
      ]
    }
  end

  def options
    [
      ["--with-qt4", "Include Qt4 support (which compiles all of Qt4!)"],
      ["--enable-xpdf-headers", "Also install XPDF headers."]
    ]
  end

  def install
    if ARGV.include? "--with-qt4"
      ENV['POPPLER_QT4_CFLAGS'] = `pkg-config QtCore QtGui --libs`.chomp.strip
      ENV.append 'LDFLAGS', "-Wl,-F#{HOMEBREW_PREFIX}/lib"
    end

    # Need to re-generate configure script to take advantage of a patch that
    # adds native Mac font handling.
    ENV['ACLOCAL'] = "/usr/bin/aclocal -I#{HOMEBREW_PREFIX}/share/aclocal -I/usr/share/aclocal"
    ENV['AUTOMAKE'] = '/usr/bin/automake --foreign --add-missing --ignore-deps'
    system 'touch config.rpath'
    system '/usr/bin/autoreconf -fi'

    args = ["--disable-dependency-tracking", "--prefix=#{prefix}"]
    args << "--disable-poppler-qt4" unless ARGV.include? "--with-qt4"
    args << "--enable-xpdf-headers" if ARGV.include? "--enable-xpdf-headers"

    # TeXworks-specific additions to minimize library dependencies
    args.concat [
      '--disable-libpng',
      '--disable-libjpeg',
      '--disable-cms',
      '--disable-abiword-output',
      '--disable-cairo-output',
      '--disable-poppler-glib',
      '--disable-poppler-cpp',
      '--disable-gdk',
      '--disable-utils',
      '--without-x',
      '--enable-splash-output' # Required for proper font handling
    ]

    system "./configure", *args
    system "make install"

    # Install poppler font data.
    PopplerData.new.brew do
      system "make install prefix=#{prefix}"
    end
  end
end
