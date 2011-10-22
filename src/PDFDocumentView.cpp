/**
 * Copyright (C) 2011  Charlie Sharpsteen, Stefan Löffler
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
#include "PDFDocumentView.h"
#ifdef DEBUG
#include <QDebug>
QTime stopwatch;
#endif

// Some utility functions.
//
// **TODO:** _Find a better place to put these._
static bool isPageItem(QGraphicsItem *item) { return ( item->type() == PDFPageGraphicsItem::Type ); }

// PDFDocumentView
// ===============

// This class descends from `QGraphicsView` and is responsible for controlling
// and displaying the contents of a `Document` using a `QGraphicsScene`.
PDFDocumentView::PDFDocumentView(QWidget *parent):
  Super(parent),
  _rubberBandOrigin(),
  _zoomLevel(1.0),
  _pageMode(PageMode_OneColumnContinuous),
  _mouseMode(MouseMode_Move),
  _armedTool(Tool_None),
  _activeTool(Tool_None)
{
  setBackgroundRole(QPalette::Dark);
  setAlignment(Qt::AlignCenter);
  setFocusPolicy(Qt::StrongFocus);

  // If _currentPage is not set to -1, the compiler may default to 0. In that
  // case, `goFirst()` or `goToPage(0)` will fail because the view will think
  // it is already looking at page 0.
  _currentPage = -1;
  _magnifier = new PDFDocumentMagnifierView(this);
  _rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
  // We deliberately set the mouse mode to a different value above so we can
  // call setMouseMode (which bails out if the mouse mode is not changed), which
  // in turn sets up other variables such as _toolAccessors
  setMouseMode(MouseMode_MagnifyingGlass);
}


// Accessors
// ---------
void PDFDocumentView::setScene(PDFDocumentScene *a_scene)
{
  Super::setScene(a_scene);

  _pdf_scene = a_scene;
  _lastPage = a_scene->lastPage();

  // Respond to page jumps requested by the `PDFDocumentScene`.
  //
  // **TODO:**
  // _May want to consider not doing this by default. It is conceivable to have
  // a View that would ignore page jumps that other scenes would respond to._
  connect(a_scene, SIGNAL(pageChangeRequested(int)), this, SLOT(goToPage(int)));
  connect(_pdf_scene, SIGNAL(pdfActionTriggered(const PDFAction*)), this, SLOT(pdfActionTriggered(const PDFAction*)));
}
int PDFDocumentView::currentPage() { return _currentPage; }
int PDFDocumentView::lastPage()    { return _lastPage; }

void PDFDocumentView::setPageMode(PageMode pageMode)
{
  if (!_pdf_scene || pageMode == _pageMode)
    return;

  // Save the current view relative to the current page so we can restore it
  // after changing the mode
  // **TODO:** Safeguard
  QRectF viewRect(mapToScene(viewport()->rect()).boundingRect());
  viewRect.translate(-_pdf_scene->pageAt(_currentPage)->pos());

  // **TODO:** Avoid relayouting everything twice when switching from SinglePage
  // to TwoColumnContinuous (once by setContinuous(), and a second time by
  // setColumnCount() below)
  switch (pageMode) {
    case PageMode_SinglePage:
      _pdf_scene->showOnePage(_currentPage);
      _pdf_scene->pageLayout().setContinuous(false);
      break;
    case PageMode_OneColumnContinuous:
      if (_pageMode == PageMode_SinglePage) {
        _pdf_scene->pageLayout().setContinuous(true);
        _pdf_scene->showAllPages();
        // Reset the scene rect; causes it the encompass the whole scene
        setSceneRect(QRectF());
      }
      _pdf_scene->pageLayout().setColumnCount(1, 0);
      break;
    case PageMode_TwoColumnContinuous:
      if (_pageMode == PageMode_SinglePage) {
        _pdf_scene->pageLayout().setContinuous(true);
        _pdf_scene->showAllPages();
        // Reset the scene rect; causes it the encompass the whole scene
        setSceneRect(QRectF());
      }
      _pdf_scene->pageLayout().setColumnCount(2, 1);
      break;
  }
  _pageMode = pageMode;
  _pdf_scene->pageLayout().relayout();

  // We might need to update the scene rect (when switching to single page mode)
  maybeUpdateSceneRect();

  // Restore the view from before as good as possible
  viewRect.translate(_pdf_scene->pageAt(_currentPage)->pos());
  ensureVisible(viewRect, 0, 0);
}

PDFToCDockWidget* PDFDocumentView::tocDockWidget(QWidget * parent)
{
  PDFToCDockWidget * dock = new PDFToCDockWidget(parent);
  connect(dock, SIGNAL(actionTriggered(const PDFAction*)), this, SLOT(pdfActionTriggered(const PDFAction*)));
  if (_pdf_scene && _pdf_scene->document())
    dock->setToCData(_pdf_scene->document()->toc());

  return dock;
}


// Public Slots
// ------------

void PDFDocumentView::goPrev()  { goToPage(_currentPage - 1, false); }
void PDFDocumentView::goNext()  { goToPage(_currentPage + 1); }
void PDFDocumentView::goFirst() { goToPage(0); }
void PDFDocumentView::goLast()  { goToPage(_lastPage - 1); }

// `goToPage` will shift the view to a different page. If the `centerOnTop`
// parameter is `true` (the default), than the view will ensure the top left
// corner of the page is visible. Otherwise, the bottom left corner will be
// used.
//
// **TODO:**
//
//   - Be more flexible about centering. Perhaps allow a point in page
//     coordinates to be passed in along with the choice of top/bottom. This
//     could be useful for jumping to a page and centering on a search result
//     or link anchor.
//
//   - Overload this function to take `PDFPageGraphicsItem` as a parameter?
void PDFDocumentView::goToPage(int pageNum, bool centerOnTop /* = true */)
{
  if (!_pdf_scene || _pdf_scene->pages().size() <= pageNum || !_pdf_scene->pageAt(pageNum))
    return;
  // We silently ignore any invalid page numbers.
  if ( (pageNum >= 0) && (pageNum < _lastPage) && (pageNum != _currentPage) )
  {
    if (_pageMode == PageMode_SinglePage) {
      _pdf_scene->showOnePage(pageNum);
      maybeUpdateSceneRect();
    }

    QRectF pageRect = _pdf_scene->pageAt(pageNum)->sceneBoundingRect();

    // **TODO:** Investigate why this approach doesn't work during startup if
    // the margin is set to 0
    if (centerOnTop)
      ensureVisible(pageRect.left(), pageRect.top(), 1, 1);
    else
      ensureVisible(pageRect.left(), pageRect.bottom(), 1, 1);

    _currentPage = pageNum;
    emit changedPage(_currentPage);
  }
}

void PDFDocumentView::zoomBy(qreal zoomFactor)
{
  _zoomLevel *= zoomFactor;
  // Set the transformation anchor to AnchorViewCenter so we always zoom out of
  // the center of the view (rather than out of the upper left corner)
  QGraphicsView::ViewportAnchor anchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  this->scale(zoomFactor, zoomFactor);
  setTransformationAnchor(anchor);

  emit changedZoom(_zoomLevel);
}

void PDFDocumentView::zoomIn() { zoomBy(3.0/2.0); }
void PDFDocumentView::zoomOut() { zoomBy(2.0/3.0); }

void PDFDocumentView::zoomToRect(QRectF a_rect)
{
  // NOTE: The argument, `a_rect`, is assumed to be in _scene coordinates_.
  fitInView(a_rect, Qt::KeepAspectRatio);

  // Since we passed `Qt::KeepAspectRatio` to `fitInView` both x and y scaling
  // factors were changed by the same amount. So we'll just take the x scale to
  // be the new `_zoomLevel`.
  _zoomLevel = transform().m11();
  emit changedZoom(_zoomLevel);
}

void PDFDocumentView::zoomFitWindow()
{
  // Curious fact: This function will end up producing a different zoom level depending on if
  // it zooms out or in. But the implementation of `fitInView` in the Qt source
  // is pretty solid---I can't think of a better way to do it.
  fitInView(_pdf_scene->pageAt(_currentPage), Qt::KeepAspectRatio);

  _zoomLevel = transform().m11();
  emit changedZoom(_zoomLevel);
}


