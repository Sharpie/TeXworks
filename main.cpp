/*
    This is part of TeXworks, an environment for working with TeX documents
    Copyright (C) 2007-08  Jonathan Kew

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "TWApp.h"
#include "TeXDocument.h"
#include "PDFDocument.h"

int main(int argc, char *argv[])
{
	TWApp app(argc, argv);

	// first argument is the executable name, so we skip that
	for (int i = 1; i < argc; ++i)
		app.open(argv[i]);
	
	if (TeXDocument::documentList().size() == 0 && PDFDocument::documentList().size() == 0) {
		TeXDocument *mainWin = new TeXDocument;
		mainWin->show();
	}

	return app.exec();
}
