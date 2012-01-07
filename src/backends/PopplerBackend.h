/**
 * Copyright (C) 2011-2012  Charlie Sharpsteen, Stefan Löffler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

// NOTE:
//
// ** THIS HEADER IS NOT MENT TO BE INCLUDED DIRECTLY **
//
// Instead, include `PDFBackend.h` which defines classes that this header
// relies on.
#ifndef PopplerBackend_H
#define PopplerBackend_H

#include <poppler/qt4/poppler-qt4.h>

namespace QtPDF {

class PopplerDocument;
class PopplerPage;

class PopplerDocument: public Document
{
  typedef Document Super;
  friend class PopplerPage;

  QSharedPointer<Poppler::Document> _poppler_doc;

  void recursiveConvertToC(QList<PDFToCItem> & items, QDomNode node) const;

protected:
  // Poppler is not threadsafe, so some operations need to be serialized with a
  // mutex.
  QMutex *_doc_lock;
  QList<PDFFontInfo> _fonts;
  bool _fontsLoaded;

public:
  PopplerDocument(QString fileName);
  ~PopplerDocument();

  bool isValid() const { return (_poppler_doc != NULL); }
  bool isLocked() const { return (_poppler_doc ? _poppler_doc->isLocked() : false); }

  bool unlock(const QString password);

  QSharedPointer<Page> page(int at);
  PDFDestination resolveDestination(const PDFDestination & namedDestination) const;

  PDFToC toc() const;
  QList<PDFFontInfo> fonts() const;

private:
  void parseDocument();
};


class PopplerPage: public Page
{
  typedef Page Super;
  QSharedPointer<Poppler::Page> _poppler_page;
  QList< QSharedPointer<Annotation::AbstractAnnotation> > _annotations;
  QList< QSharedPointer<Annotation::Link> > _links;
  bool _annotationsLoaded;
  bool _linksLoaded;

public:
  PopplerPage(PopplerDocument *parent, int at);
  ~PopplerPage();

  QSizeF pageSizeF() const;

  QImage renderToImage(double xres, double yres, QRect render_box = QRect(), bool cache = false);

  QList< QSharedPointer<Annotation::Link> > loadLinks();
  QList< QSharedPointer<Annotation::AbstractAnnotation> > loadAnnotations();

  QList<SearchResult> search(QString searchText);
};

} // namespace QtPDF

#endif // End header guard
// vim: set sw=2 ts=2 et