// `zoomFitWidth` is basically a re-worked version of `QGraphicsView::fitInView`.
void PDFDocumentView::zoomFitWidth()
{
  if ( !scene() || rect().isNull() )
      return;

  QGraphicsItem *currentPage = _pdf_scene->pageAt(_currentPage);
  // Store current y position so we can center on it later.
  qreal ypos = mapToScene(viewport()->rect()).boundingRect().center().y();

  // Reset the view scale to 1:1.
  QRectF unity = matrix().mapRect(QRectF(0, 0, 1, 1));
  if (unity.isEmpty())
      return;
  scale(1 / unity.width(), 1 / unity.height());

  // Find the x scaling ratio to fit the page to the view width.
  int margin = 2;
  QRectF viewRect = viewport()->rect().adjusted(margin, margin, -margin, -margin);
  if (viewRect.isEmpty())
      return;
  qreal xratio = viewRect.width() / currentPage->sceneBoundingRect().width();

  scale(xratio, xratio);
  // Focus on the horizontal center of the page and set the vertical position
  // to the previous y position.
  centerOn(QPointF(currentPage->sceneBoundingRect().center().x(), ypos));

  // We reset the scaling factors to (1,1) and then scaled both by the same
  // factor so the zoom level should be equal to the x scale.
  _zoomLevel = transform().m11();
  emit changedZoom(_zoomLevel);
}

void PDFDocumentView::setMouseMode(const MouseMode newMode)
{
  if (_mouseMode == newMode)
    return;

  // TODO: eventually make _toolAccessors configurable
  _toolAccessors.clear();
  _toolAccessors[Qt::ControlModifier + Qt::LeftButton] = Tool_ContextClick;
  _toolAccessors[Qt::NoModifier + Qt::RightButton] = Tool_ContextMenu;
  _toolAccessors[Qt::NoModifier + Qt::MiddleButton] = Tool_Move;
  _toolAccessors[Qt::ShiftModifier + Qt::LeftButton] = Tool_ZoomIn;
  _toolAccessors[Qt::AltModifier + Qt::LeftButton] = Tool_ZoomOut;
  // Other tools: Tool_MagnifyingGlass, Tool_MarqueeZoom, Tool_Move

  disarmTool(_armedTool);
  switch (newMode) {
    case MouseMode_Move:
      armTool(Tool_Move);
      _toolAccessors[Qt::NoModifier + Qt::LeftButton] = Tool_Move;
      break;

    case MouseMode_MarqueeZoom:
      armTool(Tool_MarqueeZoom);
      _toolAccessors[Qt::NoModifier + Qt::LeftButton] = Tool_MarqueeZoom;
      break;

    case MouseMode_MagnifyingGlass:
      armTool(Tool_MagnifyingGlass);
      _toolAccessors[Qt::NoModifier + Qt::LeftButton] = Tool_MagnifyingGlass;
      break;
  }

  _mouseMode = newMode;
}

void PDFDocumentView::setMagnifierShape(const MagnifierShape shape)
{
  if (_magnifier)
    _magnifier->setShape(shape);
}

void PDFDocumentView::setMagnifierSize(const int size)
{
  if (_magnifier)
    _magnifier->setSize(size);
}

// Protected Slots
// --------------
void PDFDocumentView::maybeUpdateSceneRect() {
  if (!_pdf_scene || _pageMode != PageMode_SinglePage)
    return;

  // Set the scene rect of the view, i.e., the rect accessible via the scroll
  // bars. In single page mode, this must be the rect of the current page
  // **TODO:** Safeguard
  setSceneRect(_pdf_scene->pageAt(_currentPage)->sceneBoundingRect());
}

void PDFDocumentView::pdfActionTriggered(const PDFAction * action)
{
  if (!action)
    return;

  // Propagate link signals so that the outside world doesn't have to care about
  // our internal implementation (document/view structure, etc.)
  switch (action->type()) {
    case PDFAction::ActionTypeGoTo:
      {
        const PDFGotoAction * actionGoto = static_cast<const PDFGotoAction*>(action);
        // TODO: Possibly handle other properties of destination() (e.g.,
        // viewport settings, zoom level, etc.)
        // Note: if this action requires us to open other files (possible
        // security issue) or to create a new window, we need to propagate this
        // up the hierarchy. Otherwise we can handle it ourselves here.
        if (actionGoto->isRemote() || actionGoto->openInNewWindow())
          emit requestOpenPdf(actionGoto->filename(), actionGoto->destination().page(), actionGoto->openInNewWindow());
        else
          goToPage(actionGoto->destination().page());
      }
      break;
    case PDFAction::ActionTypeURI:
      {
        const PDFURIAction * actionURI = static_cast<const PDFURIAction*>(action);
        emit requestOpenUrl(actionURI->url());
      }
      break;
    case PDFAction::ActionTypeLaunch:
      {
        const PDFLaunchAction * actionLaunch = static_cast<const PDFLaunchAction*>(action);
        emit requestExecuteCommand(actionLaunch->command());
      }
      break;
    // **TODO:**
    // We don't handle other actions yet, but the ActionTypes Quit, Presentation,
    // EndPresentation, Find, GoToPage, Close, and Print should be propagated to
    // the outside world
    default:
      // All other link types are currently not supported
      break;
  }
}


// Event Handlers
// --------------

// Keep track of the current page by overloading the widget paint event.
void PDFDocumentView::paintEvent(QPaintEvent *event)
{
  Super::paintEvent(event);

  // After `QGraphicsView` has taken care of updates to this widget, find the
  // currently displayed page. We do this by grabbing all items that are
  // currently within the bounds of the viewport's top half. We take the
  // first item found to be the "current page".
  QRect pageBbox = viewport()->rect();
  pageBbox.setHeight(0.5 * pageBbox.height());
  int nextCurrentPage = _pdf_scene->pageNumAt(mapToScene(pageBbox));

  if ( nextCurrentPage != _currentPage && nextCurrentPage >= 0 && nextCurrentPage < _lastPage )
  {
    _currentPage = nextCurrentPage;
    emit changedPage(_currentPage);
  }

  // Draw a drop shadow
  if (_magnifier && _magnifier->isVisible()) {
    QPainter p(viewport());
    QPixmap& dropShadow(_magnifier->dropShadow());
    QRect r(QPoint(0, 0), dropShadow.size());
    r.moveCenter(_magnifier->geometry().center());
    p.drawPixmap(r.topLeft(), dropShadow);
  }
}

void PDFDocumentView::keyPressEvent(QKeyEvent *event)
{
  switch ( event->key() )
  {
    case Qt::Key_Home:
      if (_activeTool != Tool_None)
        break;
      goFirst();
      event->accept();
      break;

    case Qt::Key_End:
      if (_activeTool != Tool_None)
        break;
      goLast();
      event->accept();
      break;

    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
      // Don't scroll the view while a tool, such as the magnifier, is active.
      if (_activeTool != Tool_None)
        break;

      // Check to see if we need to jump to the next page in single page mode.
      if ( pageMode() == PageMode_SinglePage ) {
        int scrollStep, scrollPos = verticalScrollBar()->value();

        if ( event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown )
          scrollStep = verticalScrollBar()->pageStep();
        else
          scrollStep = verticalScrollBar()->singleStep();


        // Take no action on the first and last page so that PageUp/Down can
        // move the view right up to the page boundary.
        if (
          (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Up) &&
          (scrollPos - scrollStep) <= verticalScrollBar()->minimum() &&
          _currentPage > 0
        ) {
          goPrev();
          event->accept();
          break;
        } else if (
          (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Down) &&
          (scrollPos + scrollStep) >= verticalScrollBar()->maximum() &&
          _currentPage < _lastPage
        ) {
          goNext();
          event->accept();
          break;
        }
      }

      // Deliberate fall-through; we only override the movement keys if a tool is
      // currently in use or the view is in single page mode and the movement
      // would cross a page boundary.
    default:
      Super::keyPressEvent(event);
      break;
  }
  Tool t = _toolAccessors.value(Qt::LeftButton + event->modifiers(), Tool_None);
  if (_activeTool != t)
    abortTool(_activeTool);
  if (_armedTool != t) {
    disarmTool(_armedTool);
    armTool(t);
  }
}

void PDFDocumentView::keyReleaseEvent(QKeyEvent *event)
{
  Tool t = _toolAccessors.value(Qt::LeftButton + event->modifiers(), Tool_None);
  if (_activeTool != t)
    abortTool(_activeTool);
  if (_armedTool != t) {
    disarmTool(_armedTool);
    armTool(t);
  }
}

void PDFDocumentView::mousePressEvent(QMouseEvent * event)
{
  Super::mousePressEvent(event);

  // Don't do anything if the event was handled elsewhere (e.g., by a
  // PDFLinkGraphicsItem)
  if (event->isAccepted())
    return;

  Tool t = _toolAccessors.value(event->buttons() | event->modifiers(), Tool_None);
  if (_armedTool != t) {
    disarmTool(_armedTool);
    armTool(t);
  }
  if (t != _activeTool)
    abortTool(_activeTool);
  startTool(t, event);
}

void PDFDocumentView::mouseMoveEvent(QMouseEvent * event)
{
  // Note: to avoid reverting to _armed == Tool_None when moving the mouse
  // without pressing any button, we arm the default tool (corresponding to the
  // left mouse button) instead in that case
  Qt::MouseButtons buttons = event->buttons();
  if (buttons == Qt::NoButton)
    buttons = Qt::LeftButton;
  Tool t = _toolAccessors.value(buttons | event->modifiers(), Tool_None);
  // TODO: This can possibly be simplified by checking if any buttons are
  // pressed...
  if (_armedTool != t) {
    disarmTool(_armedTool);
    armTool(t);
  }
  if (t != _activeTool)
    abortTool(_activeTool);

  if (_activeTool == Tool_MarqueeZoom) {
    // Some shortcut values.
    QPoint o = _rubberBandOrigin, p = event->pos();

    if ( not event->buttons() == Qt::LeftButton ) {
      // The user somehow let go of the left button without us recieving an
      // event. Abort the zoom operation.
      _rubberBand->setGeometry(QRect());
      _rubberBand->hide();
    } else if ( (o - p).manhattanLength() > QApplication::startDragDistance() ) {
      // Update rubber band Geometry.
      _rubberBand->setGeometry(QRect(
        QPoint(qMin(o.x(),p.x()), qMin(o.y(), p.y())),
        QPoint(qMax(o.x(),p.x()), qMax(o.y(), p.y()))
      ));

      event->accept();
      return;
    }
  }

  Super::mouseMoveEvent(event);

  // We don't check for event->isAccepted() here; for one, this always seems to
  // return true (for whatever reason), but more importantly, without enabling
  // mouse tracking we only receive this event if the current widget has grabbed
  // the mouse (i.e., after a mousePressEvent and before the corresponding
  // mouseReleaseEvent)

  switch (_activeTool) {
    case Tool_MagnifyingGlass:
      if (_magnifier && _magnifier->isVisible()) {
        _magnifier->setPosition(event->pos());
        viewport()->update();
      }
      break;

    case Tool_Move:
      // Adapted from <qt>/src/gui/graphicsview/qgraphicsview.cpp @ QGraphicsView::mouseMoveEvent
      {
        QScrollBar *hBar = horizontalScrollBar();
        QScrollBar *vBar = verticalScrollBar();
        QPoint delta = event->pos() - _movePosition;
        hBar->setValue(hBar->value() - delta.x());
        vBar->setValue(vBar->value() - delta.y());
        _movePosition = event->pos();
      }
      break;

    default:
      // Nothing to do
      break;
  }
}

void PDFDocumentView::mouseReleaseEvent(QMouseEvent * event)
{
  Super::mouseReleaseEvent(event);

  // We don't check for event->isAccepted() here; for one, this always seems to
  // return true (for whatever reason), but more importantly, without enabling
  // mouse tracking we only receive this event if the current widget has grabbed
  // the mouse (i.e., after a mousePressEvent)

  Qt::MouseButtons buttons = event->buttons();
  if (buttons == Qt::NoButton)
    buttons |= Qt::LeftButton;

  Tool t = _toolAccessors.value(buttons | event->modifiers(), Tool_None);
  if (_armedTool != t) {
    disarmTool(_armedTool);
    armTool(t);
  }
  if (t != _activeTool)
    abortTool(_activeTool);
  else
    finishTool(_activeTool, event);
}

void PDFDocumentView::wheelEvent(QWheelEvent * event)
{
  int delta = event->delta();

  if (event->orientation() == Qt::Vertical && event->buttons() == Qt::NoButton && event->modifiers() == Qt::ControlModifier) {

    // TODO: Possibly make the Ctrl modifier configurable?
    // TODO: According to Qt docs, the delta() is not necessarily the same for all
    // mice. Decide if we want to enforce the same step size regardless of the
    // resolution of the mouse wheel sensor
    if ( delta > 0 )
      zoomIn();
    else if ( delta < 0 )
      zoomOut();
    event->accept();
    return;

  } else if ( pageMode() == PageMode_SinglePage ) {

    // In single page mode we need to flip to the next page if the scroll bar
    // is a the top or bottom of it's range.`
    int scrollPos = verticalScrollBar()->value();
    if ( delta < 0 && scrollPos == verticalScrollBar()->maximum() ) {
      goNext();

      event->accept();
      return;
    } else if ( delta > 0 && scrollPos == verticalScrollBar()->minimum() ) {
      goPrev();

      event->accept();
      return;
    }

  }

  Super::wheelEvent(event);
}

void PDFDocumentView::armTool(const Tool tool)
{
  if (_armedTool == tool)
    return;

  // FIXME: Create cursors only once
  // FIXME: Should separate cursors from the rest of the viewer resources
  switch (tool) {
    case Tool_MagnifyingGlass:
      viewport()->setCursor(QCursor(QPixmap(QString::fromUtf8(":/icons/magnifiercursor.png"))));
      break;
    case Tool_ZoomIn:
      viewport()->setCursor(QCursor(QPixmap(QString::fromUtf8(":/icons/zoomincursor.png"))));
      break;
    case Tool_ZoomOut:
      viewport()->setCursor(QCursor(QPixmap(QString::fromUtf8(":/icons/zoomoutcursor.png"))));
      break;
    case Tool_Move:
      viewport()->setCursor(Qt::OpenHandCursor);
      break;
    // FIXME: Mouse cursor for marquee zoom
    case Tool_MarqueeZoom:
      viewport()->setCursor(Qt::CrossCursor);
      break;
    default:
      viewport()->unsetCursor();
      break;
  }
  _armedTool = tool;
}

void PDFDocumentView::startTool(const Tool tool, QMouseEvent * event)
{
  switch (tool) {
    case Tool_MarqueeZoom:
      // The ideal way to implement marquee zoom would be to set `dragMode` to
      // `QGraphicsView::RubberBandDrag` and use the rubber band selector
      // built-in to `QGraphicsView`. However, there is no way to do this and
      // prevent graphics items, such as links, from responding to the
      // mouse---hence no way to start a zoom over a link. Calling
      // `setInteractive(false)` keeps the view from propagating mouse events to
      // the scene, but it also disables `QGraphicsView::RubberBandDrag`.
      //
      // So... we have to do this ourselves.
      _rubberBandOrigin = event->pos();
      _rubberBand->setGeometry(QRect());
      _rubberBand->show();
      event->accept();
      break;
    case Tool_Move:
      // The ideal way to implement the move tool would be to set `dragMode` to
      // `QGraphicsView::ScrollHandDrag` and use the built-in functionality.
      // However, that does work only with the left mouse button.
      //
      // So... we have to do this ourselves.
      viewport()->setCursor(Qt::ClosedHandCursor);
      _movePosition = event->pos();
      event->accept();
      break;
    case Tool_MagnifyingGlass:
      _magnifier->prepareToShow();
      _magnifier->setPosition(event->pos());
      _magnifier->show();

      // Hide the cursor while the magnifier is active, but save a reference to
      // the current value so that it can be restored later.
      _hiddenCursor = viewport()->cursor();
      viewport()->setCursor(Qt::BlankCursor);

      viewport()->update();
      event->accept();
      break;
    default:
      // Nothing to do
      break;
  }
  _activeTool = tool;
}

void PDFDocumentView::finishTool(const Tool tool, QMouseEvent * event)
{
  switch (tool) {
    case Tool_MarqueeZoom:
      if (_rubberBand->isVisible()) {
        QRectF zoomRect = mapToScene(_rubberBand->geometry()).boundingRect();
        _rubberBand->hide();
        _rubberBand->setGeometry(QRect());
        zoomToRect(zoomRect);
        event->accept();
      }
      break;

    case Tool_MagnifyingGlass:
      if (_magnifier && _magnifier->isVisible()) {
        _magnifier->hide();
        viewport()->setCursor(_hiddenCursor);
        viewport()->update();
        event->accept();
      }
      break;

    case Tool_Move:
      // TODO: Disarming and rearming the current tool is a hack to get the
      // cursor right if the move tool was accessed through non-standard ways
      // (e.g., using the middle mouse button)
      {
        Tool armedTool = _armedTool;
        disarmTool(armedTool);
        armTool(armedTool);
      }
      break;

    case Tool_ZoomIn:
      zoomIn();
      event->accept();
      break;

    case Tool_ZoomOut:
      zoomOut();
      event->accept();
      break;

    case Tool_ContextClick:
      {
        QPointF pos(mapToScene(event->pos()));
        QGraphicsItem * item = scene()->itemAt(pos);
        if (!item || item->type() != PDFPageGraphicsItem::Type)
          break;
        PDFPageGraphicsItem * pageItem = static_cast<PDFPageGraphicsItem*>(item);
        emit contextClick(pageItem->page()->pageNum(), pageItem->mapToPage(pageItem->mapFromScene(pos)));
        event->accept();
      }
      break;

    default:
      // Nothing to do
      break;
  }
  _activeTool = Tool_None;
}

void PDFDocumentView::abortTool(const Tool tool)
{
  switch (tool) {
    case Tool_MarqueeZoom:
      if (_rubberBand->isVisible()) {
        _rubberBand->hide();
        _rubberBand->setGeometry(QRect());
      }
      break;
      
    case Tool_Move:
      // TODO: Disarming and rearming the current tool is a hack to get the
      // cursor right if the move tool was accessed through non-standard ways
      // (e.g., using the middle mouse button)
      {
        Tool armedTool = _armedTool;
        disarmTool(armedTool);
        armTool(armedTool);
      }
      break;

    case Tool_MagnifyingGlass:
      if (_magnifier && _magnifier->isVisible()) {
        _magnifier->hide();
        viewport()->setCursor(_hiddenCursor);
        viewport()->update();
      }
      break;
    default:
      // Nothing to do
      break;
  }
  _activeTool = Tool_None;
}

void PDFDocumentView::disarmTool(const Tool tool)
{
  viewport()->unsetCursor();
  _armedTool = Tool_None;
}


// PDFDocumentMagnifierView
// ========================
//
PDFDocumentMagnifierView::PDFDocumentMagnifierView(PDFDocumentView *parent /* = 0 */) :
  Super(parent),
  _parent_view(parent),
  _zoomLevel(1.0),
  _zoomFactor(2.0),
  _shape(PDFDocumentView::Magnifier_Circle),
  _size(300)
{
  // the magnifier should initially be hidden
  hide();

  // suppress scrollbars
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // suppress any border styling (which doesn't work with a mask, e.g., for a
  // circular magnifier)
  setFrameShape(QFrame::NoFrame);

  if (parent) {
    // transfer some settings from the parent view
    setBackgroundRole(parent->backgroundRole());
    setAlignment(parent->alignment());
  }

  setShape(_shape);
}

void PDFDocumentMagnifierView::prepareToShow()
{
  qreal zoomLevel;

  if (!_parent_view)
    return;

  // Ensure we have the same scene
  if (_parent_view->scene() != scene())
    setScene(_parent_view->scene());
  // Fix the zoom
  zoomLevel = _parent_view->zoomLevel() * _zoomFactor;
  if (zoomLevel != _zoomLevel)
    scale(zoomLevel / _zoomLevel, zoomLevel / _zoomLevel);
  _zoomLevel = zoomLevel;
  // Ensure we have enough padding at the border that we can display the
  // magnifier even beyond the edge
  setSceneRect(_parent_view->sceneRect().adjusted(-width() / _zoomLevel, -height() / _zoomLevel, width() / _zoomLevel, height() / _zoomLevel));
}

void PDFDocumentMagnifierView::setZoomFactor(const qreal zoomFactor)
{
  _zoomFactor = zoomFactor;
  // Actual handling of zoom levels happens in prepareToShow, as the zoom level
  // of the parent cannot change while the magnifier is shown
}

void PDFDocumentMagnifierView::setPosition(const QPoint pos)
{
  move(pos.x() - width() / 2, pos.y() - height() / 2);
  centerOn(_parent_view->mapToScene(pos));
}

void PDFDocumentMagnifierView::setShape(const PDFDocumentView::MagnifierShape shape)
{
  _shape = shape;

  // ensure the window rect is set properly for the new mode
  setSize(_size);

  switch (shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      clearMask();
#ifdef Q_WS_MAC
      // On OS X there is a bug that affects masking of QAbstractScrollArea and
      // its subclasses:
      //
      //   https://bugreports.qt.nokia.com/browse/QTBUG-7150
      //
      // The fix is to explicitly mask the viewport. As of Qt 4.7.4, this bug
      // is still present.
      viewport()->clearMask();
#endif
      break;
    case PDFDocumentView::Magnifier_Circle:
      setMask(QRegion(rect(), QRegion::Ellipse));
#ifdef Q_WS_MAC
      // Hack to fix QTBUG-7150
      viewport()->setMask(QRegion(rect(), QRegion::Ellipse));
#endif
      break;
  }
  _dropShadow = QPixmap();
}

void PDFDocumentMagnifierView::setSize(const int size)
{
  _size = size;
  switch (_shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      setFixedSize(size * 4 / 3, size);
      break;
    case PDFDocumentView::Magnifier_Circle:
      setFixedSize(size, size);
      break;
  }
  _dropShadow = QPixmap();
}

void PDFDocumentMagnifierView::paintEvent(QPaintEvent * event)
{
  Super::paintEvent(event);

  // Draw our custom border
  // Note that QGraphicsView is derived from QAbstractScrollArea, but we are not
  // asked to paint on that but on the widget it contains. Therefore, we can't
  // just say QPainter(this)
  QPainter painter(viewport());

  painter.setRenderHint(QPainter::Antialiasing);
  
  QPen pen(Qt::gray);
  pen.setWidth(2);
  
  QRect rect(this->rect());

  painter.setPen(pen);
  switch(_shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      painter.drawRect(rect);
      break;
    case PDFDocumentView::Magnifier_Circle:
      // Ensure we're drawing where we should, regardless how the window system
      // handles masks
      painter.setClipRegion(mask());
      // **TODO:** It seems to be necessary to adjust the window rect by one pixel
      // to draw an evenly wide border; is there a better way?
      rect.adjust(1, 1, 0, 0);
      painter.drawEllipse(rect);
      break;
  }

  // **Note:** We don't/can't draw the drop-shadow here. The reason is that we
  // rely on Super::paintEvent to do the actual rendering, which constructs its
  // own QPainter so we can't do clipping. Resetting the mask is no option,
  // either, as that may trigger an update (recursion!).
  // Alternatively, we could fill the border with the background from the
  // underlying window. But _parent_view->render() no option, because it
  // requires QWidget::DrawChildren (apparently the QGraphicsItems are
  // implemented as child widgets) which would cause a recursion again (the
  // magnifier is also a child widget!). Calling scene()->render() is no option,
  // either, because then render requests for unmagnified images would originate
  // from here, which would break the current implementation of
  // PDFPageGraphicsItem::paint().
  // Instead, drop-shadows are drawn in PDFDocumentView::paintEvent(), invoking
  // PDFDocumentMagnifierView::dropShadow().
}

// Modelled after http://labs.qt.nokia.com/2009/10/07/magnifying-glass
QPixmap& PDFDocumentMagnifierView::dropShadow()
{
  if (!_dropShadow.isNull())
    return _dropShadow;

  int padding = 10;
  _dropShadow = QPixmap(width() + 2 * padding, height() + 2 * padding);

  _dropShadow.fill(Qt::transparent);

  switch(_shape) {
    case PDFDocumentView::Magnifier_Rectangle:
      {
        QPainterPath path;
        QRectF boundingRect(_dropShadow.rect().adjusted(0, 0, -1, -1));
        QLinearGradient gradient(boundingRect.center(), QPointF(0.0, boundingRect.center().y()));
        gradient.setSpread(QGradient::ReflectSpread);
        QGradientStops stops;
        QColor color(Qt::black);
        color.setAlpha(64);
        stops.append(QGradientStop(1.0 - padding * 2.0 / _dropShadow.width(), color));
        color.setAlpha(0);
        stops.append(QGradientStop(1.0, color));

        QPainter shadow(&_dropShadow);
        shadow.setRenderHint(QPainter::Antialiasing);

        // paint horizontal gradient
        gradient.setStops(stops);

        path = QPainterPath();
        path.moveTo(boundingRect.topLeft());
        path.lineTo(boundingRect.topLeft() + QPointF(padding, padding));
        path.lineTo(boundingRect.bottomRight() + QPointF(-padding, -padding));
        path.lineTo(boundingRect.bottomRight());
        path.lineTo(boundingRect.topRight());
        path.lineTo(boundingRect.topRight() + QPointF(-padding, padding));
        path.lineTo(boundingRect.bottomLeft() + QPointF(padding, -padding));
        path.lineTo(boundingRect.bottomLeft());
        path.closeSubpath();

        shadow.fillPath(path, gradient);

        // paint vertical gradient
        stops[0].first = 1.0 - padding * 2.0 / _dropShadow.height();
        gradient.setStops(stops);

        path = QPainterPath();
        path.moveTo(boundingRect.topLeft());
        path.lineTo(boundingRect.topLeft() + QPointF(padding, padding));
        path.lineTo(boundingRect.bottomRight() + QPointF(-padding, -padding));
        path.lineTo(boundingRect.bottomRight());
        path.lineTo(boundingRect.bottomLeft());
        path.lineTo(boundingRect.bottomLeft() + QPointF(padding, -padding));
        path.lineTo(boundingRect.topRight() + QPointF(-padding, padding));
        path.lineTo(boundingRect.topRight());
        path.closeSubpath();

        gradient.setFinalStop(QPointF(QRectF(_dropShadow.rect()).center().x(), 0.0));
        shadow.fillPath(path, gradient);
      }
      break;
    case PDFDocumentView::Magnifier_Circle:
      {
        QRadialGradient gradient(QRectF(_dropShadow.rect()).center(), _dropShadow.width() / 2.0, QRectF(_dropShadow.rect()).center());
        QColor color(Qt::black);
        color.setAlpha(0);
        gradient.setColorAt(1.0, color);
        color.setAlpha(64);
        gradient.setColorAt(1.0 - padding * 2.0 / _dropShadow.width(), color);
        
        QPainter shadow(&_dropShadow);
        shadow.setRenderHint(QPainter::Antialiasing);
        shadow.fillRect(_dropShadow.rect(), gradient);
      }
      break;
  }
  return _dropShadow;
}


// PDFDocumentScene
// ================
//
// A large canvas that manages the layout of QGraphicsItem subclasses. The
// primary items we are concerned with are PDFPageGraphicsItem and
// PDFLinkGraphicsItem.
PDFDocumentScene::PDFDocumentScene(Document *a_doc, QObject *parent):
  Super(parent),
  _doc(a_doc)
{
  // We need to register a QList<PDFLinkGraphicsItem *> meta-type so we can
  // pass it through inter-thread (i.e., queued) connections
  qRegisterMetaType< QList<PDFLinkGraphicsItem *> >();

  _lastPage = _doc->numPages();

  connect(&_pageLayout, SIGNAL(layoutChanged(const QRectF)), this, SLOT(pageLayoutChanged(const QRectF)));

  // Create a `PDFPageGraphicsItem` for each page in the PDF document and let
  // them be layed out by a `PDFPageLayout` instance.
  int i;
  PDFPageGraphicsItem *pagePtr;

  for (i = 0; i < _lastPage; ++i)
  {
    pagePtr = new PDFPageGraphicsItem(_doc->page(i));
    _pages.append(pagePtr);
    addItem(pagePtr);
    _pageLayout.addPage(pagePtr);
  }
  _pageLayout.relayout();
}

void PDFDocumentScene::handleActionEvent(const PDFActionEvent * action_event)
{
  if (!action_event || !action_event->action)
    return;
  const PDFAction * action = action_event->action;

  switch (action_event->action->type() )
  {
    case PDFAction::ActionTypeGoTo:
      {
        const PDFGotoAction * actionGoto = static_cast<const PDFGotoAction*>(action);
        if (!actionGoto->isRemote()) {
          // Jump by page number.
          //
          // **NOTE:**
          // _There are many details that are not being considered, such as
          // centering on a specific anchor point and possibly changing the zoom
          // level rather than just focusing on the center of the target page._
          emit pageChangeRequested(actionGoto->destination().page());
          return;
        }
      }
      break;
    // Link types that we don't handle here but that may be of interest
    // elsewhere
    case PDFAction::ActionTypeURI:
    case PDFAction::ActionTypeLaunch:
      break;
    default:
      // All other link types are currently not supported
      return;
  }
  // Translate into a signal that can be handled by some other part of the
  // program, such as a `PDFDocumentView`.
  emit pdfActionTriggered(action_event->action);
}


// Accessors
// ---------

QList<QGraphicsItem*> PDFDocumentScene::pages() { return _pages; };

// Overloaded method that returns all page objects inside a given rectangular
// area. First, `items` is used to grab all items inside the rectangle. This
// list is then filtered by item type so that it contains only references to
// `PDFPageGraphicsItem` objects.
QList<QGraphicsItem*> PDFDocumentScene::pages(const QPolygonF &polygon)
{
  QList<QGraphicsItem*> pageList = items(polygon);
  QtConcurrent::blockingFilter(pageList, isPageItem);

  return pageList;
};

// Convenience function to avoid moving the complete list of pages around
// between functions if only one page is needed
QGraphicsItem* PDFDocumentScene::pageAt(const int idx)
{
  if (idx < 0 || idx >= _pages.size())
    return NULL;
  return _pages[idx];
}

// This is a convenience function for returning the page number of the first
// page item inside a given area of the scene. If no page is in the specified
// area, -1 is returned.
int PDFDocumentScene::pageNumAt(const QPolygonF &polygon)
{
  QList<QGraphicsItem*> p(pages(polygon));
  if (p.isEmpty())
    return -1;
  return _pages.indexOf(p.first());
}

int PDFDocumentScene::pageNumFor(PDFPageGraphicsItem * const graphicsItem) const
{
  return _pages.indexOf(graphicsItem);
}

int PDFDocumentScene::lastPage() { return _lastPage; }

// Event Handlers
// --------------

// We re-implement the main event handler for the scene so that we can
// translate events generated by child items into signals that can be sent out
// to the rest of the program.
bool PDFDocumentScene::event(QEvent *event)
{
  if ( event->type() == PDFActionEvent::ActionEvent )
  {
    event->accept();
    // Cast to a pointer for `PDFActionEvent` so that we can access the `pageNum`
    // field.
    const PDFActionEvent *action_event = static_cast<const PDFActionEvent*>(event);
    handleActionEvent(action_event);
    return true;
  }

  return Super::event(event);
}

// Protected Slots
// --------------
void PDFDocumentScene::pageLayoutChanged(const QRectF& sceneRect)
{
  setSceneRect(sceneRect);
  emit pageLayoutChanged();
}

// Other
// -----
void PDFDocumentScene::showOnePage(const int pageIdx) const
{
  int i;

  for (i = 0; i < _pages.size(); ++i) {
    if (!isPageItem(_pages[i]))
      continue;
    _pages[i]->setVisible(i == pageIdx);
  }
}

void PDFDocumentScene::showAllPages() const
{
  int i;

  for (i = 0; i < _pages.size(); ++i) {
    if (!isPageItem(_pages[i]))
      continue;
    _pages[i]->setVisible(true);
  }
}


// PDFPageGraphicsItem
// ===================

// This class descends from `QGraphicsObject` and implements the on-screen
// representation of `Page` objects.
PDFPageGraphicsItem::PDFPageGraphicsItem(Page *a_page, QGraphicsItem *parent):
  Super(parent),
  _page(a_page),
  _dpiX(QApplication::desktop()->physicalDpiX()),
  _dpiY(QApplication::desktop()->physicalDpiY()),

  _linksLoaded(false),
  _zoomLevel(0.0),
  _magnifiedZoomLevel(0.0)
{
  // Create an empty pixmap that is the same size as the PDF page. This
  // allows us to delay the rendering of pages until they actually come into
  // view yet still know what the page size is.
  _pageSize = _page->pageSizeF();
  _pageSize.setWidth(_pageSize.width() * _dpiX / 72.0);
  _pageSize.setHeight(_pageSize.height() * _dpiY / 72.0);

  _pageScale = QTransform::fromScale(_pageSize.width(), _pageSize.height());

  // So we get information during paint events about what portion of the page
  // is visible.
  //
  // NOTE: This flag needs Qt 4.6 or newer.
  setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);
}

QRectF PDFPageGraphicsItem::boundingRect() const { return QRectF(QPointF(0.0, 0.0), _pageSize); }
int PDFPageGraphicsItem::type() const { return Type; }

QPointF PDFPageGraphicsItem::mapFromPage(const QPointF & point)
{
  // item coordinates are in pixels
  return QPointF(_pageSize.width() * point.x() / _page->pageSizeF().width(), \
    _pageSize.height() * (1.0 - point.y() / _page->pageSizeF().height()));
}

QPointF PDFPageGraphicsItem::mapToPage(const QPointF & point)
{
  // item coordinates are in pixels
  return QPointF(_page->pageSizeF().width() * point.x() / _pageSize.width(), \
    _page->pageSizeF().height() * (1.0 - point.y() / _pageSize.height()));
}

// An overloaded paint method allows us to handle rendering via asynchronous
// calls to backend functions.
void PDFPageGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
  // Really, there is an X scaling factor and a Y scaling factor, but we assume
  // that the X scaling factor is equal to the Y scaling factor.
  qreal scaleFactor = painter->transform().m11();
  QTransform scaleT = QTransform::fromScale(scaleFactor, scaleFactor);
  QRect pageRect = scaleT.mapRect(boundingRect()).toAlignedRect(), pageTile;

  // If this is the first time this `PDFPageGraphicsItem` has come into view,
  // `_linksLoaded` will be `false`. We then load all of the links on the page.
  if ( not _linksLoaded )
  {
    _page->asyncLoadLinks(this);
    _linksLoaded = true;
  }

  if ( _zoomLevel != scaleFactor ) {
    // We apply a scale-only transformation to the bounding box to get the new
    // page dimensions at the new DPI. The `QRectF` is converted to a `QRect`
    // by `toAlignedRect` so that we can work in pixels.

    if (pageRect.width() % TILE_SIZE == 0)
      _nTile_x = pageRect.width() / TILE_SIZE;
    else
      _nTile_x = pageRect.width() / TILE_SIZE + 1;
    if (pageRect.height() % TILE_SIZE == 0)
      _nTile_y = pageRect.height() / TILE_SIZE;
    else
      _nTile_y = pageRect.height() / TILE_SIZE + 1;

    _zoomLevel = scaleFactor;
  }

  painter->save();
    // Clip to the exposed rectangle to prevent unnecessary drawing operations.
    // This can provide up to a 50% speedup depending on the size of the tile.
    painter->setClipRect(option->exposedRect);

    // The transformation matrix of the `painter` object contains information
    // such as the current zoom level of the widget viewing this PDF page. We
    // throw away the scaling information because that has already been
    // applied during page rendering. (Note: we don't support rotation/skewing,
    // so we only care about the translational part)
    QTransform pageT = painter->transform();
    painter->setTransform(QTransform::fromTranslate(pageT.dx(), pageT.dy()));

#ifdef DEBUG
    // Pen style used to draw the outline of each tile for debugging purposes.
    QPen tilePen(Qt::darkGray);
    tilePen.setStyle(Qt::DashDotLine);
    painter->setPen(tilePen);
#endif

    QRect visibleRect = scaleT.mapRect(option->exposedRect).toAlignedRect();
    QSharedPointer<QImage> renderedPage;
    
    int i, imin, imax;
    int j, jmin, jmax;
    
    imin = (visibleRect.left() - pageRect.left()) / TILE_SIZE;
    imax = (visibleRect.right() - pageRect.left());
    if (imax % TILE_SIZE == 0)
      imax /= TILE_SIZE;
    else
      imax = imax / TILE_SIZE + 1;

    jmin = (visibleRect.top() - pageRect.top()) / TILE_SIZE;
    jmax = (visibleRect.bottom() - pageRect.top());
    if (jmax % TILE_SIZE == 0)
      jmax /= TILE_SIZE;
    else
      jmax = jmax / TILE_SIZE + 1;

    for (j = jmin; j < jmax; ++j) {
      for (i = imin; i < imax; ++i) {
        // Construct the tile to paint. Intersect it with the page rect to
        // avoid doign extra work outside the page
        QRect tile(i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE);
        tile &= pageRect;
        renderedPage = _page->getTileImage(this, _dpiX * scaleFactor, _dpiY * scaleFactor, tile);
        // we don't want a finished render thread to change our image while we
        // draw it
        _page->document()->pageCache().lock();
        // renderedPage as returned from getTileImage _should_ always be valid
        if ( renderedPage )
          painter->drawImage(tile.topLeft(), *renderedPage);
        _page->document()->pageCache().unlock();
#ifdef DEBUG
        painter->drawRect(tile);
#endif
      }
    }

  painter->restore();
}

// Event Handlers
// --------------
bool PDFPageGraphicsItem::event(QEvent *event)
{
  // Look for callbacks from asynchronous page operations.
  if( event->type() == PDFLinksLoadedEvent::LinksLoadedEvent ) {
    event->accept();

    // Cast to a `PDFLinksLoaded` event so we can access the links.
    const PDFLinksLoadedEvent *links_loaded_event = static_cast<const PDFLinksLoadedEvent*>(event);
    addLinks(links_loaded_event->links);

    return true;

  } else if( event->type() == PDFPageRenderedEvent::PageRenderedEvent ) {
    event->accept();

    // FIXME: We're sort of misusing the render event here---it contains a copy
    // of the image data that we never touch. The assumption is that the page
    // cache now has new data, so we call `update` to trigger a repaint which
    // fetches stuff from the cache.
    //
    // Perhaps there should be a separate event for when the cache is updated.
    update();

    return true;
  }

  // Otherwise, pass event to default handler.
  return Super::event(event);
}

// This method causes the `PDFPageGraphicsItem` to create `PDFLinkGraphicsItem`
// objects for a list of asynchronously generated `PDFLinkAnnotation` objects.
// The page item also takes ownership the objects created.  Calling
// `setParentItem` causes the link objects to be added to the scene that owns
// the page object. `update` is then called to ensure all links are drawn at
// once.
void PDFPageGraphicsItem::addLinks(QList< QSharedPointer<PDFLinkAnnotation> > links)
{
  PDFLinkGraphicsItem *linkItem;
#ifdef DEBUG
  stopwatch.start();
#endif
  foreach( QSharedPointer<PDFLinkAnnotation> link, links ){
    linkItem = new PDFLinkGraphicsItem(link);
    linkItem->setTransform(_pageScale);
    linkItem->setParentItem(this);
  }
#ifdef DEBUG
  qDebug() << "Added links in: " << stopwatch.elapsed() << " milliseconds";
#endif

  update();
}


// PDFLinkGraphicsItem
// ===================

// This class descends from `QGraphicsRectItem` and serves the following
// functions:
//
//    * Provides easy access to the on-screen geometry of a hyperlink area.
//
//    * Handles tasks such as cursor changes on mouse hover and link activation
//      on mouse clicks.
PDFLinkGraphicsItem::PDFLinkGraphicsItem(QSharedPointer<PDFLinkAnnotation> a_link, QGraphicsItem *parent):
  Super(parent),
  _link(a_link),
  _activated(false)
{
  // The link area is expressed in "normalized page coordinates", i.e.  values
  // in the range [0, 1]. The transformation matrix of this item will have to
  // be adjusted so that links will show up correctly in a graphics view.
  setRect(_link->rect());

  // Allows links to provide a context-specific cursor when the mouse is
  // hovering over them.
  //
  // **NOTE:** _Requires Qt 4.4 or newer._
  setAcceptHoverEvents(true);

  // Only left-clicks will trigger the link.
  setAcceptedMouseButtons(Qt::LeftButton);

#ifdef DEBUG
  // **TODO:**
  // _Currently for debugging purposes only so that the link area can be
  // determined visually, but might make a nice option._
  setPen(QPen(Qt::red));
#else
  // Perhaps there is a way to not draw the outline at all? Might be more
  // efficient...
  setPen(QPen(Qt::transparent));
#endif

  PDFAction * action = _link->actionOnActivation();
  if (action) {
    // Set some meaningful tooltip to inform the user what the link does
    // Using <p>...</p> ensures the tooltip text is interpreted as rich text
    // and thus is wrapping sensibly to avoid over-long lines.
    // Using PDFDocumentView::trUtf8 avoids having to explicitly derive
    // PDFLinkGraphicsItem explicily from QObject and puts all translatable
    // strings into the same context.
    switch(action->type()) {
      case PDFAction::ActionTypeGoTo:
        {
          PDFGotoAction * actionGoto = static_cast<PDFGotoAction*>(action);
          setToolTip(PDFDocumentView::trUtf8("<p>Goto page %1</p>").arg(actionGoto->destination().page()));
        }
        break;
      case PDFAction::ActionTypeURI:
        {
          PDFURIAction * actionURI = static_cast<PDFURIAction*>(action);
          setToolTip(QString::fromUtf8("<p>%1</p>").arg(actionURI->url().toString()));
        }
        break;
      case PDFAction::ActionTypeLaunch:
        {
          PDFLaunchAction * actionLaunch = static_cast<PDFLaunchAction*>(action);
          setToolTip(PDFDocumentView::trUtf8("<p>Execute `%1`</p>").arg(actionLaunch->command()));
        }
        break;
      default:
        // All other link types are currently not supported
        break;
    }
  }
}

int PDFLinkGraphicsItem::type() const { return Type; }

// Event Handlers
// --------------

// Swap cursor during hover events.
void PDFLinkGraphicsItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
  setCursor(Qt::PointingHandCursor);
}

void PDFLinkGraphicsItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
  unsetCursor();
}

// Respond to clicks. Limited to left-clicks by `setAcceptedMouseButtons` in
// this object's constructor.
void PDFLinkGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
  // Actually opening the link is handled during a `mouseReleaseEvent` --- but
  // only if the `_activated` flag is `true`.
  _activated = true;
}

// The real nitty-gritty of link activation happens in here.
void PDFLinkGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
  // Check that this link was "activated" (mouse press occurred within the link
  // bounding box) and that the mouse release also occurred within the bounding
  // box.
  if ( (not _activated) || (not contains(event->pos())) )
  {
    _activated = false;
    return;
  }

  // Post an event to the parent scene. The scene then takes care of processing
  // it further, notifying objects, such as `PDFDocumentView`, that may want to
  // take action via a `SIGNAL`.
  // **TODO:** Wouldn't a direct call be more efficient?
  if (_link && _link->actionOnActivation())
    QCoreApplication::postEvent(scene(), new PDFActionEvent(_link->actionOnActivation()));
  _activated = false;
}


// PDFToCDockWidget
// ============

PDFToCDockWidget::PDFToCDockWidget(QWidget * parent) :
  QDockWidget(PDFDocumentView::trUtf8("Table of Contents"), parent)
{
  QTreeWidget * tree = new QTreeWidget(this);
  tree->setAlternatingRowColors(true);
  tree->setHeaderHidden(true);
  tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  connect(tree, SIGNAL(itemSelectionChanged()), this, SLOT(itemSelectionChanged()));
  setWidget(tree);
}

PDFToCDockWidget::~PDFToCDockWidget()
{
  clearTree();
}
  
void PDFToCDockWidget::setToCData(const PDFToC data)
{
  QTreeWidget * tree = qobject_cast<QTreeWidget*>(widget());
  if (!tree)
    return;
  clearTree();
  recursiveAddTreeItems(data, tree->invisibleRootItem());
}

void PDFToCDockWidget::itemSelectionChanged()
{
  QTreeWidget * tree = qobject_cast<QTreeWidget*>(widget());
  if (!tree || tree->selectedItems().isEmpty())
    return;
  
  // Since the ToC QTreeWidget is in single selection mode, the first element is
  // the only one.
  QTreeWidgetItem * item = tree->selectedItems().first();
  Q_ASSERT(item != NULL);
  // TODO: It might be better to register PDFAction with the QMetaType framework
  // instead of doing casts with (void*).
  PDFAction * action = (PDFAction*)item->data(0, Qt::UserRole).value<void*>();
  if (action)
    emit actionTriggered(action);
}

void PDFToCDockWidget::clearTree()
{
  QTreeWidget * tree = qobject_cast<QTreeWidget*>(widget());
  if (!tree)
    return;

  recursiveClearTreeItems(tree->invisibleRootItem());
}

//static
void PDFToCDockWidget::recursiveAddTreeItems(const QList<PDFToCItem> & tocItems, QTreeWidgetItem * parentTreeItem)
{
  foreach (const PDFToCItem & tocItem, tocItems) {
    QTreeWidgetItem * treeItem = new QTreeWidgetItem(parentTreeItem, QStringList(tocItem.label()));
    treeItem->setForeground(0, tocItem.color());
    if (tocItem.flags()) {
      QFont font = treeItem->font(0);
      font.setBold(tocItem.flags().testFlag(PDFToCItem::Flag_Bold));
      font.setItalic(tocItem.flags().testFlag(PDFToCItem::Flag_Bold));
      treeItem->setFont(0, font);
    }
    treeItem->setExpanded(tocItem.isOpen());
    // TODO: It might be better to register PDFAction via QMetaType to avoid
    // having to use (void*).
    treeItem->setData(0, Qt::UserRole, QVariant::fromValue((void*)tocItem.action()->clone()));

    // FIXME: page numbers in col 2, goto actions, etc.

    if (!tocItem.children().isEmpty())
      recursiveAddTreeItems(tocItem.children(), treeItem);
  }
}

//static
void PDFToCDockWidget::recursiveClearTreeItems(QTreeWidgetItem * parent)
{
  Q_ASSERT(parent != NULL);
  while (parent->childCount() > 0) {
    QTreeWidgetItem * item = parent->child(0);
    recursiveClearTreeItems(item);
    PDFAction * action = (PDFAction*)item->data(0, Qt::UserRole).value<void*>();
    if (action)
      delete action;
    parent->removeChild(item);
    delete item;
  }
}


// PDFActionEvent
// ============

// A PDF Link event is generated when a link is clicked and contains the page
// number of the link target.
PDFActionEvent::PDFActionEvent(const PDFAction * action) : Super(ActionEvent), action(action) {}

// Obtain a unique ID for `PDFActionEvent` that can be used by event handlers to
// filter out these events.
QEvent::Type PDFActionEvent::ActionEvent = static_cast<QEvent::Type>( QEvent::registerEventType() );


PDFPageLayout::PDFPageLayout() :
_numCols(1),
_firstCol(0),
_xSpacing(10),
_ySpacing(10),
_isContinuous(true)
{
}

void PDFPageLayout::setColumnCount(const int numCols) {
  // We need at least one column, and we only handle changes
  if (numCols <= 0 || numCols == _numCols)
    return;

  _numCols = numCols;
  // Make sure the first column is still valid
  if (_firstCol >= _numCols)
    _firstCol = _numCols - 1;
  rearrange();
}

void PDFPageLayout::setColumnCount(const int numCols, const int firstCol) {
  // We need at least one column, and we only handle changes
  if (numCols <= 0 || (numCols == _numCols && firstCol == _firstCol))
    return;

  _numCols = numCols;

  if (firstCol < 0)
    _firstCol = 0;
  else if (firstCol >= _numCols)
    _firstCol = _numCols - 1;
  else
    _firstCol = firstCol;
  rearrange();
}

void PDFPageLayout::setFirstColumn(const int firstCol) {
  // We only handle changes
  if (firstCol == _firstCol)
    return;

  if (firstCol < 0)
    _firstCol = 0;
  else if (firstCol >= _numCols)
    _firstCol = _numCols - 1;
  else
    _firstCol = firstCol;
  rearrange();
}

void PDFPageLayout::setXSpacing(const qreal xSpacing) {
  if (xSpacing > 0)
    _xSpacing = xSpacing;
  else
    _xSpacing = 0.;
}

void PDFPageLayout::setYSpacing(const qreal ySpacing) {
  if (ySpacing > 0)
    _ySpacing = ySpacing;
  else
    _ySpacing = 0.;
}

void PDFPageLayout::setContinuous(const bool continuous /* = true */)
{
  if (continuous == _isContinuous)
    return;
  _isContinuous = continuous;
  if (!_isContinuous)
    setColumnCount(1, 0);
    // setColumnCount() calls relayout automatically
  else relayout();
}

int PDFPageLayout::rowCount() const {
  if (_layoutItems.isEmpty())
    return 0;
  return _layoutItems.last().row + 1;
}

void PDFPageLayout::addPage(PDFPageGraphicsItem * page) {
  LayoutItem item;

  if (!page)
    return;

  item.page = page;
  if (_layoutItems.isEmpty()) {
    item.row = 0;
    item.col = _firstCol;
  }
  else if (_layoutItems.last().col < _numCols - 1){
    item.row = _layoutItems.last().row;
    item.col = _layoutItems.last().col + 1;
  }
  else {
    item.row = _layoutItems.last().row + 1;
    item.col = 0;
  }
  _layoutItems.append(item);
}

void PDFPageLayout::removePage(PDFPageGraphicsItem * page) {
  QList<LayoutItem>::iterator it;
  int row, col;

  // **TODO:** Decide what to do with pages that are in the list multiple times
  // (see also insertPage())

  // First, find the page and remove it
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (it->page == page) {
      row = it->row;
      col = it->col;
      it = _layoutItems.erase(it);
      break;
    }
  }

  // Then, rearrange the pages behind it (no call to rearrange() to save time
  // by not going over the unchanged pages in front of the removed one)
  for (; it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}

void PDFPageLayout::insertPage(PDFPageGraphicsItem * page, PDFPageGraphicsItem * before /* = NULL */) {
  QList<LayoutItem>::iterator it;
  int row, col;
  LayoutItem item;

  item.page = page;

  // **TODO:** Decide what to do with pages that are in the list multiple times
  // (see also insertPage())

  // First, find the page to insert before and insert (row and col will be set
  // below)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (it->page == before) {
      row = it->row;
      col = it->col;
      it = _layoutItems.insert(it, item);
      break;
    }
  }
  if (it == _layoutItems.end()) {
    // We haven't found "before", so we just append the page
    addPage(page);
    return;
  }

  // Then, rearrange the pages starting from the inserted one (no call to
  // rearrange() to save time by not going over the unchanged pages)
  for (; it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}

// Relayout the pages on the canvas
void PDFPageLayout::relayout() {
  if (_isContinuous)
    continuousModeRelayout();
  else
    singlePageModeRelayout();
}

// Relayout the pages on the canvas in continuous mode
void PDFPageLayout::continuousModeRelayout() {
  // Create arrays to hold offsets and make sure that they have
  // sufficient space (to avoid moving the data around in memory)
  QVector<qreal> colOffsets(_numCols + 1, 0), rowOffsets(rowCount() + 1, 0);
  int i;
  qreal x, y;
  QList<LayoutItem>::iterator it;
  PDFPageGraphicsItem * page;
  QSizeF pageSize;
  QRectF sceneRect;

  // First, fill the offsets with the respective widths and heights
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    page = it->page;
    pageSize = page->_page->pageSizeF();

    if (colOffsets[it->col + 1] < pageSize.width() * page->_dpiX / 72.)
      colOffsets[it->col + 1] = pageSize.width() * page->_dpiX / 72.;
    if (rowOffsets[it->row + 1] < pageSize.height() * page->_dpiY / 72.)
      rowOffsets[it->row + 1] = pageSize.height() * page->_dpiY / 72.;
  }

  // Next, calculate cumulative offsets (including spacing)
  for (i = 1; i <= _numCols; ++i)
    colOffsets[i] += colOffsets[i - 1] + _xSpacing;
  for (i = 1; i <= rowCount(); ++i)
    rowOffsets[i] += rowOffsets[i - 1] + _ySpacing;

  // Finally, position pages
  // **TODO:** Figure out why this loop causes some noticeable lag when switching
  // from SinglePage to continuous mode in a large document (but not when
  // switching between separate continuous modes)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    // If we have more than one column, right-align the left-most column and
    // left-align the right-most column to avoid large space between columns
    // In all other cases, center the page in allotted space (in case we
    // stumble over pages of different sizes, e.g., landscape pages, etc.)
    pageSize = it->page->_page->pageSizeF();
    if (_numCols > 1 && it->col == 0)
      x = colOffsets[it->col + 1] - _xSpacing - pageSize.width() * page->_dpiX / 72.;
    else if (_numCols > 1 && it->col == _numCols - 1)
      x = colOffsets[it->col];
    else
      x = 0.5 * (colOffsets[it->col + 1] + colOffsets[it->col] - _xSpacing - pageSize.width() * page->_dpiX / 72.);
    // Always center the page vertically
    y = 0.5 * (rowOffsets[it->row + 1] + rowOffsets[it->row] - _ySpacing - pageSize.height() * page->_dpiY / 72.);
    it->page->setPos(x, y);
  }

  // leave some space around the pages (note that the space on the right/bottom
  // is already included in the corresponding Offset values)
  sceneRect.setRect(-_xSpacing, -_ySpacing, colOffsets[_numCols] + _xSpacing, rowOffsets[rowCount()] + _ySpacing);
  emit layoutChanged(sceneRect);
}

// Relayout the pages on the canvas in single page mode
void PDFPageLayout::singlePageModeRelayout()
{
  qreal width, height, maxWidth = 0.0, maxHeight = 0.0;
  QList<LayoutItem>::iterator it;
  PDFPageGraphicsItem * page;
  QSizeF pageSize;
  QRectF sceneRect;

  // We lay out all pages such that their center is in the origin (since only
  // one page is visible at any time, this is no problem)
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    if (!it->page || !it->page->_page)
      continue;
    page = it->page;
    pageSize = page->_page->pageSizeF();
    width = pageSize.width() * page->_dpiX / 72.;
    height = pageSize.height() * page->_dpiY / 72.;
    if (width > maxWidth)
      maxWidth = width;
    if (height > maxHeight)
      maxHeight = height;
    page->setPos(-width / 2., -height / 2.);
  }

  sceneRect.setRect(-maxWidth / 2., -maxHeight / 2., maxWidth, maxHeight);
  emit layoutChanged(sceneRect);
}

void PDFPageLayout::rearrange() {
  QList<LayoutItem>::iterator it;
  int row, col;

  row = 0;
  col = _firstCol;
  for (it = _layoutItems.begin(); it != _layoutItems.end(); ++it) {
    it->row = row;
    it->col = col;

    ++col;
    if (col >= _numCols) {
      col = 0;
      ++row;
    }
  }
}


// vim: set sw=2 ts=2 et

